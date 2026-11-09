#include "tui/App.hpp"

namespace tui {

    int App::run() {
        backend_->init();
        if (mouse_enabled_) backend_->enable_mouse(true);
        buffer_.resize(backend_->get_width(), backend_->get_height());

        // Un premier arrange() avant la boucle garantit des bounds()
        // valides dès le tout premier événement traité (sinon un clic
        // reçu avant le premier render_frame() échouerait son
        // hit-test : bounds_ vaudrait encore {0,0,0,0}).
        if (root_) {
            Size available{buffer_.width(), buffer_.height()};
            if (styling_active()) stylesheet_.apply(*root_, {focus_.current().get(), dark_});
            (void)root_->measure_outer(Constraints::tight(available));
            root_->layout(Rect{0, 0, available.width, available.height});
        }

        running_ = true;
        dirty_ = true;

        while (running_) {
            auto now = TimerManager::Clock::now();
            auto next_timer = timers_.time_until_next(now);
            constexpr auto kIdlePoll = std::chrono::milliseconds(50);
            auto wait = next_timer
                ? std::min(std::chrono::duration_cast<std::chrono::milliseconds>(*next_timer), kIdlePoll)
                : kIdlePoll;

            if (auto event = backend_->poll_event(wait)) {
                handle_event(*event);
            }

            timers_.run_due(TimerManager::Clock::now());
            drain_posted();
            check_css_watch();

            if (dirty_) {
                render_frame();
                dirty_ = false;
            }
        }

        backend_->shutdown();
        return exit_code_;
    }

    void App::handle_event(const Event& ev) {
        std::visit([this](auto&& e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, ResizeEvent>) {
                buffer_.resize(e.width, e.height);
                dirty_ = true;
            } else if constexpr (std::is_same_v<T, KeyEvent>) {
                // 1) Interception globale d'abord : permet à l'application
                //    de capter un raccourci indépendamment du focus.
                if (global_key_handler_ && global_key_handler_(e)) {
                    dirty_ = true;
                    return;
                }
                // 2) Distribution normale.
                if (e.key == Key::Tab) {
                    focus_.focus_next(root_);
                    dirty_ = true;
                } else if (e.key == Key::BackTab) {
                    focus_.focus_prev(root_);
                    dirty_ = true;
                } else {
                    auto target = focus_.current();
                    if (!target) target = root_;
                    if (target && target->on_key(e)) dirty_ = true;
                }
            } else if constexpr (std::is_same_v<T, MouseEvent>) {
                handle_mouse(e);
            }
        }, ev);
    }

    void App::render_frame() {
        if (backend_->get_width() != buffer_.width() || backend_->get_height() != buffer_.height()) {
            buffer_.resize(backend_->get_width(), backend_->get_height());
        }
        buffer_.back().clear();
        if (root_) {
            Size available{buffer_.width(), buffer_.height()};
            if (styling_active()) stylesheet_.apply(*root_, {focus_.current().get(), dark_});
            (void)root_->measure_outer(Constraints::tight(available));
            root_->layout(Rect{0, 0, available.width, available.height});
            root_->render(buffer_.back());
        }
        buffer_.diff_and_flush(*backend_);
    }


    // ------------------------------------------------------------------
    // Souris : survol, focus au clic, capture, remontée vers les parents
    // ------------------------------------------------------------------

    void App::update_hover(const std::shared_ptr<Widget>& target) {
        if (hovered_.lock() == target) return;
        for (auto& w : hover_chain_)
            if (auto sp = w.lock()) sp->set_hovered(false);
        hover_chain_.clear();
        hovered_ = target;
        for (Widget* w = target.get(); w; w = w->parent()) {
            w->set_hovered(true);
            hover_chain_.push_back(w->weak_from_this());
        }
        if (styling_active()) dirty_ = true;
    }

    void App::handle_mouse(const MouseEvent& raw) {
        using A = MouseEvent::Action;
        using B = MouseEvent::Button;
        MouseEvent e = raw;
        // Double-clic : même bouton, même cellule, dans le délai imparti.
        if (e.action == A::Press && (e.button == B::Left || e.button == B::Right || e.button == B::Middle)) {
            auto now = std::chrono::steady_clock::now();
            bool same = e.button == last_click_button_ && Point{e.x, e.y} == last_click_pos_ &&
                        now - last_click_time_ <= double_click_ms_;
            click_count_ = same ? click_count_ + 1 : 1;
            last_click_time_ = now;
            last_click_pos_ = {e.x, e.y};
            last_click_button_ = e.button;
            e.clicks = click_count_;
        }
        auto target = overlay_hit(root_, {e.x, e.y});
        if (!target) target = hit_test(root_, {e.x, e.y});
        auto scope = focus_.current_scope();
        // Une modale ouverte isole le reste de l'interface.
        if (scope && target && !target->is_descendant_of(scope.get())) target = scope;
        update_hover(target);

        // Capture : glisser / relâcher reviennent au widget qui a reçu l'appui.
        if (auto cap = captured_.lock(); cap && (e.action == A::Drag || e.action == A::Release)) {
            if (e.action == A::Release) captured_.reset();
            if (cap->on_mouse(e)) dirty_ = true;
            return;
        }
        if (!target) return;
        if (e.action == A::Move) {
            // Simple survol : proposé au widget et à ses parents, sans effet sinon.
            for (Widget* w = target.get(); w; w = w->parent())
                if (!w->disabled() && w->on_mouse(e)) {
                    dirty_ = true;
                    break;
                }
            return;
        }
        if (target->disabled()) return;

        bool press = e.action == A::Press && (e.button == B::Left || e.button == B::Right || e.button == B::Middle);
        if (press && click_to_focus_) {
            if (Widget* f = target->focusable_ancestor(); f && (!scope || f->is_descendant_of(scope.get()))) {
                if (auto sp = f->weak_from_this().lock(); sp && sp != focus_.current()) {
                    focus_.set_focus(sp);
                    dirty_ = true;
                }
            }
        }
        for (Widget* w = target.get(); w; w = w->parent()) {
            if (w->on_mouse(e)) {
                dirty_ = true;
                if (press) captured_ = w->weak_from_this();
                break;
            }
            if (scope && w == scope.get()) break;
        }
    }

    // ------------------------------------------------------------------
    // Styles et thèmes
    // ------------------------------------------------------------------

    bool App::set_css(std::string_view text, const std::string& origin) {
        stylesheet_.remove_origin(origin);
        bool ok = stylesheet_.add_source(text, origin, 1);
        dirty_ = true;
        return ok;
    }

    bool App::load_css_file(const std::filesystem::path& path, bool watch) {
        std::string origin = path.string();
        stylesheet_.remove_origin(origin);
        bool ok = stylesheet_.add_file(path, 1);
        dirty_ = true;
        if (watch) {
            std::error_code ec;
            auto mt = std::filesystem::last_write_time(path, ec);
            std::erase_if(watched_, [&](const WatchedCss& w) { return w.path == path; });
            watched_.push_back({path, ec ? std::filesystem::file_time_type{} : mt});
        }
        return ok;
    }

    void App::check_css_watch() {
        if (watched_.empty()) return;
        auto now = std::chrono::steady_clock::now();
        if (now - last_watch_check_ < std::chrono::milliseconds(500)) return;
        last_watch_check_ = now;
        for (auto& w : watched_) {
            std::error_code ec;
            auto mt = std::filesystem::last_write_time(w.path, ec);
            if (ec || mt == w.mtime) continue;
            w.mtime = mt;
            stylesheet_.remove_origin(w.path.string());
            (void)stylesheet_.add_file(w.path, 1);
            dirty_ = true;
        }
    }

    void App::use_default_stylesheet(bool on) {
        if (on == default_css_) return;
        default_css_ = on;
        stylesheet_.remove_origin("<default>");
        if (on) (void)stylesheet_.add_source(css::default_stylesheet(), "<default>", 0);
        dirty_ = true;
    }

    bool App::set_theme(const std::string& name) {
        const themes::ThemeSpec* spec = themes::find(name);
        if (!spec) return false;
        theme_name_ = spec->name;
        dark_ = spec->dark;
        stylesheet_.set_theme_variables(themes::generate_variables(*spec));
        role_theme_ = themes::to_role_theme(*spec);
        dirty_ = true;
        if (on_theme_change_) on_theme_change_(theme_name_);
        return true;
    }

    std::string App::next_theme() {
        auto all = themes::names();
        auto it = std::find(all.begin(), all.end(), theme_name_);
        size_t i = it == all.end() ? 0 : static_cast<size_t>(it - all.begin() + 1) % all.size();
        set_theme(all[i]);
        return theme_name_;
    }

}
