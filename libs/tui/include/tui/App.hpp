/**
 * @file App.hpp
 * @brief Runtime de l'application TUI : boucle d'événements, timers, thread-safety.
 *
 * Routage des événements : Tab/Shift+Tab déplacent le focus (via
 * FocusManager) plutôt que d'être transmis au widget focusé ; les
 * autres touches vont au widget actuellement focusé (repli sur la
 * racine si rien n'est focusé). Les événements souris sont distribués
 * par hit-testing géométrique (le widget le plus profond dont bounds()
 * contient le point, en testant les enfants du dernier ajouté au
 * premier - le dernier peint est visuellement "au-dessus").
 */

#pragma once

#include "Buffer.hpp"
#include "core/Theme.hpp"
#include "core/Timer.hpp"
#include "style/Stylesheet.hpp"
#include "style/Themes.hpp"
#include "terminal/ITerminalBackend.hpp"
#include "widget/FocusManager.hpp"
#include "widget/Widget.hpp"

#include <algorithm>
#include <chrono>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>

namespace tui {

class App {
public:
    explicit App(std::unique_ptr<ITerminalBackend> backend) : backend_(std::move(backend)) { set_theme("textual-dark"); }

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    void set_root(std::shared_ptr<Widget> root) {
        root_ = std::move(root);
        if (root_) {
            root_->set_parent(nullptr);
            root_->set_repaint_callback([this] { dirty_ = true; });
        }
        dirty_ = true;
    }

    [[nodiscard]] FocusManager& focus_manager() { return focus_; }

        /// Installe un intercepteur clavier global appelé AVANT la
    /// distribution normale (FocusManager, puis widget focusé). Si le
    /// handler retourne true, l'événement est considéré consommé et
    /// n'atteint ni le FocusManager ni le widget focusé.
    ///
    /// Usage typique : raccourcis indépendants du focus (Ctrl+Q pour
    /// quitter, F1 pour l'aide, Ctrl+P pour une palette de commandes…).
    /// Passer un handler vide (ou nullptr) pour désactiver.
    void set_global_key_handler(std::function<bool(const KeyEvent&)> handler) {
        global_key_handler_ = std::move(handler);
    }

    /// Lance la boucle principale ; bloque jusqu'à quit(). Retourne le
    /// code passé à quit() (0 par défaut).
    int run();

    void quit(int code = 0) {
        running_ = false;
        exit_code_ = code;
    }

    /// Seul point d'entrée thread-safe : programme `fn` pour exécution
    /// sur le thread de App::run(), et réveille poll_event() si celui-ci
    /// est bloqué en attente d'entrée terminal.
    void post(std::function<void()> fn) {
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            posted_.push_back(std::move(fn));
        }
        backend_->wake_up();
    }

    TimerId add_timer(std::chrono::milliseconds interval, bool repeat, std::function<void()> callback) {
        return timers_.add(TimerManager::Clock::now(), interval, repeat, std::move(callback));
    }

    void cancel_timer(TimerId id) { timers_.cancel(id); }

    void need_repaint() { dirty_ = true; }

    [[nodiscard]] ITerminalBackend& backend() { return *backend_; }

    // ------------------------------------------------------------------
    // Styles et thèmes (voir style/Stylesheet.hpp, style/Themes.hpp)
    // ------------------------------------------------------------------
    //
    // Sans feuille de style, le rendu est strictement celui des widgets.
    // Dès qu'une feuille est chargée (CSS de l'application et/ou feuille
    // par défaut), les styles sont recalculés à chaque frame.

    [[nodiscard]] css::Stylesheet& stylesheet() { return stylesheet_; }

    /// Remplace le CSS de l'application (origine `origin`) ; false si erreurs
    /// (les règles valides sont appliquées quand même, voir css_errors()).
    bool set_css(std::string_view text, const std::string& origin = "<app>");
    /// Charge un fichier .tcss ; `watch` le recharge dès qu'il change (sondage 500 ms).
    bool load_css_file(const std::filesystem::path& path, bool watch = false);
    /// Active la feuille par défaut à l'apparence Textual (tier 0).
    void use_default_stylesheet(bool on = true);
    [[nodiscard]] const std::vector<css::ParseError>& css_errors() const { return stylesheet_.errors(); }

    /// Change le thème (variables CSS $primary…) ; false si inconnu.
    bool set_theme(const std::string& name);
    [[nodiscard]] const std::string& theme_name() const { return theme_name_; }
    /// Passe au thème intégré suivant ; renvoie son nom.
    std::string next_theme();
    /// Palette par rôle dérivée du thème courant (widgets à couleurs fixes).
    [[nodiscard]] const Theme& role_theme() const { return role_theme_; }
    [[nodiscard]] bool dark() const { return dark_; }
    /// Appelé après chaque changement de thème.
    void set_on_theme_change(std::function<void(const std::string&)> cb) { on_theme_change_ = std::move(cb); }

    /// Active (défaut) le focus au clic, comme Textual.
    void set_click_to_focus(bool on) { click_to_focus_ = on; }

    /// Souris activée par défaut : run() demande au terminal de signaler
    /// clics, molette et mouvements. Maintenir Maj permet alors, dans la
    /// plupart des terminaux, de sélectionner du texte comme d'habitude.
    void set_mouse_enabled(bool on) {
        mouse_enabled_ = on;
        if (running_) backend_->enable_mouse(on);
    }
    [[nodiscard]] bool mouse_enabled() const { return mouse_enabled_; }

    /// Délai maximal entre deux clics pour un double-clic (défaut 400 ms).
    void set_double_click_interval(std::chrono::milliseconds ms) { double_click_ms_ = ms; }

    /// Widget le plus profond sous (x, y), en tenant compte des bordures CSS.
    [[nodiscard]] std::shared_ptr<Widget> widget_at(int x, int y) const {
        if (auto o = overlay_hit(root_, {x, y})) return o;
        return hit_test(root_, {x, y});
    }
    /// Widget survolé par la souris (ou nullptr).
    [[nodiscard]] std::shared_ptr<Widget> hovered() const { return hovered_.lock(); }

    /// Traite un événement comme s'il venait du terminal (tests, automatisation).
    void dispatch(const Event& ev) { handle_event(ev); }
    /// Calcule styles et mise en page puis peint une frame (tests).
    void refresh() { render_frame(); }

private:
    [[nodiscard]] static std::shared_ptr<Widget> hit_test(const std::shared_ptr<Widget>& node, Point p) {
        if (!node || !node->visible() || !node->outer_bounds().contains(p)) return nullptr;
        auto children = node->children();
        for (auto it = children.rbegin(); it != children.rend(); ++it) {
            if (auto hit = hit_test(*it, p)) return hit;
        }
        return node;
    }

    /// Widget dont un popup débordant contient p (le dernier peint d'abord).
    [[nodiscard]] static std::shared_ptr<Widget> overlay_hit(const std::shared_ptr<Widget>& node, Point p) {
        if (!node || !node->visible()) return nullptr;
        auto children = node->children();
        for (auto it = children.rbegin(); it != children.rend(); ++it)
            if (auto hit = overlay_hit(*it, p)) return hit;
        return node->overlay_contains(p) ? node : nullptr;
    }

    void handle_event(const Event& ev);

    void drain_posted() {
        std::deque<std::function<void()>> local;
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            std::swap(local, posted_);
        }
        for (auto& fn : local) fn();
    }

    void render_frame();
    void handle_mouse(const MouseEvent& e);
    void update_hover(const std::shared_ptr<Widget>& target);
    [[nodiscard]] bool styling_active() const { return !stylesheet_.empty(); }
    void check_css_watch();

    std::unique_ptr<ITerminalBackend> backend_;
    std::shared_ptr<Widget> root_;
    TimerManager timers_;
    FocusManager focus_;
    DoubleBuffer buffer_;

    std::mutex queue_mutex_;
    std::deque<std::function<void()>> posted_;
    std::function<bool(const KeyEvent&)> global_key_handler_;

    bool running_ = false;
    bool dirty_ = true;
    int exit_code_ = 0;

    css::Stylesheet stylesheet_;
    std::string theme_name_;
    Theme role_theme_;
    bool dark_ = true;
    bool default_css_ = false;
    bool click_to_focus_ = true;
    bool mouse_enabled_ = true;
    std::chrono::milliseconds double_click_ms_{400};
    std::chrono::steady_clock::time_point last_click_time_{};
    Point last_click_pos_{-1, -1};
    MouseEvent::Button last_click_button_ = MouseEvent::Button::None;
    int click_count_ = 0;
    std::function<void(const std::string&)> on_theme_change_;
    std::vector<std::weak_ptr<Widget>> hover_chain_;
    std::weak_ptr<Widget> hovered_;
    std::weak_ptr<Widget> captured_;
    struct WatchedCss {
        std::filesystem::path path;
        std::filesystem::file_time_type mtime;
    };
    std::vector<WatchedCss> watched_;
    std::chrono::steady_clock::time_point last_watch_check_{};
};

} // namespace tui
