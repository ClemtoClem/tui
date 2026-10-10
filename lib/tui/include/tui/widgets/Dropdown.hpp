/**
 * @file Dropdown.hpp
 * @brief Sélecteur déroulant : état fermé + popup de sélection basé sur ListView.
 *
 * paint() ne dessine JAMAIS le popup - seulement la ligne fermée -
 * pour la même raison que MenuBar : un widget peint après un Dropdown
 * dans son parent (ex: un Label en stretch juste en dessous) écraserait
 * sinon le popup ouvert. Utiliser paint_overlay() (via DropdownOverlay,
 * un widget compagnon ajouté en dernier dans un Stack racine) pour
 * garantir que le popup reste au-dessus de tout ce qui est peint après
 * ce Dropdown dans son conteneur parent.
 */

#pragma once

#include "../Buffer.hpp"
#include "../core/Text.hpp"
#include "../widget/Widget.hpp"
#include "ListView.hpp"

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace tui {

class Dropdown : public Widget {
public:
    Dropdown() { popup_.set_multi_select(false); }

    void set_options(std::vector<std::string> options) {
        options_ = std::move(options);
        if (selected_index_ >= options_.size()) selected_index_ = options_.empty() ? 0 : options_.size() - 1;
        sync_popup_items();
        need_repaint();
    }
    [[nodiscard]] const std::vector<std::string>& options() const { return options_; }

    void set_selected_index(size_t i) {
        if (i >= options_.size() || i == selected_index_) return;
        selected_index_ = i;
        need_repaint();
        if (on_change_) on_change_(i);
    }
    [[nodiscard]] size_t selected_index() const { return selected_index_; }
    void set_on_change(std::function<void(size_t)> cb) { on_change_ = std::move(cb); }

    [[nodiscard]] bool is_open() const { return open_; }

    void set_open(bool open) {
        if (open == open_) return;
        open_ = open;
        if (open_) {
            popup_.set_selected_index(selected_index_);
            popup_.on_focus(); // le popup dessine son highlight tant qu'il est ouvert
        } else {
            popup_.on_blur();
        }
        need_repaint();
    }

    /// Nombre de lignes du popup quand ouvert (hauteur totale = 1 + ce
    /// nombre, plafonné à max_popup_rows()).
    void set_max_popup_rows(int rows) { max_popup_rows_ = std::max(rows, 1); }

    [[nodiscard]] Size measure(const Constraints& c) const override {
        int w = 4;
        for (const auto& o : options_) {
            w = std::max(w, static_cast<int>(TextHelper::display_width(TextHelper::decode_utf8(o))) + 4);
        }
        int h = 1 + (open_ ? popup_row_count() : 0);
        return c.clamp({w, h});
    }

    void arrange(Rect final_rect) override {
        bounds_ = final_rect;
        int popup_rows = popup_row_count();
        Rect popup_rect{final_rect.x, final_rect.y + 1, final_rect.width, popup_rows};
        popup_.arrange(popup_rect);
    }

    void paint(Buffer& buffer) const override {
        if (bounds_.height <= 0) return;
        paint_closed_row(buffer);
    }

    /// Peint uniquement le popup s'il est ouvert. Voir la note de tête
    /// de fichier : à appeler après tout le reste de l'arbre (via
    /// DropdownOverlay), jamais depuis paint() lui-même.
    void paint_overlay(Buffer& buffer) const {
        if (open_) popup_.paint(buffer);
    }

    [[nodiscard]] bool has_open_overlay() const { return open_; }

    [[nodiscard]] bool focusable() const override { return true; }
    void on_focus() override { focused_ = true; need_repaint(); }
    void on_blur() override { focused_ = false; need_repaint(); }

    bool on_key(const KeyEvent& e) override {
        if (!open_) {
            if (e.key == Key::Enter || (e.key == Key::Char && e.codepoint == U' ')) {
                set_open(true);
                return true;
            }
            return false;
        }

        if (e.key == Key::Escape) { set_open(false); return true; } // annule, garde l'ancienne sélection
        if (e.key == Key::Enter) {
            set_selected_index(popup_.selected_index());
            set_open(false);
            return true;
        }
        return popup_.on_key(e); // Up/Down/Home/End/PageUp/PageDown déplacent le highlight
    }

    bool on_mouse(const MouseEvent& e) override {
        if (!open_) return false;
        return popup_.on_mouse(e);
    }

private:
    [[nodiscard]] int popup_row_count() const {
        return std::min(static_cast<int>(options_.size()), max_popup_rows_);
    }

    void sync_popup_items() {
        std::vector<ListItem> items;
        items.reserve(options_.size());
        for (auto& o : options_) items.push_back(ListItem{o, "", true, ""});
        popup_.set_items(std::move(items));
    }

    void paint_closed_row(Buffer& buffer) const {
        std::string label = options_.empty() ? "" : options_[selected_index_];
        std::string display = label + " " + (open_ ? "▲" : "▼");
        auto decoded = TextHelper::decode_utf8(display);
        auto truncated = TextHelper::truncate_to_width(decoded, bounds_.width);
        TextStyle style = focused_ ? TextStyle::Reverse : TextStyle::None;
        int x = bounds_.x;
        for (char32_t ch : truncated) {
            buffer.set(x, bounds_.y, Cell{ch, 1, Color::Default(), Color::Default(), style});
            ++x;
        }
    }

    std::vector<std::string> options_;
    size_t selected_index_ = 0;
    bool open_ = false;
    bool focused_ = false;
    int max_popup_rows_ = 8;
    ListView popup_;
    std::function<void(size_t)> on_change_;
};

/// Compagnon minimal : ne peint que le popup ouvert d'un Dropdown
/// donné. À ajouter en DERNIER parmi les enfants d'un Stack racine
/// (même rôle que MenuBarOverlay pour MenuBar).
class DropdownOverlay : public Widget {
public:
    explicit DropdownOverlay(std::shared_ptr<Dropdown> dropdown) : dropdown_(std::move(dropdown)) {}

    [[nodiscard]] Size measure(const Constraints& c) const override { return c.clamp({0, 0}); }
    void paint(Buffer& buffer) const override {
        if (dropdown_) dropdown_->paint_overlay(buffer);
    }

private:
    std::shared_ptr<Dropdown> dropdown_;
};

} // namespace tui
