/**
 * @file Digits.hpp
 * @brief Grands chiffres sur 3 lignes en caractères de cadre (Textual « Digits »).
 * Reconnaît 0-9, A-F, « : », « . », « , », « - », « + », « % », espace.
 * CSS : « text-align: center | right ».
 */

#pragma once

#include "../widget/Paint.hpp"
#include "../widget/Widget.hpp"

#include <array>
#include <string>

namespace tui {

class Digits : public Widget {
public:
    explicit Digits(std::string value = {}) : value_(std::move(value)) {}
    void set_value(std::string v) { value_ = std::move(v); need_repaint(); }
    [[nodiscard]] const std::string& value() const { return value_; }

    /// Les 3 lignes du glyphe de `ch`.
    [[nodiscard]] static std::array<std::string, 3> glyph(char32_t ch) {
        switch (ch) {
        case U'0': return {"╭─╮", "│ │", "╰─╯"};
        case U'1': return {"╶┐ ", " │ ", "╶┴╴"};
        case U'2': return {"╶─╮", "┌─┘", "╰─╴"};
        case U'3': return {"╶─╮", " ─┤", "╶─╯"};
        case U'4': return {"╷ ╷", "╰─┤", "  ╵"};
        case U'5': return {"╭─╴", "╰─╮", "╶─╯"};
        case U'6': return {"╭─╴", "├─╮", "╰─╯"};
        case U'7': return {"╶─┐", "  │", "  ╵"};
        case U'8': return {"╭─╮", "├─┤", "╰─╯"};
        case U'9': return {"╭─╮", "╰─┤", "╶─╯"};
        case U'A': case U'a': return {"╭─╮", "├─┤", "╵ ╵"};
        case U'B': case U'b': return {"┌╮ ", "├┴╮", "╰─╯"};
        case U'C': case U'c': return {"╭─╴", "│  ", "╰─╴"};
        case U'D': case U'd': return {"┌─╮", "│ │", "└─╯"};
        case U'E': case U'e': return {"╭─╴", "├─ ", "╰─╴"};
        case U'F': case U'f': return {"╭─╴", "├─ ", "╵  "};
        case U':': return {" ", ":", " "};
        case U'.': return {" ", " ", "."};
        case U',': return {" ", " ", ","};
        case U'-': return {"   ", "╶─╴", "   "};
        case U'+': return {"   ", "╶┼╴", "   "};
        case U'%': return {"o ╱", " ╱ ", "╱ o"};
        case U' ': return {" ", " ", " "};
        default: {
            std::string s = TextHelper::encode_utf8(std::u32string(1, ch));
            return {" ", s, " "};
        }
        }
    }

    /// Les 3 lignes rendues de la valeur.
    [[nodiscard]] std::array<std::string, 3> rows() const {
        std::array<std::string, 3> out;
        for (char32_t ch : TextHelper::decode_utf8(value_)) {
            auto g = glyph(ch);
            for (int i = 0; i < 3; ++i) out[static_cast<size_t>(i)] += g[static_cast<size_t>(i)];
        }
        return out;
    }

    [[nodiscard]] Size measure(const Constraints& c) const override {
        return c.clamp({paint::width(rows()[1]), 3});
    }

    void paint(Buffer& b) const override {
        auto r = rows();
        int w = paint::width(r[1]);
        std::string align = style().extra_value("text-align");
        int x = align == "center" ? bounds_.x + (bounds_.width - w) / 2
              : align == "right"  ? bounds_.right() - w
                                  : bounds_.x;
        for (int i = 0; i < 3 && i < bounds_.height; ++i)
            paint::text(b, std::max(x, bounds_.x), bounds_.y + i, bounds_.width, r[static_cast<size_t>(i)]);
    }

private:
    std::string value_;
};

} // namespace tui
