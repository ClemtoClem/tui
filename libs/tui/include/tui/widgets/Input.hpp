/**
 * @file Input.hpp
 * @brief Champ de texte sur une ligne : curseur, sélection, presse-papiers.
 */

#pragma once

#include "../Buffer.hpp"
#include "../core/Clipboard.hpp"
#include "../core/Text.hpp"
#include "../widget/Widget.hpp"

#include <algorithm>
#include <functional>

namespace tui {

class Input : public Widget {
public:
    void set_text(const std::string& text) {
        text_ = TextHelper::decode_utf8(text);
        cursor_ = std::min(cursor_, text_.size());
        anchor_ = cursor_;
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
    [[nodiscard]] bool has_selection() const { return anchor_ != cursor_; }
    [[nodiscard]] size_t selection_start() const { return std::min(anchor_, cursor_); }
    [[nodiscard]] size_t selection_end() const { return std::max(anchor_, cursor_); }
    [[nodiscard]] std::string selected_text() const {
        if (!has_selection()) return "";
        return TextHelper::encode_utf8(text_.substr(selection_start(), selection_end() - selection_start()));
    }
    void copy_selection() {
        std::string s = selected_text();
        if (!s.empty()) Clipboard::instance().set(std::move(s));
    }
    void cut_selection() {
        if (!has_selection()) return;
        std::string s = selected_text();
        Clipboard::instance().set(s);
        delete_selection();
        changed();
    }
    void paste() {
        const std::string& clip = Clipboard::instance().get();
        if (clip.empty()) return;
        auto decoded = TextHelper::decode_utf8(clip);
        std::u32string cleaned;
        cleaned.reserve(decoded.size());
        for (char32_t c : decoded)
            if (c != U'\n' && c != U'\r') cleaned.push_back(c);
        if (cleaned.empty()) return;
        if (has_selection()) delete_selection();
        text_.insert(text_.begin() + static_cast<std::ptrdiff_t>(cursor_), cleaned.begin(), cleaned.end());
        cursor_ += cleaned.size();
        anchor_ = cursor_;
        changed();
    }
    void select_all() {
        anchor_ = 0;
        cursor_ = text_.size();
        need_repaint();
    }

    [[nodiscard]] Size measure(const Constraints& c) const override {
        return c.clamp({std::max(c.min_width, 10), 1});
    }

    void paint(Buffer& buffer) const override {
        if (bounds_.height <= 0 || bounds_.width <= 0) return;
        int offset = compute_scroll_offset(bounds_.width);

        bool show_placeholder = text_.empty() && !placeholder_.empty();
        std::u32string display = show_placeholder ? TextHelper::decode_utf8(placeholder_) : text_;
        TextStyle base_style = show_placeholder ? TextStyle::Dim : TextStyle::None;

        size_t sel_start = has_selection() ? selection_start() : 0;
        size_t sel_end = has_selection() ? selection_end() : 0;

        for (int i = 0; i < bounds_.width; ++i) {
            size_t idx = static_cast<size_t>(offset + i);
            char32_t ch = idx < display.size() ? display[idx] : U' ';
            TextStyle style = base_style;
            if (!show_placeholder && focused_) {
                if (has_selection() && idx >= sel_start && idx < sel_end) style = style | TextStyle::Reverse;
                else if (!has_selection() && idx == cursor_) style = style | TextStyle::Reverse;
            }
            buffer.set(bounds_.x + i, bounds_.y, Cell{ch, 1, Color::Default(), Color::Default(), style});
        }
    }

    [[nodiscard]] bool focusable() const override { return true; }
    void on_focus() override { focused_ = true; need_repaint(); }
    void on_blur() override { focused_ = false; need_repaint(); }

    /// Clic : place le curseur ; glisser étend la sélection (comme Textual).
    bool on_mouse(const MouseEvent& e) override {
        if (e.button != MouseEvent::Button::Left) return false;
        if (e.action != MouseEvent::Action::Press && e.action != MouseEvent::Action::Drag) return false;
        int col = std::clamp(e.x - bounds_.x, 0, std::max(bounds_.width - 1, 0));
        size_t pos = std::min(text_.size(), static_cast<size_t>(std::max(0, scroll_offset_ + col)));
        if (e.action == MouseEvent::Action::Press && !e.shift) anchor_ = pos;
        cursor_ = pos;
        need_repaint();
        return true;
    }

    bool on_paste(const std::string& text) override {
        auto decoded = TextHelper::decode_utf8(text);
        std::u32string cleaned;
        for (char32_t c : decoded)
            if (c != U'\n' && c != U'\r') cleaned.push_back(c);
        if (cleaned.empty()) return false;
        if (has_selection()) delete_selection();
        text_.insert(text_.begin() + static_cast<std::ptrdiff_t>(cursor_), cleaned.begin(), cleaned.end());
        cursor_ += cleaned.size();
        anchor_ = cursor_;
        changed();
        return true;
    }

    bool on_key(const KeyEvent& e) override {
        if (e.ctrl) {
            char32_t c = (e.codepoint >= U'A' && e.codepoint <= U'Z') ? e.codepoint + 32 : e.codepoint;
            switch (c) {
                case U'c': copy_selection(); return true;
                case U'x': cut_selection(); return true;
                case U'v': paste(); return true;
                case U'a': select_all(); return true;
                default: return false;
            }
        }
        switch (e.key) {
            case Key::Left:
                move_cursor(cursor_ > 0 ? cursor_ - 1 : cursor_, e.shift);
                return true;
            case Key::Right:
                move_cursor(cursor_ < text_.size() ? cursor_ + 1 : cursor_, e.shift);
                return true;
            case Key::Home:
                move_cursor(0, e.shift);
                return true;
            case Key::End:
                move_cursor(text_.size(), e.shift);
                return true;
            case Key::Backspace:
                if (has_selection()) { delete_selection(); changed(); return true; }
                if (cursor_ > 0) {
                    text_.erase(cursor_ - 1, 1);
                    --cursor_;
                    anchor_ = cursor_;
                    changed();
                }
                return true;
            case Key::Delete:
                if (has_selection()) { delete_selection(); changed(); return true; }
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
                    if (has_selection()) delete_selection();
                    text_.insert(text_.begin() + static_cast<std::ptrdiff_t>(cursor_), e.codepoint);
                    ++cursor_;
                    anchor_ = cursor_;
                    changed();
                }
                return true;
            default:
                return false;
        }
    }

private:
    void delete_selection() {
        if (!has_selection()) return;
        size_t start = selection_start();
        text_.erase(start, selection_end() - start);
        cursor_ = start;
        anchor_ = start;
    }

    void move_cursor(size_t pos, bool extend) {
        cursor_ = pos;
        if (!extend) anchor_ = cursor_;
        need_repaint();
    }

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
    size_t anchor_ = 0;
    std::string placeholder_;
    std::function<bool(char32_t)> validator_;
    std::function<void(const std::string&)> on_change_;
    std::function<void(const std::string&)> on_submit_;
    bool focused_ = false;
    mutable int scroll_offset_ = 0;
};

} // namespace tui
