/**
 * @file Tooltip.hpp
 * @brief Popup courte ancrée à un point, auto-positionnée pour rester à l'écran.
 */

#pragma once

#include "../Buffer.hpp"
#include "../core/Text.hpp"
#include "../widget/BoxFrame.hpp"
#include "../widget/Widget.hpp"

#include <algorithm>
#include <string>

namespace tui {

class Tooltip : public Widget {
public:
    void set_text(std::string text) { text_ = std::move(text); need_repaint(); }
    void set_anchor(Point anchor) { anchor_ = anchor; need_repaint(); }

    void show() { showing_ = true; need_repaint(); }
    void hide() { showing_ = false; need_repaint(); }
    [[nodiscard]] bool visible() const override { return showing_; }

    [[nodiscard]] Size measure(const Constraints& c) const override {
        auto decoded = TextHelper::decode_utf8(text_);
        return c.clamp({TextHelper::display_width(decoded) + 2, 3});
    }

    /// `screen_bounds` est la zone totale disponible (l'écran), utilisée
    /// pour maintenir le tooltip visible - pas la position propre du
    /// widget dans un flux de mise en page classique.
    void arrange(Rect screen_bounds) override {
        Size s = measure(Constraints::loose({screen_bounds.width, screen_bounds.height}));
        int x = anchor_.x;
        int y = anchor_.y + 1; // sous l'ancre par défaut
        x = std::clamp(x, screen_bounds.x, std::max(screen_bounds.right() - s.width, screen_bounds.x));
        y = std::clamp(y, screen_bounds.y, std::max(screen_bounds.bottom() - s.height, screen_bounds.y));
        bounds_ = Rect{x, y, s.width, s.height};
    }

    void paint(Buffer& buffer) const override {
        if (!showing_) return;
        draw_box_frame(buffer, bounds_);
        auto decoded = TextHelper::decode_utf8(text_);
        auto truncated = TextHelper::truncate_to_width(decoded, std::max(bounds_.width - 2, 0));
        int x = bounds_.x + 1;
        for (char32_t ch : truncated) {
            buffer.set(x, bounds_.y + 1, Cell{ch, 1, Color::Default(), Color::Default(), TextStyle::None});
            ++x;
        }
    }

private:
    std::string text_;
    Point anchor_;
    bool showing_ = false;
};

} // namespace tui
