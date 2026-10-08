/**
 * @file NumberInput.hpp
 * @brief Champ numérique : les flèches font varier la valeur (pas de curseur texte).
 *
 * N'hérite pas d'Input : la sémantique clavier diffère trop (les
 * flèches changent la valeur au lieu de déplacer un curseur), donc
 * réutiliser Input aurait forcé à désactiver la moitié de son on_key.
 */

#pragma once

#include "../Buffer.hpp"
#include "../core/Text.hpp"
#include "../widget/Widget.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <functional>

namespace tui {

class NumberInput : public Widget {
public:
    void set_value(double v) {
        double clamped = std::clamp(v, min_, max_);
        if (clamped == value_) return;
        value_ = clamped;
        need_repaint();
        if (on_change_) on_change_(value_);
    }
    [[nodiscard]] double value() const { return value_; }

    void set_range(double min_v, double max_v) {
        min_ = min_v;
        max_ = max_v;
        set_value(value_);
    }
    void set_step(double step) { step_ = step; }
    void set_on_change(std::function<void(double)> cb) { on_change_ = std::move(cb); }

    [[nodiscard]] Size measure(const Constraints& c) const override {
        return c.clamp({std::max(c.min_width, 8), 1});
    }

    void paint(Buffer& buffer) const override {
        if (bounds_.height <= 0) return;
        auto decoded = TextHelper::decode_utf8(format_value());
        auto truncated = TextHelper::truncate_to_width(decoded, bounds_.width);
        TextStyle style = focused_ ? TextStyle::Reverse : TextStyle::None;
        int x = bounds_.x;
        for (char32_t ch : truncated) {
            buffer.set(x, bounds_.y, Cell{ch, 1, Color::Default(), Color::Default(), style});
            ++x;
        }
    }

    [[nodiscard]] bool focusable() const override { return true; }
    void on_focus() override { focused_ = true; need_repaint(); }
    void on_blur() override { focused_ = false; need_repaint(); }

    bool on_key(const KeyEvent& e) override {
        if (e.key == Key::Up || e.key == Key::Right) { set_value(value_ + step_); return true; }
        if (e.key == Key::Down || e.key == Key::Left) { set_value(value_ - step_); return true; }
        return false;
    }

    bool on_mouse(const MouseEvent& e) override {
        if (e.button == MouseEvent::Button::WheelUp) { set_value(value_ + step_); return true; }
        if (e.button == MouseEvent::Button::WheelDown) { set_value(value_ - step_); return true; }
        return false;
    }

private:
    [[nodiscard]] std::string format_value() const {
        bool integral_step = (step_ == static_cast<double>(static_cast<long long>(step_)));
        if (integral_step) return std::format("{}", static_cast<long long>(value_));
        return std::format("{:.2f}", value_);
    }

    double value_ = 0.0;
    double min_ = -1e18;
    double max_ = 1e18;
    double step_ = 1.0;
    bool focused_ = false;
    std::function<void(double)> on_change_;
};

} // namespace tui
