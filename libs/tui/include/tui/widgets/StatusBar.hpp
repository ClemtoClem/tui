/**
 * @file StatusBar.hpp
 * @brief Barre d'une ligne avec segments gauche/centre/droite (ex: barre d'état en bas d'écran).
 */

#pragma once

#include "../Buffer.hpp"
#include "../core/Text.hpp"
#include "../widget/Widget.hpp"

#include <algorithm>

namespace tui {

class StatusBar : public Widget {
public:
    void set_left(std::string text) { left_ = std::move(text); need_repaint(); }
    void set_center(std::string text) { center_ = std::move(text); need_repaint(); }
    void set_right(std::string text) { right_ = std::move(text); need_repaint(); }

    [[nodiscard]] Size measure(const Constraints& c) const override {
        return c.clamp({c.max_width, 1});
    }

    void paint(Buffer& buffer) const override {
        if (bounds_.height <= 0) return;
        for (int x = 0; x < bounds_.width; ++x) {
            buffer.set(bounds_.x + x, bounds_.y, Cell{U' ', 1, Color::Default(), Color::Default(), TextStyle::Reverse});
        }
        paint_segment(buffer, left_, 0);
        paint_segment(buffer, center_, std::max((bounds_.width - width_of(center_)) / 2, 0));
        paint_segment(buffer, right_, std::max(bounds_.width - width_of(right_), 0));
    }

private:
    [[nodiscard]] static int width_of(const std::string& text) {
        return TextHelper::display_width(TextHelper::decode_utf8(text));
    }

    void paint_segment(Buffer& buffer, const std::string& text, int x_offset) const {
        auto decoded = TextHelper::decode_utf8(text);
        auto truncated = TextHelper::truncate_to_width(decoded, std::max(bounds_.width - x_offset, 0));
        int x = bounds_.x + x_offset;
        for (char32_t ch : truncated) {
            buffer.set(x, bounds_.y, Cell{ch, 1, Color::Default(), Color::Default(), TextStyle::Reverse});
            ++x;
        }
    }

    std::string left_;
    std::string center_;
    std::string right_;
};

} // namespace tui
