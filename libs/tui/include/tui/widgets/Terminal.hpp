/**
 * @file Terminal.hpp
 * @brief Panneau terminal intégré : shell lancé dans un pseudo-terminal
 * (POSIX forkpty), sortie interprétée via AnsiScreen, avec scrollback
 * (défilement), piste de défilement et sélection/copie à la souris.
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

    /// Largeur logique du terminal (largeur du pty). Par défaut 0 = celle de
    /// l'affichage. Une valeur supérieure à la largeur d'affichage fait
    /// apparaître un défilement horizontal.
    void set_content_width(int cols) {
        logical_cols_ = std::max(cols, 0);
        arrange(bounds_);
        need_repaint();
    }
    [[nodiscard]] int content_width_cols() const { return logical_cols_ > 0 ? logical_cols_ : bounds_.width; }

    [[nodiscard]] Size measure(const Constraints& c) const override { return c.clamp({c.max_width, c.max_height}); }
    void arrange(Rect final_rect) override;
    void paint(Buffer& buffer) const override;
    [[nodiscard]] Size content_size() const override;
    [[nodiscard]] Point scroll_offset() const override;
    void set_scroll_offset(int x, int y) override;

    [[nodiscard]] bool focusable() const override { return true; }
    void on_focus() override { focused_ = true; need_repaint(); }
    void on_blur() override { focused_ = false; need_repaint(); }

    bool on_key(const KeyEvent& e) override;
    bool on_mouse(const MouseEvent& e) override;

private:
    void write_to_child(std::string_view bytes);
    void sync_pty_size();

    /// Cellule affichée à (ligne, colonne) de l'écran, en tenant compte du
    /// scrollback et des offsets de défilement (vertical et horizontal).
    [[nodiscard]] Cell cell_at(int row, int col) const;
    /// Texte de la sélection (coordonnées écran), avec les retours à la ligne.
    [[nodiscard]] std::string selected_text() const;
    void clear_selection();
    void scroll_to(int offset);
    void scroll_by(int delta);
    [[nodiscard]] int max_scroll_col() const;

    int master_fd_ = -1;
    pid_t pid_ = -1;
    AnsiScreen screen_;
    bool focused_ = false;

    // --- Scrollback / défilement ---
    int scroll_offset_ = 0;   ///< première ligne visible (offset depuis le haut)
    int scroll_col_ = 0;      ///< première colonne visible (défilement horizontal)
    int logical_cols_ = 0;    ///< largeur logique (0 = largeur d'affichage)

    // --- Sélection (copie) ---
    bool selecting_ = false;
    int sel_anchor_row_ = 0, sel_anchor_col_ = 0;
    int sel_row_ = 0, sel_col_ = 0;   ///< extrémité courante (écran)
};

} // namespace tui
