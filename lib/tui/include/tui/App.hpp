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
#include "core/Timer.hpp"
#include "terminal/ITerminalBackend.hpp"
#include "widget/FocusManager.hpp"
#include "widget/Widget.hpp"

#include <algorithm>
#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>

namespace tui {

class App {
public:
    explicit App(std::unique_ptr<ITerminalBackend> backend) : backend_(std::move(backend)) {}

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

private:
    [[nodiscard]] static std::shared_ptr<Widget> hit_test(const std::shared_ptr<Widget>& node, Point p) {
        if (!node || !node->visible() || !node->bounds().contains(p)) return nullptr;
        auto children = node->children();
        for (auto it = children.rbegin(); it != children.rend(); ++it) {
            if (auto hit = hit_test(*it, p)) return hit;
        }
        return node;
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
};

} // namespace tui
