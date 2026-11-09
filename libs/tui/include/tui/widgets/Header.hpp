/**
 * @file Header.hpp
 * @brief En-tête d'application à la Textual : icône, titre — sous-titre
 * centrés, horloge optionnelle à droite. Cliquer sur l'icône appelle
 * on_icon_click (palette de commandes, menu…).
 *
 * CSS : « Header { background: $panel; } », propriété « icon-color ».
 */

#pragma once

#include "../widget/Paint.hpp"
#include "../widget/Widget.hpp"

#include <chrono>
#include <ctime>
#include <functional>
#include <string>

namespace tui {

class Header : public Widget {
public:
    explicit Header(std::string title = {}, std::string subtitle = {})
        : title_(std::move(title)), subtitle_(std::move(subtitle)) {}

    void set_title(std::string t) { title_ = std::move(t); need_repaint(); }
    void set_subtitle(std::string t) { subtitle_ = std::move(t); need_repaint(); }
    [[nodiscard]] const std::string& title() const { return title_; }
    [[nodiscard]] const std::string& subtitle() const { return subtitle_; }
    void set_icon(std::string icon) { icon_ = std::move(icon); need_repaint(); }
    /// Affiche HH:MM:SS à droite (rafraîchi à chaque frame ; piloter par un timer).
    void set_show_clock(bool on) { clock_ = on; need_repaint(); }
    void set_on_icon_click(std::function<void()> cb) { on_icon_ = std::move(cb); }

    [[nodiscard]] Size measure(const Constraints& c) const override { return c.clamp({c.max_width, 1}); }

    void paint(Buffer& b) const override {
        if (bounds_.empty()) return;
        paint::fill(b, bounds_);
        int y = bounds_.y + (bounds_.height - 1) / 2;
        Color icon_color = style().extra_color("icon-color").value_or(Color::Default());
        int left = paint::text(b, bounds_.x + 1, y, bounds_.width - 1, icon_, icon_color) + 2;
        std::string clock_text;
        if (clock_) {
            std::time_t t = std::time(nullptr);
            char buf[16];
            std::strftime(buf, sizeof buf, "%H:%M:%S", std::localtime(&t));
            clock_text = buf;
            paint::text(b, bounds_.right() - 9, y, 8, clock_text, Color::Default(), Color::Default(), TextStyle::Dim);
        }
        int tw = paint::width(title_);
        int sw = subtitle_.empty() ? 0 : paint::width(subtitle_) + 3;
        int avail = bounds_.width - 2 * std::max(left, clock_ ? 10 : 0);
        int x = bounds_.x + (bounds_.width - std::min(avail, tw + sw)) / 2;
        int w = paint::text(b, x, y, avail, title_, Color::Default(), Color::Default(), TextStyle::Bold);
        if (!subtitle_.empty() && w < avail)
            paint::text(b, x + w, y, avail - w, " — " + subtitle_, Color::Default(), Color::Default(), TextStyle::Dim);
    }

    bool on_mouse(const MouseEvent& e) override {
        if (e.button == MouseEvent::Button::Left && e.action == MouseEvent::Action::Press &&
            e.x <= bounds_.x + 2 && on_icon_) {
            on_icon_();
            return true;
        }
        return false;
    }

private:
    std::string title_, subtitle_;
    std::string icon_ = "⭘";
    bool clock_ = false;
    std::function<void()> on_icon_;
};

} // namespace tui
