#include "tui/App.hpp"

namespace tui {

    int App::run() {
        backend_->init();
        buffer_.resize(backend_->get_width(), backend_->get_height());

        // Un premier arrange() avant la boucle garantit des bounds()
        // valides dès le tout premier événement traité (sinon un clic
        // reçu avant le premier render_frame() échouerait son
        // hit-test : bounds_ vaudrait encore {0,0,0,0}).
        if (root_) {
            Size available{buffer_.width(), buffer_.height()};
            root_->measure(Constraints::tight(available));
            root_->arrange(Rect{0, 0, available.width, available.height});
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
                auto target = hit_test(root_, {e.x, e.y});
                if (target && target->on_mouse(e)) dirty_ = true;
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
            root_->measure(Constraints::tight(available));
            root_->arrange(Rect{0, 0, available.width, available.height});
            root_->paint(buffer_.back());
        }
        buffer_.diff_and_flush(*backend_);
    }

}