/**
 * @file Checkbox.hpp
 * @brief Case à cocher focusable : [x]/[ ] + label.
 */

#pragma once

#include "../Buffer.hpp"
#include "../core/Text.hpp"
#include "../widget/Widget.hpp"

#include <functional>

namespace tui {

class Checkbox : public Widget {
public:
    explicit Checkbox(std::string label = "") : label_(std::move(label)) {}

    void set_label(std::string label) { label_ = std::move(label); need_repaint(); }
    void set_checked(bool checked) {
        if (checked_ == checked) return;
        checked_ = checked;
        need_repaint();
        if (on_change_) on_change_(checked_);
    }
    [[nodiscard]] bool checked() const { return checked_; }
    void set_on_change(std::function<void(bool)> cb) { on_change_ = std::move(cb); }

    [[nodiscard]] Size measure(const Constraints& c) const override {
        auto decoded = TextHelper::decode_utf8(label_);
        return c.clamp({4 + TextHelper::display_width(decoded), 1});
    }

    void paint(Buffer& buffer) const override {
        if (bounds_.height <= 0) return;
        std::string display = std::string("[") + (checked_ ? "x" : " ") + "] " + label_;
        auto decoded = TextHelper::decode_utf8(display);
        auto truncated = TextHelper::truncate_to_width(decoded, bounds_.width);
        TextStyle style = focused_ ? TextStyle::Reverse : TextStyle::None;
        int x = bounds_.x;
        for (char32_t ch : truncated) {
            buffer.set(x, bounds_.y, Cell{ch, 1, Color::Default(), Color::Default(), style});
            ++x;
        }
    }

    [[nodiscard]] bool focusable() const override { return true; }
    void on_focus() override { focused_ = true; need_repaint(); }
    void on_blur() override { focused_ = false; need_repaint(); }

    bool on_key(const KeyEvent& e) override {
        if (e.key == Key::Enter || (e.key == Key::Char && e.codepoint == U' ')) {
            set_checked(!checked_);
            return true;
        }
        return false;
    }

    bool on_mouse(const MouseEvent& e) override {
        if (e.button == MouseEvent::Button::Left && e.action == MouseEvent::Action::Press) {
            set_checked(!checked_);
            return true;
        }
        return false;
    }

private:
    std::string label_;
    bool checked_ = false;
    bool focused_ = false;
    std::function<void(bool)> on_change_;
};

} // namespace tui
