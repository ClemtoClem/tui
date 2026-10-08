/**
 * @file RadioSet.hpp
 * @brief Choix unique parmi plusieurs options, naviguable au clavier.
 */

#pragma once

#include "../Buffer.hpp"
#include "../core/Text.hpp"
#include "../widget/Widget.hpp"

#include <functional>
#include <string>
#include <vector>

namespace tui {

class RadioSet : public Widget {
public:
    void set_options(std::vector<std::string> options) {
        options_ = std::move(options);
        if (selected_index_ >= options_.size()) selected_index_ = options_.empty() ? 0 : options_.size() - 1;
        if (cursor_ >= options_.size()) cursor_ = selected_index_;
        need_repaint();
    }

    void set_selected_index(size_t i) {
        if (i >= options_.size() || i == selected_index_) return;
        selected_index_ = i;
        need_repaint();
        if (on_change_) on_change_(i);
    }
    [[nodiscard]] size_t selected_index() const { return selected_index_; }
    void set_on_change(std::function<void(size_t)> cb) { on_change_ = std::move(cb); }

    [[nodiscard]] Size measure(const Constraints& c) const override {
        return c.clamp({c.max_width, static_cast<int>(options_.size())});
    }

    void paint(Buffer& buffer) const override {
        for (size_t i = 0; i < options_.size() && static_cast<int>(i) < bounds_.height; ++i) {
            std::string display = std::string("(") + (i == selected_index_ ? "*" : " ") + ") " + options_[i];
            auto decoded = TextHelper::decode_utf8(display);
            auto truncated = TextHelper::truncate_to_width(decoded, bounds_.width);
            TextStyle style = (focused_ && i == cursor_) ? TextStyle::Reverse : TextStyle::None;
            int x = bounds_.x;
            for (char32_t ch : truncated) {
                buffer.set(x, bounds_.y + static_cast<int>(i), Cell{ch, 1, Color::Default(), Color::Default(), style});
                ++x;
            }
        }
    }

    [[nodiscard]] bool focusable() const override { return !options_.empty(); }
    void on_focus() override { focused_ = true; need_repaint(); }
    void on_blur() override { focused_ = false; need_repaint(); }

    bool on_key(const KeyEvent& e) override {
        if (options_.empty()) return false;
        if (e.key == Key::Up) {
            if (cursor_ > 0) --cursor_;
            need_repaint();
            return true;
        }
        if (e.key == Key::Down) {
            if (cursor_ + 1 < options_.size()) ++cursor_;
            need_repaint();
            return true;
        }
        if (e.key == Key::Enter || (e.key == Key::Char && e.codepoint == U' ')) {
            set_selected_index(cursor_);
            return true;
        }
        return false;
    }

private:
    std::vector<std::string> options_;
    size_t selected_index_ = 0; // le choix retenu
    size_t cursor_ = 0;         // la ligne survolée au clavier (peut différer avant validation)
    bool focused_ = false;
    std::function<void(size_t)> on_change_;
};

} // namespace tui
