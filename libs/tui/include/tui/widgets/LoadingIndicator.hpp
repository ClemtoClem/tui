/**
 * @file LoadingIndicator.hpp
 * @brief Indicateur de chargement animé à la Textual : une vague de points
 * qui s'éclairent tour à tour. Animation pilotée par advance() (timer).
 * CSS : « color » (points allumés), propriété « dim-color ».
 */

#pragma once

#include "../style/ColorUtil.hpp"
#include "../widget/Widget.hpp"
#include "../Buffer.hpp"

#include <cmath>

namespace tui {

class LoadingIndicator : public Widget {
public:
    static constexpr int kDots = 5;

    void advance() { frame_ = (frame_ + 1) % (kDots * 4); need_repaint(); }
    [[nodiscard]] int frame() const { return frame_; }

    [[nodiscard]] Size measure(const Constraints& c) const override { return c.clamp({c.max_width, 1}); }

    /// Intensité 0..1 du point i pour la frame courante.
    [[nodiscard]] double intensity(int i) const {
        double phase = static_cast<double>(frame_) / 4.0;   // un point par 4 frames
        double d = std::fmod(phase - i + kDots * 2, static_cast<double>(kDots));
        return std::max(0.0, 1.0 - d / 2.0);
    }

    void paint(Buffer& b) const override {
        if (bounds_.empty()) return;
        int w = kDots * 2 - 1;
        int x0 = bounds_.x + std::max(0, (bounds_.width - w) / 2);
        int y = bounds_.y + (bounds_.height - 1) / 2;
        Color on = style().color.value_or(Color::Default());
        Color off = style().extra_color("dim-color").value_or(Color::Default());
        for (int i = 0; i < kDots; ++i) {
            double t = intensity(i);
            Color fg = Color::Default();
            TextStyle st = TextStyle::None;
            if (!on.is_default) fg = off.is_default ? color::lighten(on, -(1 - t) * 0.3) : color::blend(off, on, t);
            else st = t > 0.5 ? TextStyle::Bold : TextStyle::Dim;
            b.set(x0 + i * 2, y, Cell{U'●', 1, fg, Color::Default(), st});
        }
    }

private:
    int frame_ = 0;
};

} // namespace tui
