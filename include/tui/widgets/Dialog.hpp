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
#include "../widget/Widget.hpp"

#include <algorithm>
#include <memory>
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
        if (content_) content_->arrange(dialog_rect_.inset(1));
    }

    void paint(Buffer& buffer) const override {
        if (!showing_) return;
        paint_scrim(buffer);
        draw_box_frame(buffer, dialog_rect_, title_);
        if (content_ && content_->visible()) content_->paint(buffer);
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

private:
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
