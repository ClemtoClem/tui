/**
 * @file SplitPane.hpp
 * @brief Deux enfants séparés par un diviseur déplaçable (souris) ou ajustable (clavier).
 */

#pragma once

#include "../../Buffer.hpp"
#include "../Widget.hpp"

#include <algorithm>

namespace tui {

class SplitPane : public Widget {
public:
    enum class Axis { LeftRight, TopBottom };

    explicit SplitPane(Axis axis = Axis::LeftRight) : axis_(axis) {}

    void set_first(std::shared_ptr<Widget> w) {
        first_ = std::move(w);
        if (first_) first_->set_parent(this);
        need_repaint();
    }
    void set_second(std::shared_ptr<Widget> w) {
        second_ = std::move(w);
        if (second_) second_->set_parent(this);
        need_repaint();
    }

    void set_ratio(double r) { ratio_ = std::clamp(r, 0.0, 1.0); need_repaint(); }
    [[nodiscard]] double ratio() const { return ratio_; }

    [[nodiscard]] Size measure(const Constraints& c) const override {
        return c.clamp({c.max_width, c.max_height});
    }

    void arrange(Rect final_rect) override {
        bounds_ = final_rect;
        int total = (axis_ == Axis::LeftRight) ? final_rect.width : final_rect.height;
        constexpr int kDividerSize = 1;
        int first_size = std::clamp(static_cast<int>(total * ratio_), 0, std::max(total - kDividerSize, 0));
        int second_size = std::max(total - first_size - kDividerSize, 0);

        if (axis_ == Axis::LeftRight) {
            divider_rect_ = Rect{final_rect.x + first_size, final_rect.y, kDividerSize, final_rect.height};
            if (first_) first_->arrange(Rect{final_rect.x, final_rect.y, first_size, final_rect.height});
            if (second_) second_->arrange(Rect{final_rect.x + first_size + kDividerSize, final_rect.y, second_size, final_rect.height});
        } else {
            divider_rect_ = Rect{final_rect.x, final_rect.y + first_size, final_rect.width, kDividerSize};
            if (first_) first_->arrange(Rect{final_rect.x, final_rect.y, final_rect.width, first_size});
            if (second_) second_->arrange(Rect{final_rect.x, final_rect.y + first_size + kDividerSize, final_rect.width, second_size});
        }
    }

    void paint(Buffer& buffer) const override {
        ClipGuard clip(buffer, bounds_);
        if (first_ && first_->visible()) first_->paint(buffer);
        if (second_ && second_->visible()) second_->paint(buffer);
        draw_divider(buffer);
    }

    [[nodiscard]] bool focusable() const override { return true; } // le diviseur reçoit le focus clavier

    bool on_key(const KeyEvent& e) override {
        int total = (axis_ == Axis::LeftRight) ? bounds_.width : bounds_.height;
        if (total <= 0) return false;
        bool decrease = (axis_ == Axis::LeftRight) ? (e.key == Key::Left) : (e.key == Key::Up);
        bool increase = (axis_ == Axis::LeftRight) ? (e.key == Key::Right) : (e.key == Key::Down);
        if (!decrease && !increase) return false;
        adjust_by_cells(decrease ? -1 : 1, total);
        return true;
    }

    bool on_mouse(const MouseEvent& e) override {
        if (e.action != MouseEvent::Action::Drag && e.action != MouseEvent::Action::Press) return false;
        int total = (axis_ == Axis::LeftRight) ? bounds_.width : bounds_.height;
        if (total <= 0) return false;
        int pos = (axis_ == Axis::LeftRight) ? (e.x - bounds_.x) : (e.y - bounds_.y);
        set_ratio(static_cast<double>(pos) / total);
        return true;
    }

    [[nodiscard]] std::vector<std::shared_ptr<Widget>> children() const override {
        std::vector<std::shared_ptr<Widget>> result;
        if (first_) result.push_back(first_);
        if (second_) result.push_back(second_);
        return result;
    }

private:
    void adjust_by_cells(int delta, int total) {
        int current_cells = static_cast<int>(total * ratio_);
        set_ratio(static_cast<double>(current_cells + delta) / total);
    }

    void draw_divider(Buffer& buffer) const {
        char32_t ch = (axis_ == Axis::LeftRight) ? U'│' : U'─';
        Cell cell{ch, 1, Color::Default(), Color::Default(), TextStyle::None};
        for (int y = divider_rect_.y; y < divider_rect_.bottom(); ++y) {
            for (int x = divider_rect_.x; x < divider_rect_.right(); ++x) {
                buffer.set(x, y, cell);
            }
        }
    }

    Axis axis_;
    double ratio_ = 0.5;
    std::shared_ptr<Widget> first_;
    std::shared_ptr<Widget> second_;
    Rect divider_rect_;
};

} // namespace tui
