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

    /// Couleurs utilisées par le style Detailed ; sans effet en Compact
    /// (qui reste en vidéo inverse par défaut du terminal).
    void set_colors(Color accent, Color foreground, Color muted) {
        accent_color_ = accent;
        foreground_color_ = foreground;
        muted_color_ = muted;
        set_scrollbar_color(accent);
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

    [[nodiscard]] Size content_size() const override {
        return {row_max_width(), static_cast<int>(items_.size()) * row_height()};
    }
    [[nodiscard]] Point scroll_offset() const override { return {scroll_col_, scroll_offset_ * row_height()}; }
    void set_scroll_offset(int x, int y) override {
        int rh = std::max(row_height(), 1);
        int max_items = std::max(static_cast<int>(items_.size()) - visible_item_count(), 0);
        scroll_offset_ = std::clamp(y / rh, 0, max_items);
        scroll_col_ = std::clamp(x, 0, max_scroll_col());
        need_repaint();
    }

    void arrange(Rect final_rect) override {
        bounds_ = final_rect;
        // On ne fait que borner le défilement : on n'y force PAS la sélection
        // visible, pour que l'utilisateur puisse défiler au-delà de l'élément
        // sélectionné. ensure_visible() n'intervient qu'au changement de sélection.
        clamp_scroll();
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
        if (handle_h_scroll_wheel(e)) return true;
        if (e.button == MouseEvent::Button::WheelUp) { scroll_by(-3); return true; }
        if (e.button == MouseEvent::Button::WheelDown) { scroll_by(3); return true; }
        if (handle_scrollbar_mouse(e)) return true;
        if (e.button == MouseEvent::Button::Left && e.action == MouseEvent::Action::Press) {
            int height = row_height();
            size_t idx = static_cast<size_t>(scroll_offset_ + (e.y - bounds_.y) / std::max(height, 1));
            if (idx < items_.size() && items_[idx].selectable) {
                set_selected_index(idx);
                // Case « [x] » en tête de ligne : coche / décoche.
                if (multi_select_ && e.x - bounds_.x < 3) toggle_multi(idx);
                else if (e.clicks >= 2 && on_activate_) on_activate_(idx);
            }
            return true;
        }
        return false;
    }

private:
    [[nodiscard]] int row_height() const { return style_ == ListViewStyle::Detailed ? 3 : 1; }
    [[nodiscard]] int visible_item_count() const { return std::max(bounds_.height / std::max(row_height(), 1), 1); }

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
        scroll_offset_ += delta;
        clamp_scroll();
        need_repaint();
    }

    void clamp_scroll() {
        int max_scroll = std::max(static_cast<int>(items_.size()) - visible_item_count(), 0);
        scroll_offset_ = std::clamp(scroll_offset_, 0, max_scroll);
        scroll_col_ = std::clamp(scroll_col_, 0, max_scroll_col());
    }

    void ensure_visible() {
        if (bounds_.height <= 0) return;
        int visible = visible_item_count();
        if (static_cast<int>(selected_) < scroll_offset_) scroll_offset_ = static_cast<int>(selected_);
        if (static_cast<int>(selected_) >= scroll_offset_ + visible) {
            scroll_offset_ = static_cast<int>(selected_) - visible + 1;
        }
        clamp_scroll();
    }

    [[nodiscard]] std::string item_prefix(size_t idx, const ListItem& item) const {
        std::string prefix = multi_select_ ? (std::string("[") + (is_checked(idx) ? "x" : " ") + "] ") : "";
        return prefix + (item.prefix_icon.empty() ? "" : item.prefix_icon + " ");
    }

    /// Largeur maximale d'une ligne (pour le défilement horizontal).
    [[nodiscard]] int row_max_width() const {
        int w = 0;
        for (size_t i = 0; i < items_.size(); ++i) {
            const auto& it = items_[i];
            int title = TextHelper::display_width(TextHelper::decode_utf8(item_prefix(i, it) + it.text));
            if (style_ == ListViewStyle::Detailed) {
                title += 2; // barre + retrait
                if (!it.detail.empty())
                    w = std::max(w, 2 + TextHelper::display_width(TextHelper::decode_utf8(it.detail)));
            }
            w = std::max(w, title);
        }
        return w;
    }
    [[nodiscard]] int max_scroll_col() const { return std::max(row_max_width() - content_width(), 0); }

    /// Dessine `text` à partir de `start_x`, décalé de scroll_col_, tronqué à
    /// [clip_left, clip_right).
    void draw_text_clipped(Buffer& buffer, const std::u32string& text, int start_x, int y,
                           int clip_left, int clip_right, Color fg, TextStyle style) const {
        int x = start_x - scroll_col_;
        for (char32_t ch : text) {
            if (x >= clip_right) break;
            if (x >= clip_left) buffer.set(x, y, Cell{ch, 1, fg, Color::Default(), style});
            ++x;
        }
    }

    void paint_row_compact(Buffer& buffer, const ListItem& item, size_t idx, int y) const {
        std::string display = item_prefix(idx, item) + item.text;
        auto decoded = TextHelper::decode_utf8(display);
        RowCursor cur = row_cursor(focused_);
        bool sel = cur.active && idx == selected_;
        TextStyle style = sel ? cur.style : TextStyle::None;
        Color fg = sel ? cur.fg : Color::Default(), bg = sel ? cur.bg : Color::Default();
        int left = bounds_.x;
        int right = left + content_width();
        if (sel && cur.full_row)
            for (int fx = left; fx < right; ++fx) buffer.set(fx, y, Cell{U' ', 1, fg, bg, style});
        draw_text_clipped(buffer, decoded, left, y, left, right, fg, style);
    }

    void paint_row_detailed(Buffer& buffer, const ListItem& item, size_t idx, int y) const {
        bool selected = focused_ && idx == selected_;
        Color bar_color = selected ? accent_color_ : Color::Default();
        Color title_color = selected ? accent_color_ : foreground_color_;
        Color detail_color = selected ? accent_color_ : muted_color_;
        TextStyle title_style = selected ? TextStyle::Bold : TextStyle::None;
        int left = bounds_.x;
        int right = left + content_width();

        auto bar_cell = [&](char32_t ch, Color color) { return Cell{ch, 1, color, Color::Default(), TextStyle::None}; };

        if (bounds_.height > 0) {
            buffer.set(left, y, bar_cell(selected ? U'│' : U' ', bar_color));
            auto decoded = TextHelper::decode_utf8(item_prefix(idx, item) + item.text);
            draw_text_clipped(buffer, decoded, left + 2, y, left + 2, right, title_color, title_style);
        }

        int y1 = y + 1;
        if (!item.detail.empty() && y1 < bounds_.y + bounds_.height) {
            buffer.set(left, y1, bar_cell(selected ? U'│' : U' ', bar_color));
            auto decoded = TextHelper::decode_utf8(item.detail);
            draw_text_clipped(buffer, decoded, left + 2, y1, left + 2, right, detail_color, TextStyle::None);
        }
    }

    std::vector<ListItem> items_;
    size_t selected_ = 0;
    int scroll_offset_ = 0;
    int scroll_col_ = 0;
    bool multi_select_ = false;
    std::set<size_t> multi_selected_;
    bool focused_ = false;
    ListViewStyle style_ = ListViewStyle::Compact;
    Color accent_color_ = Color::Default();
    Color foreground_color_ = Color::Default();
    Color muted_color_ = Color::Default();
    std::function<void(size_t)> on_select_;
    std::function<void(size_t)> on_activate_;
};

} // namespace tui
