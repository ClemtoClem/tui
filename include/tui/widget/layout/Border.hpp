/**
 * @file Border.hpp
 * @brief Décorateur à un seul enfant : dessine un cadre avec titre optionnel.
 */

#pragma once

#include "../../core/Text.hpp"
#include "../BoxFrame.hpp"
#include "../Container.hpp"

namespace tui {

class Border : public Container {
public:
    void set_title(std::string title) { title_ = std::move(title); need_repaint(); }
    void set_child(std::shared_ptr<Widget> child) {
        children_.clear();
        if (child) add_child(std::move(child), LayoutParams::stretch(1));
    }

    /// Style du cadre (carré par défaut, arrondi façon lipgloss/planor).
    void set_border_style(BorderStyle style) { style_ = style; need_repaint(); }

    /// Couleur utilisée hors focus. Color::Default() (valeur par défaut)
    /// laisse le terminal choisir sa couleur habituelle, comme avant
    /// l'ajout de cette méthode.
    void set_color(Color color) { color_ = color; need_repaint(); }

    /// Couleur utilisée quand set_focused(true) a été appelé ; par défaut
    /// identique à color_ tant qu'elle n'a pas été fixée explicitement.
    void set_focused_color(Color color) { focused_color_ = color; has_focused_color_ = true; need_repaint(); }

    /// À appeler par le code applicatif (l'App ne le déduit pas
    /// automatiquement du FocusManager) quand ce panneau contient le
    /// widget actuellement focusé, pour basculer color_ / focused_color_.
    void set_focused(bool focused) {
        if (focused_ == focused) return;
        focused_ = focused;
        need_repaint();
    }
    [[nodiscard]] bool focused() const { return focused_; }

    [[nodiscard]] Size measure(const Constraints& c) const override {
        if (child_count() == 0) return c.clamp({2, 2});
        Constraints inner{
            std::max(c.min_width - 2, 0), std::max(c.max_width - 2, 0),
            std::max(c.min_height - 2, 0), std::max(c.max_height - 2, 0),
        };
        Size s = child_at(0).widget->measure(inner);
        return c.clamp({s.width + 2, s.height + 2});
    }

    void arrange(Rect final_rect) override {
        bounds_ = final_rect;
        if (child_count() > 0) child_at(0).widget->arrange(final_rect.inset(1));
    }

    void paint(Buffer& buffer) const override {
        Color active = (focused_ && has_focused_color_) ? focused_color_ : color_;
        draw_box_frame(buffer, bounds_, title_, style_, active, active);
        Container::paint(buffer);
    }

private:
    std::string title_;
    BorderStyle style_ = BorderStyle::Square;
    Color color_ = Color::Default();
    Color focused_color_ = Color::Default();
    bool has_focused_color_ = false;
    bool focused_ = false;
};

} // namespace tui
