/**
 * @file Rule.hpp
 * @brief Ligne de séparation horizontale ou verticale (Textual « Rule »).
 * CSS : « Rule { color: $secondary; } », propriété « line-style »
 * (solid, heavy, double, dashed, thick, ascii, blank).
 */

#pragma once

#include "../widget/Widget.hpp"
#include "../Buffer.hpp"

#include <string>

namespace tui {

class Rule : public Widget {
public:
    enum class Orientation { Horizontal, Vertical };
    enum class Line { Solid, Heavy, Double, Dashed, Thick, Ascii, Blank };

    explicit Rule(Orientation o = Orientation::Horizontal, Line l = Line::Solid) : orientation_(o), line_(l) {}

    void set_orientation(Orientation o) { orientation_ = o; need_repaint(); }
    void set_line(Line l) { line_ = l; need_repaint(); }
    [[nodiscard]] Orientation orientation() const { return orientation_; }

    [[nodiscard]] Size measure(const Constraints& c) const override {
        return orientation_ == Orientation::Horizontal ? c.clamp({c.max_width, 1}) : c.clamp({1, c.max_height});
    }

    void paint(Buffer& b) const override {
        Line l = line_;
        std::string ls = style().extra_value("line-style");
        if (ls == "solid") l = Line::Solid;
        else if (ls == "heavy") l = Line::Heavy;
        else if (ls == "double") l = Line::Double;
        else if (ls == "dashed") l = Line::Dashed;
        else if (ls == "thick") l = Line::Thick;
        else if (ls == "ascii") l = Line::Ascii;
        else if (ls == "blank") l = Line::Blank;
        bool h = orientation_ == Orientation::Horizontal;
        char32_t ch = U' ';
        switch (l) {
        case Line::Solid: ch = h ? U'─' : U'│'; break;
        case Line::Heavy: ch = h ? U'━' : U'┃'; break;
        case Line::Double: ch = h ? U'═' : U'║'; break;
        case Line::Dashed: ch = h ? U'╌' : U'╎'; break;
        case Line::Thick: ch = U'█'; break;
        case Line::Ascii: ch = h ? U'-' : U'|'; break;
        case Line::Blank: ch = U' '; break;
        }
        for (int y = bounds_.y; y < bounds_.bottom(); ++y)
            for (int x = bounds_.x; x < bounds_.right(); ++x) b.set(x, y, Cell{ch, 1, Color::Default(), Color::Default(), TextStyle::None});
    }

private:
    Orientation orientation_;
    Line line_;
};

} // namespace tui
