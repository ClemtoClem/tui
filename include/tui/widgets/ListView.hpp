/**
 * @file ListView.hpp
 * @brief Liste défilante navigable au clavier, sélection simple ou multiple.
 *
 * N'hérite pas de ScrollableContainer (celui-ci attend un unique widget
 * enfant à défiler ; ListView peint des lignes de données directement,
 * pas des sous-widgets) mais reproduit la même technique : un offset de
 * défilement + un ClipGuard, pas de logique de scroll séparée.
 */

#pragma once

#include "../Buffer.hpp"
#include "../core/Text.hpp"
#include "../widget/Scrollbar.hpp"
#include "../widget/Widget.hpp"

#include <algorithm>
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace tui {

struct ListItem {
    std::string text;
    std::string detail;
    bool selectable = true;
    std::string prefix_icon;
};

/// Compact (défaut) : une ligne par item, sélection en vidéo inverse -
/// rendu historique, inchangé.
/// Detailed : deux lignes par item (texte + detail) suivies d'une ligne
/// d'espacement, item sélectionné marqué par une barre d'accent à
/// gauche plutôt qu'en vidéo inverse - façon panneau de liste planor.
enum class ListViewStyle {
    Compact,
    Detailed,
};

class ListView : public Widget {
public:
    void set_items(std::vector<ListItem> items) {
        items_ = std::move(items);
        if (selected_ >= items_.size()) selected_ = items_.empty() ? 0 : items_.size() - 1;
        need_repaint();
    }
    [[nodiscard]] const std::vector<ListItem>& items() const { return items_; }

    void set_multi_select(bool enabled) {
        multi_select_ = enabled;
        if (!enabled) multi_selected_.clear();
    }
    [[nodiscard]] bool multi_select() const { return multi_select_; }
    [[nodiscard]] bool is_checked(size_t i) const { return multi_selected_.count(i) > 0; }

    void set_style(ListViewStyle style) { style_ = style; need_repaint(); }

    /// Réserve la dernière colonne pour une piste de défilement cliquable
    /// (jump-to-position) en plus de la molette/PageUp/PageDown déjà
    /// gérés. Désactivée par défaut pour ne rien changer aux listes
    /// existantes qui utilisent toute leur largeur pour le texte.
    void set_show_scrollbar(bool show) { show_scrollbar_ = show; need_repaint(); }

    /// Couleurs utilisées par le style Detailed ; sans effet en Compact
    /// (qui reste en vidéo inverse par défaut du terminal).
    void set_colors(Color accent, Color foreground, Color muted) {
        accent_color_ = accent;
        foreground_color_ = foreground;
        muted_color_ = muted;
        need_repaint();
    }

    [[nodiscard]] size_t selected_index() const { return selected_; }
    void set_selected_index(size_t i) {
        if (i >= items_.size()) return;
        selected_ = i;
        ensure_visible();
        need_repaint();
        if (on_select_) on_select_(selected_);
    }

    void set_on_select(std::function<void(size_t)> cb) { on_select_ = std::move(cb); }
    void set_on_activate(std::function<void(size_t)> cb) { on_activate_ = std::move(cb); }

    [[nodiscard]] Size measure(const Constraints& c) const override {
        return c.clamp({c.max_width, c.max_height});
    }

    void arrange(Rect final_rect) override {
        bounds_ = final_rect;
        ensure_visible();
    }

    void paint(Buffer& buffer) const override {
        ClipGuard clip(buffer, bounds_);
        int height = row_height();
        for (int slot = 0; slot * height < bounds_.height; ++slot) {
            size_t idx = static_cast<size_t>(scroll_offset_ + slot);
            if (idx >= items_.size()) continue;
            int y = bounds_.y + slot * height;
            if (style_ == ListViewStyle::Detailed) {
                paint_row_detailed(buffer, items_[idx], idx, y);
            } else {
                paint_row_compact(buffer, items_[idx], idx, y);
            }
        }
        if (show_scrollbar_ && bounds_.width > 1) {
            Rect track{bounds_.x + bounds_.width - 1, bounds_.y, 1, bounds_.height};
            draw_vertical_scrollbar(buffer, track, static_cast<int>(items_.size()), visible_item_count(),
                                     scroll_offset_, accent_color_);
        }
    }

    [[nodiscard]] bool focusable() const override { return !items_.empty(); }
    void on_focus() override { focused_ = true; need_repaint(); }
    void on_blur() override { focused_ = false; need_repaint(); }

    bool on_key(const KeyEvent& e) override {
        if (items_.empty()) return false;
        if (e.key == Key::Up) { move_selection(-1); return true; }
        if (e.key == Key::Down) { move_selection(1); return true; }
        if (e.key == Key::PageUp) { move_selection(-std::max(visible_item_count(), 1)); return true; }
        if (e.key == Key::PageDown) { move_selection(std::max(visible_item_count(), 1)); return true; }
        if (e.key == Key::Home) { set_selected_index(0); return true; }
        if (e.key == Key::End) { set_selected_index(items_.empty() ? 0 : items_.size() - 1); return true; }
        if (multi_select_ && e.key == Key::Char && e.codepoint == U' ') { toggle_multi(selected_); return true; }
        if (e.key == Key::Enter) { if (on_activate_) on_activate_(selected_); return true; }
        return false;
    }

    bool on_mouse(const MouseEvent& e) override {
        if (e.button == MouseEvent::Button::WheelUp) { scroll_by(-3); return true; }
        if (e.button == MouseEvent::Button::WheelDown) { scroll_by(3); return true; }
        bool on_scrollbar_column = show_scrollbar_ && bounds_.width > 1 && e.x == bounds_.x + bounds_.width - 1;
        if (on_scrollbar_column && e.button == MouseEvent::Button::Left &&
            (e.action == MouseEvent::Action::Press || e.action == MouseEvent::Action::Drag)) {
            Rect track{bounds_.x + bounds_.width - 1, bounds_.y, 1, bounds_.height};
            int pos = scrollbar_hit_to_position(track, static_cast<int>(items_.size()), visible_item_count(), e.y);
            scroll_offset_ = pos;
            need_repaint();
            return true;
        }
        if (e.button == MouseEvent::Button::Left && e.action == MouseEvent::Action::Press) {
            int height = row_height();
            size_t idx = static_cast<size_t>(scroll_offset_ + (e.y - bounds_.y) / std::max(height, 1));
            if (idx < items_.size()) set_selected_index(idx);
            return true;
        }
        return false;
    }

private:
    [[nodiscard]] int row_height() const { return style_ == ListViewStyle::Detailed ? 3 : 1; }
    [[nodiscard]] int visible_item_count() const { return std::max(bounds_.height / std::max(row_height(), 1), 1); }
    [[nodiscard]] int content_width() const { return show_scrollbar_ && bounds_.width > 1 ? bounds_.width - 1 : bounds_.width; }

    void move_selection(int delta) {
        if (items_.empty()) return;
        long next = std::clamp(static_cast<long>(selected_) + delta, 0L, static_cast<long>(items_.size()) - 1);
        set_selected_index(static_cast<size_t>(next));
    }

    void toggle_multi(size_t i) {
        if (multi_selected_.count(i)) multi_selected_.erase(i);
        else multi_selected_.insert(i);
        need_repaint();
    }

    void scroll_by(int delta) {
        int max_scroll = std::max(static_cast<int>(items_.size()) - visible_item_count(), 0);
        scroll_offset_ = std::clamp(scroll_offset_ + delta, 0, max_scroll);
        need_repaint();
    }

    void ensure_visible() {
        if (bounds_.height <= 0) return;
        int visible = visible_item_count();
        if (static_cast<int>(selected_) < scroll_offset_) scroll_offset_ = static_cast<int>(selected_);
        if (static_cast<int>(selected_) >= scroll_offset_ + visible) {
            scroll_offset_ = static_cast<int>(selected_) - visible + 1;
        }
        int max_scroll = std::max(static_cast<int>(items_.size()) - visible, 0);
        scroll_offset_ = std::clamp(scroll_offset_, 0, max_scroll);
    }

    [[nodiscard]] std::string item_prefix(size_t idx, const ListItem& item) const {
        std::string prefix = multi_select_ ? (std::string("[") + (is_checked(idx) ? "x" : " ") + "] ") : "";
        return prefix + (item.prefix_icon.empty() ? "" : item.prefix_icon + " ");
    }

    void paint_row_compact(Buffer& buffer, const ListItem& item, size_t idx, int y) const {
        std::string display = item_prefix(idx, item) + item.text;
        auto decoded = TextHelper::decode_utf8(display);
        auto truncated = TextHelper::truncate_to_width(decoded, content_width());
        TextStyle style = (focused_ && idx == selected_) ? TextStyle::Reverse : TextStyle::None;
        int x = bounds_.x;
        for (char32_t ch : truncated) {
            buffer.set(x, y, Cell{ch, 1, Color::Default(), Color::Default(), style});
            ++x;
        }
    }

    void paint_row_detailed(Buffer& buffer, const ListItem& item, size_t idx, int y) const {
        bool selected = focused_ && idx == selected_;
        Color bar_color = selected ? accent_color_ : Color::Default();
        Color title_color = selected ? accent_color_ : foreground_color_;
        Color detail_color = selected ? accent_color_ : muted_color_;
        TextStyle title_style = selected ? TextStyle::Bold : TextStyle::None;
        int text_width = std::max(content_width() - 2, 0);

        auto bar_cell = [&](char32_t ch, Color color) { return Cell{ch, 1, color, Color::Default(), TextStyle::None}; };

        if (bounds_.height > 0) {
            buffer.set(bounds_.x, y, bar_cell(selected ? U'│' : U' ', bar_color));
            std::string title_text = item_prefix(idx, item) + item.text;
            auto decoded = TextHelper::decode_utf8(title_text);
            auto truncated = TextHelper::truncate_to_width(decoded, text_width);
            int x = bounds_.x + 2;
            for (char32_t ch : truncated) {
                buffer.set(x, y, Cell{ch, 1, title_color, Color::Default(), title_style});
                ++x;
            }
        }

        int y1 = y + 1;
        if (!item.detail.empty() && y1 < bounds_.y + bounds_.height) {
            buffer.set(bounds_.x, y1, bar_cell(selected ? U'│' : U' ', bar_color));
            auto decoded = TextHelper::decode_utf8(item.detail);
            auto truncated = TextHelper::truncate_to_width(decoded, text_width);
            int x = bounds_.x + 2;
            for (char32_t ch : truncated) {
                buffer.set(x, y1, Cell{ch, 1, detail_color, Color::Default(), TextStyle::None});
                ++x;
            }
        }
    }

    std::vector<ListItem> items_;
    size_t selected_ = 0;
    int scroll_offset_ = 0;
    bool multi_select_ = false;
    std::set<size_t> multi_selected_;
    bool focused_ = false;
    bool show_scrollbar_ = false;
    ListViewStyle style_ = ListViewStyle::Compact;
    Color accent_color_ = Color::Default();
    Color foreground_color_ = Color::Default();
    Color muted_color_ = Color::Default();
    std::function<void(size_t)> on_select_;
    std::function<void(size_t)> on_activate_;
};

} // namespace tui
