/**
 * @file Breadcrumb.hpp
 * @brief Fil d'Ariane cliquable ("Home › Documents › file.txt").
 */

#pragma once

#include "../Buffer.hpp"
#include "../core/Text.hpp"
#include "../widget/Widget.hpp"

#include <functional>
#include <string>
#include <vector>

namespace tui {

class Breadcrumb : public Widget {
public:
    void set_segments(std::vector<std::string> segments) { segments_ = std::move(segments); need_repaint(); }
    void set_on_activate(std::function<void(size_t)> cb) { on_activate_ = std::move(cb); }

    [[nodiscard]] Size measure(const Constraints& c) const override { return c.clamp({c.max_width, 1}); }

    void arrange(Rect final_rect) override {
        bounds_ = final_rect;
        segment_offsets_.clear();
        int x = 0;
        for (size_t i = 0; i < segments_.size(); ++i) {
            segment_offsets_.push_back(x);
            x += static_cast<int>(TextHelper::display_width(TextHelper::decode_utf8(segments_[i])));
            if (i + 1 < segments_.size()) x += 3; // " › "
        }
    }

    void paint(Buffer& buffer) const override {
        std::string display;
        for (size_t i = 0; i < segments_.size(); ++i) {
            if (i > 0) display += " › "; // ›
            display += segments_[i];
        }
        auto decoded = TextHelper::decode_utf8(display);
        auto truncated = TextHelper::truncate_to_width(decoded, bounds_.width, U"…");
        int x = bounds_.x;
        for (char32_t ch : truncated) {
            buffer.set(x, bounds_.y, Cell{ch, 1, Color::Default(), Color::Default(), TextStyle::None});
            ++x;
        }
    }

    bool on_mouse(const MouseEvent& e) override {
        if (e.button != MouseEvent::Button::Left || e.action != MouseEvent::Action::Press) return false;
        int col = e.x - bounds_.x;
        for (long i = static_cast<long>(segment_offsets_.size()) - 1; i >= 0; --i) {
            if (col >= segment_offsets_[static_cast<size_t>(i)]) {
                if (on_activate_) on_activate_(static_cast<size_t>(i));
                return true;
            }
        }
        return false;
    }

private:
    std::vector<std::string> segments_;
    std::vector<int> segment_offsets_;
    std::function<void(size_t)> on_activate_;
};

} // namespace tui
