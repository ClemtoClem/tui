/**
 * @file TableView.hpp
 * @brief Tableau à colonnes dimensionnées (fixe/stretch) et données via accesseur générique.
 *
 * Une seule classe couvre à la fois le besoin "pagination" et
 * "défilement" de cpp-tui (TablePaginated/TableScrollable) : le
 * défilement suffit pour tout le catalogue prévu en Phase 1, la
 * pagination est reportée jusqu'à un besoin concret.
 */

#pragma once

#include "../Buffer.hpp"
#include "../core/Text.hpp"
#include "../widget/Layout.hpp"
#include "../widget/Widget.hpp"

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace tui {

struct TableColumn {
    std::string header;
    /// Réutilise LayoutParams pour la politique de largeur (Fixed=valeur
    /// exacte, Stretch=poids proportionnel de l'espace restant). Auto
    /// n'a pas de sens ici (pas de contenu widget à mesurer) et se
    /// comporte comme Fixed.
    LayoutParams width{LayoutMode::Stretch, 1, CrossAlign::Stretch};
};

class TableView : public Widget {
public:
    using CellAccessor = std::function<std::string(int row, int col)>;

    void set_columns(std::vector<TableColumn> columns) { columns_ = std::move(columns); need_repaint(); }
    void set_row_count(int count) {
        row_count_ = std::max(count, 0);
        if (selected_row_ >= row_count_) selected_row_ = std::max(row_count_ - 1, 0);
        need_repaint();
    }
    void set_cell_accessor(CellAccessor accessor) { accessor_ = std::move(accessor); }
    void set_on_activate(std::function<void(int)> cb) { on_activate_ = std::move(cb); }

    [[nodiscard]] int selected_row() const { return selected_row_; }
    void set_selected_row(int r) {
        if (r < 0 || r >= row_count_) return;
        selected_row_ = r;
        ensure_visible();
        need_repaint();
    }

    [[nodiscard]] Size measure(const Constraints& c) const override {
        return c.clamp({c.max_width, c.max_height});
    }

    void arrange(Rect final_rect) override {
        bounds_ = final_rect;
        ensure_visible();
    }

    void paint(Buffer& buffer) const override {
        ClipGuard clip(buffer, bounds_);
        if (bounds_.height <= 0) return;
        auto widths = compute_widths(bounds_.width);
        paint_header(buffer, widths);
        for (int row = 1; row < bounds_.height; ++row) {
            int data_row = scroll_offset_ + row - 1;
            if (data_row >= row_count_) continue;
            paint_row(buffer, widths, data_row, bounds_.y + row);
        }
    }

    [[nodiscard]] bool focusable() const override { return row_count_ > 0; }
    void on_focus() override { focused_ = true; need_repaint(); }
    void on_blur() override { focused_ = false; need_repaint(); }

    bool on_key(const KeyEvent& e) override {
        if (row_count_ == 0) return false;
        int viewport = std::max(bounds_.height - 1, 1); // -1 pour la ligne d'en-tête
        if (e.key == Key::Up) { set_selected_row(std::max(selected_row_ - 1, 0)); return true; }
        if (e.key == Key::Down) { set_selected_row(std::min(selected_row_ + 1, row_count_ - 1)); return true; }
        if (e.key == Key::PageUp) { set_selected_row(std::max(selected_row_ - viewport, 0)); return true; }
        if (e.key == Key::PageDown) { set_selected_row(std::min(selected_row_ + viewport, row_count_ - 1)); return true; }
        if (e.key == Key::Home) { set_selected_row(0); return true; }
        if (e.key == Key::End) { set_selected_row(row_count_ - 1); return true; }
        if (e.key == Key::Enter) { if (on_activate_) on_activate_(selected_row_); return true; }
        return false;
    }

private:
    void ensure_visible() {
        int viewport = std::max(bounds_.height - 1, 1);
        if (selected_row_ < scroll_offset_) scroll_offset_ = selected_row_;
        if (selected_row_ >= scroll_offset_ + viewport) scroll_offset_ = selected_row_ - viewport + 1;
        int max_scroll = std::max(row_count_ - viewport, 0);
        scroll_offset_ = std::clamp(scroll_offset_, 0, max_scroll);
    }

    [[nodiscard]] std::vector<int> compute_widths(int total) const {
        int fixed_sum = 0, stretch_sum = 0;
        for (const auto& c : columns_) {
            if (c.width.mode == LayoutMode::Fixed) fixed_sum += std::max(c.width.value, 0);
            else stretch_sum += std::max(c.width.value, 1);
        }
        int remaining = std::max(total - fixed_sum, 0);

        std::vector<int> widths(columns_.size(), 0);
        int distributed = 0;
        int last_stretch = -1;
        for (size_t i = 0; i < columns_.size(); ++i) {
            if (columns_[i].width.mode == LayoutMode::Fixed) {
                widths[i] = std::max(columns_[i].width.value, 0);
            } else {
                int weight = std::max(columns_[i].width.value, 1);
                int w = stretch_sum > 0 ? (remaining * weight) / stretch_sum : 0;
                widths[i] = w;
                distributed += w;
                last_stretch = static_cast<int>(i);
            }
        }
        if (last_stretch >= 0) widths[static_cast<size_t>(last_stretch)] += remaining - distributed;
        return widths;
    }

    void paint_header(Buffer& buffer, const std::vector<int>& widths) const {
        int x = bounds_.x;
        for (size_t i = 0; i < columns_.size(); ++i) {
            auto decoded = TextHelper::decode_utf8(columns_[i].header);
            auto truncated = TextHelper::truncate_to_width(decoded, widths[i]);
            int cx = x;
            for (char32_t ch : truncated) {
                buffer.set(cx, bounds_.y, Cell{ch, 1, Color::Default(), Color::Default(), TextStyle::Bold | TextStyle::Underline});
                ++cx;
            }
            x += widths[i];
        }
    }

    void paint_row(Buffer& buffer, const std::vector<int>& widths, int data_row, int y) const {
        TextStyle style = (focused_ && data_row == selected_row_) ? TextStyle::Reverse : TextStyle::None;
        int x = bounds_.x;
        for (size_t col = 0; col < columns_.size(); ++col) {
            std::string text = accessor_ ? accessor_(data_row, static_cast<int>(col)) : "";
            auto decoded = TextHelper::decode_utf8(text);
            auto truncated = TextHelper::truncate_to_width(decoded, widths[col]);
            int cx = x;
            for (char32_t ch : truncated) {
                buffer.set(cx, y, Cell{ch, 1, Color::Default(), Color::Default(), style});
                ++cx;
            }
            x += widths[col];
        }
    }

    std::vector<TableColumn> columns_;
    int row_count_ = 0;
    CellAccessor accessor_;
    int selected_row_ = 0;
    int scroll_offset_ = 0;
    bool focused_ = false;
    std::function<void(int)> on_activate_;
};

} // namespace tui
