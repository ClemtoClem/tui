/**
 * @file Terminal.hpp
 * @brief Panneau terminal intégré : shell lancé dans un pseudo-terminal
 * (POSIX forkpty), sortie interprétée via AnsiScreen.
 *
 * L'App n'a pas de notion générique de "surveiller un descripteur de
 * fichier supplémentaire" (sa boucle ne fait que poll_event() sur le
 * backend + les timers) : l'hôte doit donc appeler pump() périodiquement
 * (ex: App::add_timer(16ms, true, ...)) pour drainer la sortie du
 * processus enfant et rafraîchir l'écran. Ce choix évite de coupler App
 * à epoll/kqueue pour un seul widget qui n'en a besoin qu'en option.
 */

#pragma once

#include "../Buffer.hpp"
#include "../terminal/AnsiScreen.hpp"
#include "../widget/Widget.hpp"

#include <string>

namespace tui {

class Terminal : public Widget {
public:
    ~Terminal() override;

    /// Démarre `shell` (défaut : $SHELL, sinon /bin/sh) dans un pty
    /// attaché à ce widget. No-op si déjà démarré. Faux si fork/openpty
    /// échoue (ex: plus de ptys disponibles).
    bool start(const std::string& shell = "");
    void stop();
    [[nodiscard]] bool running() const { return pid_ > 0; }

    /// Lit sans bloquer la sortie disponible du processus enfant et met à
    /// jour l'écran ; no-op si rien à lire ou si non démarré.
    void pump();

    [[nodiscard]] Size measure(const Constraints& c) const override { return c.clamp({c.max_width, c.max_height}); }
    void arrange(Rect final_rect) override;
    void paint(Buffer& buffer) const override;

    [[nodiscard]] bool focusable() const override { return true; }
    void on_focus() override { focused_ = true; need_repaint(); }
    void on_blur() override { focused_ = false; need_repaint(); }

    bool on_key(const KeyEvent& e) override;
    bool on_mouse(const MouseEvent& e) override;

private:
    void write_to_child(std::string_view bytes);
    void sync_pty_size();

    int master_fd_ = -1;
    pid_t pid_ = -1;
    AnsiScreen screen_;
    bool focused_ = false;
};

} // namespace tui
