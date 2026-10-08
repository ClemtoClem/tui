/**
 * @file BoxFrame.hpp
 * @brief Dessin de cadre partagé (utilisé par Border et Dialog).
 */

#pragma once

#include "../Buffer.hpp"
#include "../core/Text.hpp"

#include <algorithm>
#include <string>

namespace tui {

/// Jeu de caractères utilisé pour tracer un cadre. Rounded reproduit le
/// style "lipgloss" (coins arrondis) qu'on retrouve dans planor et la
/// plupart des TUI Bubble Tea.
enum class BorderStyle {
    Square,
    Rounded,
};

struct BoxChars {
    char32_t top_left, top_right, bottom_left, bottom_right, horizontal, vertical;
};

[[nodiscard]] inline BoxChars box_chars(BorderStyle style) {
    switch (style) {
        case BorderStyle::Rounded:
            return {U'╭', U'╮', U'╰', U'╯', U'─', U'│'};
        case BorderStyle::Square:
        default:
            return {U'┌', U'┐', U'└', U'┘', U'─', U'│'};
    }
}

inline void draw_box_frame(Buffer& buffer, Rect bounds, const std::string& title = "",
                            BorderStyle style = BorderStyle::Square, Color color = Color::Default(),
                            Color title_color = Color::Default()) {
    if (bounds.width < 2 || bounds.height < 2) return;
    int x0 = bounds.x, y0 = bounds.y;
    int x1 = bounds.x + bounds.width - 1, y1 = bounds.y + bounds.height - 1;
    BoxChars chars = box_chars(style);

    auto put = [&](int x, int y, char32_t ch) {
        buffer.set(x, y, Cell{ch, 1, color, Color::Default(), TextStyle::None});
    };

    put(x0, y0, chars.top_left);
    put(x1, y0, chars.top_right);
    put(x0, y1, chars.bottom_left);
    put(x1, y1, chars.bottom_right);
    for (int x = x0 + 1; x < x1; ++x) { put(x, y0, chars.horizontal); put(x, y1, chars.horizontal); }
    for (int y = y0 + 1; y < y1; ++y) { put(x0, y, chars.vertical); put(x1, y, chars.vertical); }

    if (!title.empty()) {
        auto decoded = TextHelper::decode_utf8(title);
        int available = std::max(x1 - x0 - 3, 0);
        auto truncated = TextHelper::truncate_to_width(decoded, available, U"…");
        int tx = x0 + 2;
        for (char32_t ch : truncated) {
            if (tx >= x1 - 1) break;
            buffer.set(tx, y0, Cell{ch, 1, title_color, Color::Default(), TextStyle::None});
            ++tx;
        }
    }
}

} // namespace tui
