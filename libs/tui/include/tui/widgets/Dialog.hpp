/**
 * @file Dialog.hpp
 * @brief Overlay modal centré : assombrit le fond, encadre son contenu, piège le focus.
 *
 * set_focus_manager() est optionnel mais recommandé : quand défini,
 * show()/hide() appellent push_scope()/pop_scope() sur le FocusManager
 * indiqué, empêchant Tab/Shift+Tab de faire sortir le focus vers les
 * widgets situés derrière la modale tant qu'elle est ouverte.
 */

#pragma once

#include "../Buffer.hpp"
#include "../widget/BoxFrame.hpp"
#include "../widget/FocusManager.hpp"
#include "../style/ColorUtil.hpp"
#include "../widget/Widget.hpp"

#include <algorithm>
#include <memory>
#include <cstdlib>
#include <string>

namespace tui {

class Dialog : public Widget {
public:
    void set_title(std::string title) { title_ = std::move(title); need_repaint(); }

    void set_content(std::shared_ptr<Widget> content) {
        content_ = std::move(content);
        if (content_) content_->set_parent(this);
        need_repaint();
    }

    void set_size(Size size) { desired_size_ = size; need_repaint(); }
    void set_focus_manager(FocusManager* fm) { focus_manager_ = fm; }

    void show() {
        if (showing_) return;
        showing_ = true;
        if (focus_manager_) focus_manager_->push_scope(shared_from_this());
        need_repaint();
    }

    void hide() {
        if (!showing_) return;
        showing_ = false;
        if (focus_manager_) focus_manager_->pop_scope();
        need_repaint();
    }

    [[nodiscard]] bool showing() const { return showing_; }

    /// Fermée, une Dialog (et son contenu) ne doit être ni peinte, ni
    /// traversée par FocusManager, ni ciblée par le hit-testing souris.
    [[nodiscard]] bool visible() const override { return showing_; }

    [[nodiscard]] Size measure(const Constraints& c) const override {
        return c.clamp({c.max_width, c.max_height}); // le scrim occupe toujours tout l'espace donné
    }

    void arrange(Rect final_rect) override {
        bounds_ = final_rect;
        int w = std::min(desired_size_.width, final_rect.width);
        int h = std::min(desired_size_.height, final_rect.height);
        int x = final_rect.x + (final_rect.width - w) / 2;
        int y = final_rect.y + (final_rect.height - h) / 2;
        dialog_rect_ = Rect{x, y, w, h};
        if (content_) content_->layout(dialog_rect_.inset(1));
    }

    void paint(Buffer& buffer) const override {
        if (!showing_) return;
        if (styled()) {
            paint_styled(buffer);
            if (content_ && content_->visible()) content_->render(buffer);
            // Le contenu peint en fond « par défaut » : il prend celui de la boîte.
            if (auto bg = style().extra_color("dialog-background")) {
                Rect r = dialog_rect_.intersect(buffer.bounds());
                for (int y = r.y; y < r.bottom(); ++y)
                    for (int x = r.x; x < r.right(); ++x)
                        if (buffer.at(x, y).bg.is_default) buffer.at(x, y).bg = *bg;
            }
            return;
        }
        paint_scrim(buffer);
        draw_box_frame(buffer, dialog_rect_, title_);
        if (content_ && content_->visible()) content_->render(buffer);
    }

    bool on_key(const KeyEvent& e) override {
        if (!showing_) return false;
        if (e.key == Key::Escape) { hide(); return true; }
        return content_ ? content_->on_key(e) : false;
    }

    bool on_mouse(const MouseEvent&) override {
        // Absorbe tout clic (y compris ceux hors du cadre, sur le scrim)
        // pour bloquer les widgets derrière la modale tant qu'elle est ouverte.
        return showing_;
    }

    [[nodiscard]] std::vector<std::shared_ptr<Widget>> children() const override {
        std::vector<std::shared_ptr<Widget>> result;
        if (content_) result.push_back(content_);
        return result;
    }

    [[nodiscard]] Rect dialog_rect() const { return dialog_rect_; }

private:
    /// Rendu stylé (CSS) : l'interface reste visible sous un voile
    /// (« scrim-color », opacité « scrim-opacity » en %), la boîte a son
    /// propre fond (« dialog-background »), cadre « border-color » et titre
    /// « border-title-color ».
    void paint_styled(Buffer& buffer) const {
        const auto& st = style();
        auto scrim = st.extra_color("scrim-color");
        double opacity = 0.6;
        if (auto o = st.extra_value("scrim-opacity"); !o.empty()) opacity = std::clamp(std::atof(o.c_str()) / 100.0, 0.0, 1.0);
        Rect area = bounds_.intersect(buffer.current_clip()).intersect(buffer.bounds());
        for (int y = area.y; y < area.bottom(); ++y)
            for (int x = area.x; x < area.right(); ++x) {
                Cell& c = buffer.at(x, y);
                if (scrim) {
                    // Couleur « par défaut » : celle du style (sinon elle reprendrait
                    // plus tard la couleur vive de l'écran, sans voile).
                    Color fg = c.fg.is_default ? st.color.value_or(Color::Default()) : c.fg;
                    if (!fg.is_default) c.fg = color::blend(fg, *scrim, opacity);
                    c.bg = c.bg.is_default ? *scrim : color::blend(c.bg, *scrim, opacity);
                } else {
                    c.style = c.style | TextStyle::Dim;
                }
            }
        Color bg = st.extra_color("dialog-background").value_or(Color::Default());
        for (int y = dialog_rect_.y; y < dialog_rect_.bottom(); ++y)
            for (int x = dialog_rect_.x; x < dialog_rect_.right(); ++x)
                buffer.set(x, y, Cell{U' ', 1, Color::Default(), bg, TextStyle::None});
        Color frame = st.border_color.value_or(Color::Default());
        draw_box_frame(buffer, dialog_rect_, title_, BorderStyle::Rounded, frame,
                       st.border_title_color.value_or(frame), bg, TextStyle::Bold);
    }

    void paint_scrim(Buffer& buffer) const {
        for (int y = bounds_.y; y < bounds_.bottom(); ++y) {
            for (int x = bounds_.x; x < bounds_.right(); ++x) {
                buffer.set(x, y, Cell{U' ', 1, Color::Default(), Color::Default(), TextStyle::Dim});
            }
        }
    }

    std::string title_;
    std::shared_ptr<Widget> content_;
    Size desired_size_{40, 10};
    Rect dialog_rect_;
    bool showing_ = false;
    FocusManager* focus_manager_ = nullptr;
};

} // namespace tui
