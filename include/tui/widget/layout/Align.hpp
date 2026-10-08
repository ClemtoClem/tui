/**
 * @file Align.hpp
 * @brief Décorateur à un seul enfant : le positionne dans l'espace disponible (9 combinaisons).
 */

#pragma once

#include "../Container.hpp"

namespace tui {

class Align : public Container {
public:
    explicit Align(HAlign h = HAlign::Center, VAlign v = VAlign::Center) : h_(h), v_(v) {}

    void set_child(std::shared_ptr<Widget> child) {
        children_.clear();
        if (child) add_child(std::move(child));
    }

    void set_alignment(HAlign h, VAlign v) { h_ = h; v_ = v; need_repaint(); }

    [[nodiscard]] Size measure(const Constraints& c) const override {
        if (child_count() == 0) return c.clamp({0, 0});
        return c.clamp(child_at(0).widget->measure(Constraints::loose({c.max_width, c.max_height})));
    }

    void arrange(Rect final_rect) override {
        bounds_ = final_rect;
        if (child_count() == 0) return;
        auto& child = child_at(0).widget;
        Size natural = child->measure(Constraints::loose({final_rect.width, final_rect.height}));

        int w = (h_ == HAlign::Stretch) ? final_rect.width : std::min(natural.width, final_rect.width);
        int h = (v_ == VAlign::Stretch) ? final_rect.height : std::min(natural.height, final_rect.height);

        int x = final_rect.x;
        switch (h_) {
            case HAlign::Start: case HAlign::Stretch: x = final_rect.x; break;
            case HAlign::Center: x = final_rect.x + (final_rect.width - w) / 2; break;
            case HAlign::End: x = final_rect.x + final_rect.width - w; break;
        }

        int y = final_rect.y;
        switch (v_) {
            case VAlign::Start: case VAlign::Stretch: y = final_rect.y; break;
            case VAlign::Center: y = final_rect.y + (final_rect.height - h) / 2; break;
            case VAlign::End: y = final_rect.y + final_rect.height - h; break;
        }

        child->arrange(Rect{x, y, w, h});
    }

private:
    HAlign h_;
    VAlign v_;
};

} // namespace tui
