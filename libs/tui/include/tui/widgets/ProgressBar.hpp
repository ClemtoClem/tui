/**
 * @file ProgressBar.hpp
 * @brief Barre de progression déterministe (0-100%) ; mode Gauge = label + barre en un.
 */

#pragma once

#include "../Buffer.hpp"
#include "../core/Text.hpp"
#include "../widget/Widget.hpp"

#include <algorithm>

namespace tui {

class ProgressBar : public Widget {
public:
    void set_progress(double p) { progress_ = std::clamp(p, 0.0, 1.0); need_repaint(); }
    [[nodiscard]] double progress() const { return progress_; }

    /// Mode Gauge : préfixe la barre par "label: " sur la même ligne.
    void set_label(std::string label) { label_ = std::move(label); need_repaint(); }
    void set_show_label(bool show) { show_label_ = show; need_repaint(); }

    [[nodiscard]] Size measure(const Constraints& c) const override {
        return c.clamp({std::max(c.min_width, 10), 1});
    }

    void paint(Buffer& buffer) const override {
        if (bounds_.height <= 0 || bounds_.width <= 0) return;

        int bar_x = bounds_.x;
        int bar_width = bounds_.width;

        if (show_label_ && !label_.empty()) {
            auto prefix = TextHelper::decode_utf8(label_ + ": ");
            int prefix_w = TextHelper::display_width(prefix);
            for (char32_t ch : prefix) {
                if (bar_x >= bounds_.x + bounds_.width) break;
                buffer.set(bar_x, bounds_.y, Cell{ch, 1, Color::Default(), Color::Default(), TextStyle::None});
                ++bar_x;
            }
            bar_width = std::max(bounds_.width - prefix_w, 0);
        }

        int filled = static_cast<int>(bar_width * progress_);
        for (int i = 0; i < bar_width; ++i) {
            char32_t ch = (i < filled) ? U'█' : U'░';
            buffer.set(bar_x + i, bounds_.y, Cell{ch, 1, Color::Default(), Color::Default(), TextStyle::None});
        }
    }

private:
    double progress_ = 0.0;
    bool show_label_ = false;
    std::string label_;
};

} // namespace tui
