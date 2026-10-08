/**
 * @file Scrollbar.hpp
 * @brief Piste + curseur de défilement vertical : helper de dessin partagé
 * (même schéma que BoxFrame.hpp) réutilisé par ListView/TreeView/TextArea,
 * plus un widget autonome pour les cas où le contenu défilant n'est pas
 * l'un de ces trois-là.
 */

#pragma once

#include "../Buffer.hpp"
#include "Widget.hpp"

#include <algorithm>
#include <functional>

namespace tui {

/// Dessine une piste de défilement verticale sur 1 colonne. `content_size`
/// et `viewport_size` sont dans la même unité que `position` (lignes,
/// items, peu importe). Si le contenu tient entièrement dans le viewport,
/// seule la piste (sans curseur) est dessinée - convention "scrollbar
/// désactivée mais visible" plutôt que masquée, pour ne pas faire sauter
/// la mise en page quand le contenu grandit.
inline void draw_vertical_scrollbar(Buffer& buffer, Rect track_rect, int content_size, int viewport_size,
                                     int position, Color thumb_color, Color track_color = Color::Default()) {
    if (track_rect.width <= 0 || track_rect.height <= 0) return;
    int track_h = track_rect.height;
    for (int y = 0; y < track_h; ++y) {
        buffer.set(track_rect.x, track_rect.y + y, Cell{U'│', 1, track_color, Color::Default(), TextStyle::None});
    }
    if (content_size <= viewport_size || content_size <= 0 || viewport_size <= 0) return;

    int thumb_size = std::clamp((viewport_size * track_h) / content_size, 1, track_h);
    int max_pos = std::max(content_size - viewport_size, 1);
    int max_offset = std::max(track_h - thumb_size, 0);
    int thumb_offset = (max_offset * std::clamp(position, 0, max_pos)) / max_pos;

    for (int y = thumb_offset; y < thumb_offset + thumb_size && y < track_h; ++y) {
        buffer.set(track_rect.x, track_rect.y + y, Cell{U'█', 1, thumb_color, Color::Default(), TextStyle::None});
    }
}

/// Convertit une coordonnée souris sur la piste en position de défilement
/// cible (clic direct au niveau du curseur ~ jump-to, comme la plupart des
/// scrollbars terminal). Retourne toujours une valeur déjà bornée.
[[nodiscard]] inline int scrollbar_hit_to_position(Rect track_rect, int content_size, int viewport_size, int mouse_y) {
    int max_pos = std::max(content_size - viewport_size, 0);
    if (max_pos <= 0 || track_rect.height <= 1) return 0;
    double ratio = static_cast<double>(mouse_y - track_rect.y) / static_cast<double>(track_rect.height - 1);
    int pos = static_cast<int>(ratio * static_cast<double>(max_pos) + 0.5);
    return std::clamp(pos, 0, max_pos);
}

/// Widget de scrollbar autonome, pour piloter le défilement d'un contenu
/// qui ne gère pas lui-même une colonne de scrollbar (ex: TextArea, ou
/// tout widget maison). Le propriétaire synchronise content/viewport/
/// position à chaque frame et réagit à set_on_scroll() pour re-scroller
/// son propre contenu ; ce widget ne connaît rien du contenu lui-même.
class Scrollbar : public Widget {
public:
    void set_content_size(int size) { content_size_ = std::max(size, 0); need_repaint(); }
    void set_viewport_size(int size) { viewport_size_ = std::max(size, 0); need_repaint(); }
    void set_position(int pos) { position_ = std::max(pos, 0); need_repaint(); }
    [[nodiscard]] int position() const { return position_; }

    void set_color(Color thumb, Color track = Color::Default()) {
        thumb_color_ = thumb;
        track_color_ = track;
        need_repaint();
    }
    void set_on_scroll(std::function<void(int)> cb) { on_scroll_ = std::move(cb); }

    [[nodiscard]] Size measure(const Constraints& c) const override { return c.clamp({1, c.max_height}); }

    void paint(Buffer& buffer) const override {
        draw_vertical_scrollbar(buffer, bounds_, content_size_, viewport_size_, position_, thumb_color_, track_color_);
    }

    bool on_mouse(const MouseEvent& e) override {
        if (e.button == MouseEvent::Button::WheelUp) { scroll_by(-3); return true; }
        if (e.button == MouseEvent::Button::WheelDown) { scroll_by(3); return true; }
        if (e.button != MouseEvent::Button::Left) return false;
        if (e.action != MouseEvent::Action::Press && e.action != MouseEvent::Action::Drag) return false;
        int pos = scrollbar_hit_to_position(bounds_, content_size_, viewport_size_, e.y);
        set_position(pos);
        if (on_scroll_) on_scroll_(pos);
        return true;
    }

private:
    void scroll_by(int delta) {
        int max_pos = std::max(content_size_ - viewport_size_, 0);
        set_position(std::clamp(position_ + delta, 0, max_pos));
        if (on_scroll_) on_scroll_(position_);
    }

    int content_size_ = 0;
    int viewport_size_ = 0;
    int position_ = 0;
    Color thumb_color_ = Color::Default();
    Color track_color_ = Color::Default();
    std::function<void(int)> on_scroll_;
};

} // namespace tui
