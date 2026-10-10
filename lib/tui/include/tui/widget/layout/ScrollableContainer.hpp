/**
 * @file ScrollableContainer.hpp
 * @brief Fait défiler un enfant plus grand que la zone visible, via offset + clip.
 *
 * Le défilement est implémenté simplement : l'enfant est arrangé à sa
 * taille naturelle complète, décalée de -scroll ; le ClipGuard déjà
 * posé par Container::paint() masque tout ce qui dépasse bounds_.
 */

#pragma once

#include "../Container.hpp"

#include <algorithm>

namespace tui {

class ScrollableContainer : public Container {
public:
    void set_child(std::shared_ptr<Widget> child) {
        children_.clear();
        if (child) add_child(std::move(child));
    }

    [[nodiscard]] Size measure(const Constraints& c) const override {
        return c.clamp({c.max_width, c.max_height});
    }

    void arrange(Rect final_rect) override {
        bounds_ = final_rect;
        if (child_count() == 0) { content_size_ = {0, 0}; return; }

        auto& child = child_at(0).widget;
        content_size_ = child->measure(Constraints::loose({kUnbounded, kUnbounded}));
        content_size_.width = std::max(content_size_.width, final_rect.width);
        content_size_.height = std::max(content_size_.height, final_rect.height);
        clamp_scroll();

        child->arrange(Rect{
            final_rect.x - scroll_x_, final_rect.y - scroll_y_,
            content_size_.width, content_size_.height,
        });
    }

    void scroll_to(int x, int y) {
        scroll_x_ = x;
        scroll_y_ = y;
        clamp_scroll();
        need_repaint();
    }
    void scroll_by(int dx, int dy) { scroll_to(scroll_x_ + dx, scroll_y_ + dy); }

    [[nodiscard]] int scroll_x() const { return scroll_x_; }
    [[nodiscard]] int scroll_y() const { return scroll_y_; }
    [[nodiscard]] int max_scroll_x() const { return std::max(content_size_.width - bounds_.width, 0); }
    [[nodiscard]] int max_scroll_y() const { return std::max(content_size_.height - bounds_.height, 0); }

    bool on_key(const KeyEvent& e) override {
        switch (e.key) {
            case Key::Up: scroll_by(0, -1); return true;
            case Key::Down: scroll_by(0, 1); return true;
            case Key::Left: scroll_by(-1, 0); return true;
            case Key::Right: scroll_by(1, 0); return true;
            case Key::PageUp: scroll_by(0, -std::max(bounds_.height, 1)); return true;
            case Key::PageDown: scroll_by(0, std::max(bounds_.height, 1)); return true;
            default: return false;
        }
    }

    bool on_mouse(const MouseEvent& e) override {
        if (e.button == MouseEvent::Button::WheelUp) { scroll_by(0, -3); return true; }
        if (e.button == MouseEvent::Button::WheelDown) { scroll_by(0, 3); return true; }
        return false;
    }

    /// Ajuste le défilement pour rendre `target` visible (utilisé par le
    /// focus automatique). Le repère de `target` inclut déjà les offsets
    /// des conteneurs défilables situés plus bas dans l'arbre, donc la
    /// comparaison avec bounds_ est directe.
    void make_descendant_visible(const Widget* target) override {
        if (target && !bounds_.empty()) {
            Rect tb = target->bounds();
            if (tb.x < bounds_.x) {
                scroll_x_ -= (bounds_.x - tb.x);
            } else if (tb.right() > bounds_.right()) {
                scroll_x_ += (tb.right() - bounds_.right());
            }
            if (tb.y < bounds_.y) {
                scroll_y_ -= (bounds_.y - tb.y);
            } else if (tb.bottom() > bounds_.bottom()) {
                scroll_y_ += (tb.bottom() - bounds_.bottom());
            }
            clamp_scroll();
            need_repaint();
        }
        if (parent_) parent_->make_descendant_visible(target);
    }

private:
    static constexpr int kUnbounded = 1'000'000;

    void clamp_scroll() {
        scroll_x_ = std::clamp(scroll_x_, 0, max_scroll_x());
        scroll_y_ = std::clamp(scroll_y_, 0, max_scroll_y());
    }

    int scroll_x_ = 0;
    int scroll_y_ = 0;
    Size content_size_{0, 0};
};

/// Ne réagit qu'au défilement vertical (clavier haut/bas/PageUp/Down) ;
/// gauche/droite ignorés pour ne pas surprendre un usage "liste verticale".
class ScrollableVertical : public ScrollableContainer {
public:
    bool on_key(const KeyEvent& e) override {
        if (e.key == Key::Left || e.key == Key::Right) return false;
        return ScrollableContainer::on_key(e);
    }
};

/// Symétrique : ignore haut/bas/PageUp/Down, ne garde que gauche/droite.
class ScrollableHorizontal : public ScrollableContainer {
public:
    bool on_key(const KeyEvent& e) override {
        if (e.key == Key::Up || e.key == Key::Down || e.key == Key::PageUp || e.key == Key::PageDown) return false;
        return ScrollableContainer::on_key(e);
    }
};

} // namespace tui
