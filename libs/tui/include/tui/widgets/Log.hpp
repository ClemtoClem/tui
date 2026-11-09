/**
 * @file Log.hpp
 * @brief Journal défilant (Textual « Log ») : ajout de lignes, limite de
 * taille, défilement automatique quand on est en bas, molette et clavier.
 */

#pragma once

#include "../widget/Paint.hpp"
#include "../widget/Scrollbar.hpp"
#include "../widget/Widget.hpp"

#include <algorithm>
#include <deque>
#include <string>

namespace tui {

class Log : public Widget {
public:
    /// Ajoute du texte ; chaque « \n » commence une nouvelle ligne.
    void write(const std::string& text) {
        size_t start = 0;
        bool first = true;
        while (true) {
            size_t e = text.find('\n', start);
            std::string part = text.substr(start, e == std::string::npos ? std::string::npos : e - start);
            if (first && continue_line_ && !lines_.empty()) lines_.back() += part;
            else lines_.push_back(part);
            first = false;
            if (e == std::string::npos) {
                continue_line_ = true;
                break;
            }
            start = e + 1;
            if (start == text.size()) {
                continue_line_ = false;
                break;
            }
        }
        trim();
        if (auto_scroll_ && follow_) scroll_ = max_scroll();
        need_repaint();
    }
    void write_line(const std::string& line) {
        continue_line_ = false;
        write(line + "\n");
    }
    void clear() {
        lines_.clear();
        scroll_ = 0;
        continue_line_ = false;
        need_repaint();
    }
    void set_max_lines(size_t n) { max_lines_ = n; trim(); }
    void set_auto_scroll(bool on) { auto_scroll_ = on; }
    [[nodiscard]] const std::deque<std::string>& lines() const { return lines_; }
    [[nodiscard]] int scroll() const { return scroll_; }

    [[nodiscard]] Size measure(const Constraints& c) const override { return c.clamp({c.max_width, c.max_height}); }

    void arrange(Rect r) override {
        bool at_end = scroll_ >= max_scroll();
        bounds_ = r;
        if (at_end && auto_scroll_) scroll_ = max_scroll();
        scroll_ = std::clamp(scroll_, 0, max_scroll());
    }

    void paint(Buffer& b) const override {
        if (bounds_.empty()) return;
        bool bar = static_cast<int>(lines_.size()) > bounds_.height && bounds_.width > 1;
        int w = bar ? bounds_.width - 1 : bounds_.width;
        for (int row = 0; row < bounds_.height; ++row) {
            size_t idx = static_cast<size_t>(scroll_ + row);
            if (idx >= lines_.size()) break;
            paint::text(b, bounds_.x, bounds_.y + row, w, lines_[idx]);
        }
        if (bar)
            draw_vertical_scrollbar(b, Rect{bounds_.right() - 1, bounds_.y, 1, bounds_.height},
                                    static_cast<int>(lines_.size()), bounds_.height, scroll_, Color::Default(), Color::Default());
    }

    [[nodiscard]] bool focusable() const override { return true; }

    bool on_key(const KeyEvent& e) override {
        int page = std::max(1, bounds_.height - 1);
        switch (e.key) {
        case Key::Up: scroll_to(scroll_ - 1); return true;
        case Key::Down: scroll_to(scroll_ + 1); return true;
        case Key::PageUp: scroll_to(scroll_ - page); return true;
        case Key::PageDown: scroll_to(scroll_ + page); return true;
        case Key::Home: scroll_to(0); return true;
        case Key::End: scroll_to(max_scroll()); return true;
        default: return false;
        }
    }

    bool on_mouse(const MouseEvent& e) override {
        if (e.button == MouseEvent::Button::WheelUp) { scroll_to(scroll_ - 3); return true; }
        if (e.button == MouseEvent::Button::WheelDown) { scroll_to(scroll_ + 3); return true; }
        return false;
    }

    void scroll_to(int s) {
        scroll_ = std::clamp(s, 0, max_scroll());
        follow_ = scroll_ >= max_scroll();
        need_repaint();
    }
    [[nodiscard]] int max_scroll() const { return std::max(0, static_cast<int>(lines_.size()) - std::max(bounds_.height, 1)); }

private:
    void trim() {
        if (max_lines_ == 0) return;
        while (lines_.size() > max_lines_) {
            lines_.pop_front();
            if (scroll_ > 0) --scroll_;
        }
    }

    std::deque<std::string> lines_;
    size_t max_lines_ = 10000;
    int scroll_ = 0;
    bool auto_scroll_ = true;
    bool follow_ = true;
    bool continue_line_ = false;
};

} // namespace tui
