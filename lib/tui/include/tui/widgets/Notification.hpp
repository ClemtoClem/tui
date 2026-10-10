/**
 * @file Notification.hpp
 * @brief Toast auto-disparaissant : tick() est piloté par un App::add_timer externe.
 */

#pragma once

#include "../Buffer.hpp"
#include "../core/Text.hpp"
#include "../widget/BoxFrame.hpp"
#include "../widget/Widget.hpp"

#include <algorithm>
#include <chrono>
#include <string>

namespace tui {

class Notification : public Widget {
public:
    void show(std::string text, std::chrono::milliseconds duration = std::chrono::milliseconds(3000)) {
        text_ = std::move(text);
        duration_ = duration;
        elapsed_ = std::chrono::milliseconds(0);
        showing_ = true;
        need_repaint();
    }
    void dismiss() { showing_ = false; need_repaint(); }
    [[nodiscard]] bool visible() const override { return showing_; }

    /// À appeler périodiquement (typiquement depuis un App::add_timer)
    /// avec le delta écoulé ; se ferme seule une fois `duration_`
    /// atteinte - prouve le chemin TimerManager -> need_repaint() ->
    /// disparition automatique de bout en bout.
    void tick(std::chrono::milliseconds delta) {
        if (!showing_) return;
        elapsed_ += delta;
        if (elapsed_ >= duration_) dismiss();
    }

    [[nodiscard]] Size measure(const Constraints& c) const override {
        auto decoded = TextHelper::decode_utf8(text_);
        return c.clamp({TextHelper::display_width(decoded) + 4, 3});
    }

    void arrange(Rect screen_bounds) override {
        Size s = measure(Constraints::loose({screen_bounds.width, screen_bounds.height}));
        int x = std::max(screen_bounds.right() - s.width - 1, screen_bounds.x);
        int y = screen_bounds.y + 1;
        bounds_ = Rect{x, y, s.width, s.height};
    }

    void paint(Buffer& buffer) const override {
        if (!showing_) return;
        draw_box_frame(buffer, bounds_);
        auto decoded = TextHelper::decode_utf8(text_);
        auto truncated = TextHelper::truncate_to_width(decoded, std::max(bounds_.width - 2, 0));
        int x = bounds_.x + 1;
        for (char32_t ch : truncated) {
            buffer.set(x, bounds_.y + 1, Cell{ch, 1, Color::Default(), Color::Default(), TextStyle::None});
            ++x;
        }
    }

private:
    std::string text_;
    bool showing_ = false;
    std::chrono::milliseconds duration_{0};
    std::chrono::milliseconds elapsed_{0};
};

} // namespace tui
