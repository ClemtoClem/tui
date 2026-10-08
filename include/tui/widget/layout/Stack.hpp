/**
 * @file Stack.hpp
 * @brief Empile les enfants au même rectangle (le dernier ajouté est visuellement au-dessus).
 */

#pragma once

#include "../Container.hpp"

namespace tui {

class Stack : public Container {
public:
    [[nodiscard]] Size measure(const Constraints& constraints) const override {
        int w = constraints.min_width;
        int h = constraints.min_height;
        for (size_t i = 0; i < child_count(); ++i) {
            Size s = child_at(i).widget->measure(constraints);
            w = std::max(w, s.width);
            h = std::max(h, s.height);
        }
        return constraints.clamp({w, h});
    }

    void arrange(Rect final_rect) override {
        bounds_ = final_rect;
        for (size_t i = 0; i < child_count(); ++i) {
            child_at(i).widget->arrange(final_rect);
        }
    }
};

} // namespace tui
