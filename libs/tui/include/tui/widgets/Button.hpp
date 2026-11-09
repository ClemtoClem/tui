/**
 * @file Button.hpp
 * @brief Bouton focusable avec callback on_click, activable au clavier ou à la souris.
 */

#pragma once

#include "../Buffer.hpp"
#include "../core/Text.hpp"
#include "../core/Theme.hpp"
#include "../widget/Widget.hpp"

#include <functional>

namespace tui {

class Button : public Widget {
public:
    explicit Button(std::string label = "") : label_(std::move(label)) {}

    void set_label(std::string label) { label_ = std::move(label); need_repaint(); }
    [[nodiscard]] const std::string& label() const { return label_; }

    void set_on_click(std::function<void()> cb) { on_click_ = std::move(cb); }

    [[nodiscard]] Size measure(const Constraints& c) const override {
        auto decoded = TextHelper::decode_utf8(label_);
        // Stylé (CSS) : le libellé seul, padding et bordure viennent du style.
        int w = TextHelper::display_width(decoded) + (styled() ? 0 : 4); // "[ label ]"
        return c.clamp({w, 1});
    }

    void paint(Buffer& buffer) const override {
        if (bounds_.height <= 0) return;
        if (styled()) {
            paint_styled(buffer);
            return;
        }
        TextStyle style = TextStyle::None;
        if (focused_) style = style | Theme::dark().focus_style();
        if (pressed_) style = style | TextStyle::Reverse;

        auto decoded = TextHelper::decode_utf8("[ " + label_ + " ]");
        auto truncated = TextHelper::truncate_to_width(decoded, bounds_.width);
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
            activate();
            return true;
        }
        return false;
    }

    bool on_mouse(const MouseEvent& e) override {
        if (e.button != MouseEvent::Button::Left) return false;
        if (e.action == MouseEvent::Action::Press) {
            pressed_ = true;
            need_repaint();
            return true;
        }
        if (e.action == MouseEvent::Action::Release) {
            bool was_pressed = pressed_;
            pressed_ = false;
            need_repaint();
            // Relâché hors du bouton (après capture) : annule le clic.
            if (was_pressed && outer_bounds().contains({e.x, e.y})) activate();
            return true;
        }
        return false;
    }

    [[nodiscard]] bool pressed() const { return pressed_; }

private:
    void activate() { if (on_click_) on_click_(); }

    void paint_styled(Buffer& buffer) const {
        TextStyle st = pressed_ ? TextStyle::Reverse : TextStyle::None;
        Color bg = Color::Default();
        // Toute la boîte intérieure prend le fond du style (via la passe
        // d'héritage) : on n'écrit que des cellules « par défaut ».
        for (int y = bounds_.y; y < bounds_.bottom(); ++y)
            for (int x = bounds_.x; x < bounds_.right(); ++x) buffer.set(x, y, Cell{U' ', 1, Color::Default(), bg, st});
        auto decoded = TextHelper::truncate_to_width(TextHelper::decode_utf8(label_), bounds_.width, U"…");
        int w = TextHelper::display_width(decoded);
        std::string align = style().extra_value("text-align");
        int x = align == "left" ? bounds_.x : align == "right" ? bounds_.right() - w : bounds_.x + (bounds_.width - w) / 2;
        int y = bounds_.y + (bounds_.height - 1) / 2;
        for (char32_t ch : decoded) {
            buffer.set(x, y, Cell{ch, 1, Color::Default(), bg, st});
            ++x;
        }
    }

    std::string label_;
    std::function<void()> on_click_;
    bool focused_ = false;
    bool pressed_ = false;
};

} // namespace tui
