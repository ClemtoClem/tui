/**
 * @file MenuDialog.hpp
 * @brief Corps d'un sous-menu déroulant, positionné explicitement, basé sur ListView.
 *
 * Même limite de clipping que Dropdown (documentée là-bas) : un rendu
 * d'overlay non-clippé correct nécessite l'infrastructure Stack+Dialog.
 */

#pragma once

#include "../Buffer.hpp"
#include "../core/Text.hpp"
#include "../widget/Widget.hpp"
#include "ListView.hpp"
#include "MenuItem.hpp"

#include <algorithm>
#include <vector>

namespace tui {

class MenuDialog : public Widget {
public:
    void set_items(std::vector<MenuItem> items) {
        items_ = std::move(items);
        std::vector<ListItem> list_items;
        list_items.reserve(items_.size());
        for (const auto& i : items_) {
            list_items.push_back(ListItem{i.separator ? std::string("───") : i.label, "", !i.separator, ""});
        }
        list_.set_items(std::move(list_items));
    }

    void set_position(Point p) { position_ = p; need_repaint(); }

    [[nodiscard]] Size measure(const Constraints& c) const override {
        int w = 10;
        for (const auto& i : items_) {
            w = std::max(w, static_cast<int>(TextHelper::display_width(TextHelper::decode_utf8(i.label))) + 4);
        }
        int h = static_cast<int>(items_.size());
        return c.clamp({w, h});
    }

    /// Ignore le rect fourni : la position est pilotée par set_position()
    /// + la taille naturelle, pas par le flux de mise en page normal
    /// (comme Dropdown, ce n'est pas un enfant classique de son parent).
    void arrange(Rect) override {
        Size s = measure(Constraints::loose({200, 200}));
        bounds_ = Rect{position_.x, position_.y, s.width, s.height};
        list_.arrange(bounds_);
    }

    void paint(Buffer& buffer) const override {
        if (!open_) return;
        list_.paint(buffer);
    }

    void open() { open_ = true; list_.on_focus(); need_repaint(); }
    void close() { open_ = false; list_.on_blur(); need_repaint(); }
    [[nodiscard]] bool is_open() const { return open_; }

    bool on_key(const KeyEvent& e) override {
        if (!open_) return false;
        if (e.key == Key::Escape) { close(); return true; }
        if (e.key == Key::Enter) {
            size_t idx = list_.selected_index();
            if (idx < items_.size() && !items_[idx].separator && items_[idx].on_activate) {
                items_[idx].on_activate();
            }
            close();
            return true;
        }
        return list_.on_key(e);
    }

private:
    std::vector<MenuItem> items_;
    ListView list_;
    Point position_;
    bool open_ = false;
};

} // namespace tui
