/**
 * @file Footer.hpp
 * @brief Barre de raccourcis à la Textual : « ^q Quitter  F1 Aide … ».
 * Chaque raccourci est cliquable (déclenche son action) et réagit au survol.
 *
 * CSS : « Footer { background: $footer-background; } » ; propriétés
 * « key-color », « key-background », « description-color »,
 * « hover-background ».
 */

#pragma once

#include "../widget/Paint.hpp"
#include "../widget/Widget.hpp"

#include <functional>
#include <string>
#include <vector>

namespace tui {

struct FooterBinding {
    std::string key;           ///< libellé de la touche (« ^q », « F1 »…)
    std::string description;
    std::function<void()> action;
    bool enabled = true;
};

class Footer : public Widget {
public:
    void set_bindings(std::vector<FooterBinding> b) { bindings_ = std::move(b); need_repaint(); }
    void add_binding(FooterBinding b) { bindings_.push_back(std::move(b)); need_repaint(); }
    [[nodiscard]] const std::vector<FooterBinding>& bindings() const { return bindings_; }

    [[nodiscard]] Size measure(const Constraints& c) const override { return c.clamp({c.max_width, 1}); }

    void paint(Buffer& b) const override {
        if (bounds_.empty()) return;
        paint::fill(b, bounds_);
        Color key_fg = style().extra_color("key-color").value_or(Color::Default());
        Color key_bg = style().extra_color("key-background").value_or(Color::Default());
        Color desc_fg = style().extra_color("description-color").value_or(Color::Default());
        Color hover_bg = style().extra_color("hover-background").value_or(Color::Default());
        int x = bounds_.x;
        items_.clear();
        for (size_t i = 0; i < bindings_.size() && x < bounds_.right(); ++i) {
            const auto& bd = bindings_[i];
            int start = x;
            bool hover = static_cast<int>(i) == hover_index_;
            Color bg = hover ? hover_bg : Color::Default();
            TextStyle hst = hover && hover_bg.is_default ? TextStyle::Reverse : TextStyle::None;
            TextStyle dim = bd.enabled ? TextStyle::None : TextStyle::Dim;
            x += paint::text(b, x, bounds_.y, bounds_.right() - x, " " + bd.key + " ", key_fg,
                             hover ? bg : key_bg, TextStyle::Bold | hst | dim);
            x += paint::text(b, x, bounds_.y, bounds_.right() - x, bd.description + " ", desc_fg, bg, hst | dim);
            items_.push_back({start, x});
        }
    }

    bool on_mouse(const MouseEvent& e) override {
        int idx = index_at(e.x);
        if (e.action == MouseEvent::Action::Move) {
            if (idx != hover_index_) {
                hover_index_ = idx;
                need_repaint();
                return true;
            }
            return false;
        }
        if (e.button == MouseEvent::Button::Left && e.action == MouseEvent::Action::Press && idx >= 0) {
            auto& bd = bindings_[static_cast<size_t>(idx)];
            if (bd.enabled && bd.action) bd.action();
            return true;
        }
        return false;
    }

    /// Index du raccourci sous la colonne x (-1 si aucun) — après un paint().
    [[nodiscard]] int index_at(int x) const {
        for (size_t i = 0; i < items_.size(); ++i)
            if (x >= items_[i].first && x < items_[i].second) return static_cast<int>(i);
        return -1;
    }

private:
    std::vector<FooterBinding> bindings_;
    mutable std::vector<std::pair<int, int>> items_;
    int hover_index_ = -1;
};

} // namespace tui
