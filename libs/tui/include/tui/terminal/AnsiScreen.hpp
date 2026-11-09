/**
 * @file AnsiScreen.hpp
 * @brief Émulateur d'écran ANSI/VT100 minimal : interprète la SORTIE d'un
 * process enfant (shell dans un pty) en une grille de Cell peignable.
 *
 * Symétrique de VTParser (qui décode l'ENTRÉE clavier/souris d'un vrai
 * terminal) mais un problème différent : ici on interprète le flux
 * d'octets qu'un programme écrit pour dessiner un écran, pas des touches.
 *
 * Portée volontairement limitée (documenté, pas oublié) :
 * - Pas d'écran alternatif (CSI ?1049h/l ignoré) : une appli plein écran
 *   comme vim/less s'affiche sur le même buffer, sans "revenir" à l'état
 *   précédent en sortant - correct pour une utilisation shell classique
 *   (ls, git, cat, éditeurs en mode ligne...), pas pour un TUI imbriqué.
 * - Pas de scrollback : seule la grille visible existe, rien au-delà.
 * - Pas de régions de défilement (DECSTBM) ni de sauvegarde/restauration
 *   de curseur (DECSC/DECRC) : CSI reconnu mais non pertinent est ignoré
 *   proprement plutôt que de planter.
 */

#pragma once

#include "../Buffer.hpp"
#include "../core/Text.hpp"

#include <string>
#include <string_view>

namespace tui {

class AnsiScreen {
public:
    explicit AnsiScreen(int cols = 80, int rows = 24) { resize(cols, rows); }

    void resize(int cols, int rows);
    void feed(std::string_view bytes);

    [[nodiscard]] const Buffer& grid() const { return grid_; }
    [[nodiscard]] int cursor_row() const { return cursor_row_; }
    [[nodiscard]] int cursor_col() const { return cursor_col_; }
    [[nodiscard]] int cols() const { return cols_; }
    [[nodiscard]] int rows() const { return rows_; }

private:
    void put_char(char32_t ch);
    void line_feed();
    void scroll_up();
    void handle_csi(const std::string& params, char final_byte);
    void handle_sgr(const std::string& params);
    void erase_in_line(int mode);
    void erase_in_display(int mode);

    Buffer grid_;
    int cols_ = 80;
    int rows_ = 24;
    int cursor_row_ = 0;
    int cursor_col_ = 0;

    Color cur_fg_ = Color::Default();
    Color cur_bg_ = Color::Default();
    TextStyle cur_style_ = TextStyle::None;

    enum class ParseState { Normal, Escape, Csi, Osc };
    ParseState state_ = ParseState::Normal;
    std::string csi_buffer_;
    std::string buffer_; // octets non encore consommés (séquence/UTF-8 incomplets à cheval sur deux feed())
};

} // namespace tui
