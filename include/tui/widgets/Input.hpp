/**
 * @file Input.hpp
 * @brief Champ de texte sur une ligne : curseur en index de codepoint, défilement horizontal.
 */

#pragma once

#include "../Buffer.hpp"
#include "../core/Text.hpp"
#include "../widget/Widget.hpp"

#include <functional>

namespace tui {

class Input : public Widget {
public:
    void set_text(const std::string& text) {
        text_ = TextHelper::decode_utf8(text);
        cursor_ = std::min(cursor_, text_.size());
        need_repaint();
    }
    [[nodiscard]] std::string text() const { return TextHelper::encode_utf8(text_); }

    void set_placeholder(std::string placeholder) { placeholder_ = std::move(placeholder); need_repaint(); }

    /// Retourne false pour rejeter un codepoint tapé (ex: NumberInput
    /// n'accepte que les chiffres/signe/point).
    void set_validator(std::function<bool(char32_t)> validator) { validator_ = std::move(validator); }

    void set_on_change(std::function<void(const std::string&)> cb) { on_change_ = std::move(cb); }
    void set_on_submit(std::function<void(const std::string&)> cb) { on_submit_ = std::move(cb); }

    [[nodiscard]] size_t cursor() const { return cursor_; }

    [[nodiscard]] Size measure(const Constraints& c) const override {
        return c.clamp({std::max(c.min_width, 10), 1});
    }

    void paint(Buffer& buffer) const override {
        if (bounds_.height <= 0 || bounds_.width <= 0) return;
        int offset = compute_scroll_offset(bounds_.width);

        bool show_placeholder = text_.empty() && !placeholder_.empty();
        std::u32string display = show_placeholder ? TextHelper::decode_utf8(placeholder_) : text_;
        TextStyle base_style = show_placeholder ? TextStyle::Dim : TextStyle::None;

        for (int i = 0; i < bounds_.width; ++i) {
            size_t idx = static_cast<size_t>(offset + i);
            char32_t ch = idx < display.size() ? display[idx] : U' ';
            TextStyle style = base_style;
            if (!show_placeholder && focused_ && idx == cursor_) style = style | TextStyle::Reverse;
            buffer.set(bounds_.x + i, bounds_.y, Cell{ch, 1, Color::Default(), Color::Default(), style});
        }
    }

    [[nodiscard]] bool focusable() const override { return true; }
    void on_focus() override { focused_ = true; need_repaint(); }
    void on_blur() override { focused_ = false; need_repaint(); }

    bool on_key(const KeyEvent& e) override {
        switch (e.key) {
            case Key::Left:
                if (cursor_ > 0) --cursor_;
                need_repaint();
                return true;
            case Key::Right:
                if (cursor_ < text_.size()) ++cursor_;
                need_repaint();
                return true;
            case Key::Home:
                cursor_ = 0;
                need_repaint();
                return true;
            case Key::End:
                cursor_ = text_.size();
                need_repaint();
                return true;
            case Key::Backspace:
                if (cursor_ > 0) {
                    text_.erase(cursor_ - 1, 1);
                    --cursor_;
                    changed();
                }
                return true;
            case Key::Delete:
                if (cursor_ < text_.size()) {
                    text_.erase(cursor_, 1);
                    changed();
                }
                return true;
            case Key::Enter:
                if (on_submit_) on_submit_(text());
                return true;
            case Key::Char:
                if (!validator_ || validator_(e.codepoint)) {
                    text_.insert(text_.begin() + static_cast<std::ptrdiff_t>(cursor_), e.codepoint);
                    ++cursor_;
                    changed();
                }
                return true;
            default:
                return false;
        }
    }

private:
    void changed() {
        need_repaint();
        if (on_change_) on_change_(text());
    }

    [[nodiscard]] int compute_scroll_offset(int width) const {
        if (width <= 0) return 0;
        if (static_cast<int>(cursor_) < scroll_offset_) scroll_offset_ = static_cast<int>(cursor_);
        if (static_cast<int>(cursor_) >= scroll_offset_ + width) scroll_offset_ = static_cast<int>(cursor_) - width + 1;
        return scroll_offset_;
    }

    std::u32string text_;
    size_t cursor_ = 0;
    std::string placeholder_;
    std::function<bool(char32_t)> validator_;
    std::function<void(const std::string&)> on_change_;
    std::function<void(const std::string&)> on_submit_;
    bool focused_ = false;
    mutable int scroll_offset_ = 0;
};

} // namespace tui
