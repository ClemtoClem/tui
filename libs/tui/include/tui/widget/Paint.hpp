/**
 * @file Paint.hpp
 * @brief Petites aides de dessin partagées par les widgets.
 */

#pragma once

#include "../Buffer.hpp"
#include "../core/Text.hpp"

#include <string>

namespace tui::paint {

/// Écrit `text` (UTF-8) à partir de (x, y), tronqué à `max_w` colonnes ;
/// renvoie la largeur écrite. Gère les glyphes larges.
inline int text(Buffer& b, int x, int y, int max_w, const std::string& text, Color fg = Color::Default(),
                Color bg = Color::Default(), TextStyle st = TextStyle::None) {
    if (max_w <= 0) return 0;
    auto decoded = TextHelper::truncate_to_width(TextHelper::decode_utf8(text), max_w);
    int w = 0;
    for (char32_t ch : decoded) {
        int cw = TextHelper::display_width(std::u32string(1, ch));
        if (cw <= 0) cw = 1;
        b.set(x + w, y, Cell{ch, static_cast<uint8_t>(cw), fg, bg, st});
        if (cw == 2) b.set(x + w + 1, y, Cell::continuation());
        w += cw;
    }
    return w;
}

[[nodiscard]] inline int width(const std::string& text) {
    return TextHelper::display_width(TextHelper::decode_utf8(text));
}

/// Remplit un rectangle avec `ch`.
inline void fill(Buffer& b, Rect r, char32_t ch = U' ', Color fg = Color::Default(), Color bg = Color::Default(),
                 TextStyle st = TextStyle::None) {
    for (int y = r.y; y < r.bottom(); ++y)
        for (int x = r.x; x < r.right(); ++x) b.set(x, y, Cell{ch, 1, fg, bg, st});
}

} // namespace tui::paint
