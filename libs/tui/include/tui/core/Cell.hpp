/**
 * @file Cell.hpp
 * @brief Une cellule du grid terminal : un codepoint stylé + sa largeur d'affichage.
 */

#pragma once

#include "Color.hpp"
#include "TextStyle.hpp"

namespace tui {

/// Une cellule de la grille terminal.
///
/// `width` vaut 0, 1 ou 2 : les glyphes larges (CJK, emoji...) occupent
/// deux colonnes ; la seconde colonne est représentée par une cellule
/// "continuation" avec `width == 0` et `codepoint == 0`, pour que le
/// diff/rendu ne dessine jamais dans la seconde moitié d'un glyphe large.
struct Cell {
    char32_t codepoint = U' ';
    uint8_t width = 1;
    Color fg = Color::Default();
    Color bg = Color::Default();
    TextStyle style = TextStyle::None;

    constexpr bool operator==(const Cell&) const = default;

    [[nodiscard]] static constexpr Cell blank() { return Cell{}; }

    [[nodiscard]] static constexpr Cell continuation() {
        Cell c;
        c.codepoint = 0;
        c.width = 0;
        return c;
    }
};

} // namespace tui
