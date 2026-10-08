/**
 * @file CheckboxList.hpp
 * @brief Liste de cases à cocher naviguable au clavier (une par ligne).
 */

#pragma once

#include "../Buffer.hpp"
#include "../core/Text.hpp"
#include "../widget/Widget.hpp"

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace tui {

class CheckboxList : public Widget {
public:
    struct Item {
        std::string label;
        bool checked = false;
    };

    void set_items(std::vector<Item> items) {
        items_ = std::move(items);
        if (selected_ >= items_.size()) selected_ = items_.empty() ? 0 : items_.size() - 1;
        need_repaint();
    }
    [[nodiscard]] const std::vector<Item>& items() const { return items_; }
    void set_on_change(std::function<void(size_t index, bool checked)> cb) { on_change_ = std::move(cb); }

    [[nodiscard]] Size measure(const Constraints& c) const override {
        return c.clamp({c.max_width, static_cast<int>(items_.size())});
    }

    void paint(Buffer& buffer) const override {
        for (size_t i = 0; i < items_.size() && static_cast<int>(i) < bounds_.height; ++i) {
            std::string display = std::string("[") + (items_[i].checked ? "x" : " ") + "] " + items_[i].label;
            auto decoded = TextHelper::decode_utf8(display);
            auto truncated = TextHelper::truncate_to_width(decoded, bounds_.width);
            TextStyle style = (focused_ && i == selected_) ? TextStyle::Reverse : TextStyle::None;
            int x = bounds_.x;
            for (char32_t ch : truncated) {
                buffer.set(x, bounds_.y + static_cast<int>(i), Cell{ch, 1, Color::Default(), Color::Default(), style});
                ++x;
            }
        }
    }

    [[nodiscard]] bool focusable() const override { return !items_.empty(); }
    void on_focus() override { focused_ = true; need_repaint(); }
    void on_blur() override { focused_ = false; need_repaint(); }

    bool on_key(const KeyEvent& e) override {
        if (items_.empty()) return false;
        if (e.key == Key::Up) {
            if (selected_ > 0) --selected_;
            need_repaint();
            return true;
        }
        if (e.key == Key::Down) {
            if (selected_ + 1 < items_.size()) ++selected_;
            need_repaint();
            return true;
        }
        if (e.key == Key::Enter || (e.key == Key::Char && e.codepoint == U' ')) {
            items_[selected_].checked = !items_[selected_].checked;
            need_repaint();
            if (on_change_) on_change_(selected_, items_[selected_].checked);
            return true;
        }
        return false;
    }

private:
    std::vector<Item> items_;
    size_t selected_ = 0;
    bool focused_ = false;
    std::function<void(size_t, bool)> on_change_;
};

} // namespace tui
