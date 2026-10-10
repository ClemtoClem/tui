#pragma once

// Table generique (en-tetes cliquables pour trier, cellules colorees,
// colonne "barre de progression") : tui::TableView ne fournit aucune de
// ces trois choses (verifie en lisant include/tui/widgets/TableView.hpp),
// donc ce widget la remplace pour cet exemple tout en reprenant le meme
// algorithme de repartition des largeurs et de defilement/selection.

#include <tui/Buffer.hpp>
#include <tui/core/Text.hpp>
#include <tui/widget/Layout.hpp>
#include <tui/widget/Widget.hpp>

#include <algorithm>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace sysmon {

struct ColumnSpec {
    std::string header;
    tui::LayoutParams width{tui::LayoutMode::Stretch, 1, tui::CrossAlign::Stretch};
};

class DataTable : public tui::Widget {
public:
    using CellText = std::function<std::string(int row, int col)>;
    using CellColor = std::function<std::optional<tui::Color>(int row, int col)>;
    using BarValue = std::function<std::optional<double>(int row, int col)>;
    using HeaderClick = std::function<void(int col)>;
    using QuitCallback = std::function<void()>;

    void set_columns(std::vector<ColumnSpec> columns) { columns_ = std::move(columns); need_repaint(); }
    void set_row_count(int count) {
        row_count_ = std::max(count, 0);
        if (selected_row_ >= row_count_) selected_row_ = std::max(row_count_ - 1, 0);
        need_repaint();
    }
    void set_cell_text(CellText fn) { cell_text_ = std::move(fn); }
    void set_cell_color(CellColor fn) { cell_color_ = std::move(fn); }
    void set_bar_value(BarValue fn) { bar_value_ = std::move(fn); }
    void set_on_header_click(HeaderClick fn) { on_header_click_ = std::move(fn); }
    void set_sort_indicator(int col, bool ascending) {
        sort_col_ = col;
        sort_ascending_ = ascending;
        need_repaint();
    }
    void set_on_quit(QuitCallback fn) { on_quit_ = std::move(fn); }

    [[nodiscard]] int selected_row() const { return selected_row_; }
    void set_selected_row(int r) {
        if (r < 0 || r >= row_count_) return;
        selected_row_ = r;
        ensure_visible();
        need_repaint();
    }

    [[nodiscard]] tui::Size measure(const tui::Constraints& c) const override {
        return c.clamp({c.max_width, c.max_height});
    }

    void arrange(tui::Rect final_rect) override {
        bounds_ = final_rect;
        ensure_visible();
    }

    void paint(tui::Buffer& buffer) const override {
        tui::ClipGuard clip(buffer, bounds_);
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

    bool on_key(const tui::KeyEvent& e) override {
        if (e.key == tui::Key::Char && (e.codepoint == U'q' || e.codepoint == U'Q')) {
            if (on_quit_) on_quit_();
            return true;
        }
        if (row_count_ == 0) return false;
        int viewport = std::max(bounds_.height - 1, 1);
        if (e.key == tui::Key::Up) { set_selected_row(std::max(selected_row_ - 1, 0)); return true; }
        if (e.key == tui::Key::Down) { set_selected_row(std::min(selected_row_ + 1, row_count_ - 1)); return true; }
        if (e.key == tui::Key::PageUp) { set_selected_row(std::max(selected_row_ - viewport, 0)); return true; }
        if (e.key == tui::Key::PageDown) {
            set_selected_row(std::min(selected_row_ + viewport, row_count_ - 1));
            return true;
        }
        if (e.key == tui::Key::Home) { set_selected_row(0); return true; }
        if (e.key == tui::Key::End) { set_selected_row(row_count_ - 1); return true; }
        return false;
    }

    bool on_mouse(const tui::MouseEvent& e) override {
        if (e.action != tui::MouseEvent::Action::Press || e.button != tui::MouseEvent::Button::Left) return false;
        if (!bounds_.contains(tui::Point{e.x, e.y})) return false;

        if (e.y == bounds_.y) {
            auto widths = compute_widths(bounds_.width);
            int x = bounds_.x;
            for (size_t i = 0; i < columns_.size(); ++i) {
                if (e.x >= x && e.x < x + widths[i]) {
                    if (on_header_click_) on_header_click_(static_cast<int>(i));
                    return true;
                }
                x += widths[i];
            }
            return true;
        }

        int data_row = scroll_offset_ + (e.y - bounds_.y - 1);
        if (data_row >= 0 && data_row < row_count_) {
            set_selected_row(data_row);
            return true;
        }
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
            if (c.width.mode == tui::LayoutMode::Fixed) fixed_sum += std::max(c.width.value, 0);
            else stretch_sum += std::max(c.width.value, 1);
        }
        int remaining = std::max(total - fixed_sum, 0);

        std::vector<int> widths(columns_.size(), 0);
        int distributed = 0;
        int last_stretch = -1;
        for (size_t i = 0; i < columns_.size(); ++i) {
            if (columns_[i].width.mode == tui::LayoutMode::Fixed) {
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

    void put_text(tui::Buffer& buffer, int x, int y, int width, const std::string& text, tui::Color fg,
                  tui::TextStyle style) const {
        auto decoded = tui::TextHelper::decode_utf8(text);
        auto truncated = tui::TextHelper::truncate_to_width(decoded, width, U"…");
        int cx = x;
        for (char32_t ch : truncated) {
            buffer.set(cx, y, tui::Cell{ch, 1, fg, tui::Color::Default(), style});
            ++cx;
        }
    }

    void paint_header(tui::Buffer& buffer, const std::vector<int>& widths) const {
        int x = bounds_.x;
        for (size_t i = 0; i < columns_.size(); ++i) {
            std::string text = columns_[i].header;
            if (sort_col_ && *sort_col_ == static_cast<int>(i)) {
                text += sort_ascending_ ? " ▲" : " ▼";
            }
            put_text(buffer, x, bounds_.y, widths[i], text, tui::Color::Default(),
                     tui::TextStyle::Bold | tui::TextStyle::Underline);
            x += widths[i];
        }
    }

    void paint_bar(tui::Buffer& buffer, int x, int y, int width, double fraction) const {
        if (width <= 0) return;
        fraction = std::clamp(fraction, 0.0, 1.0);
        std::string pct = std::to_string(static_cast<int>(fraction * 100 + 0.5)) + "%";
        int pct_width = static_cast<int>(pct.size());
        int bar_width = std::max(width - pct_width - 1, 0);
        int filled = static_cast<int>(bar_width * fraction);

        tui::Color bar_color = fraction > 0.9 ? tui::Color::Red() : (fraction > 0.7 ? tui::Color::Yellow() : tui::Color::Green());
        for (int i = 0; i < bar_width; ++i) {
            char32_t ch = (i < filled) ? U'█' : U'░';
            tui::Color fg = (i < filled) ? bar_color : tui::Color::Gray();
            buffer.set(x + i, y, tui::Cell{ch, 1, fg, tui::Color::Default(), tui::TextStyle::None});
        }
        put_text(buffer, x + bar_width + 1, y, pct_width, pct, tui::Color::Default(), tui::TextStyle::None);
    }

    void paint_row(tui::Buffer& buffer, const std::vector<int>& widths, int data_row, int y) const {
        tui::TextStyle row_style = (focused_ && data_row == selected_row_) ? tui::TextStyle::Reverse : tui::TextStyle::None;
        int x = bounds_.x;
        for (size_t col = 0; col < columns_.size(); ++col) {
            std::optional<double> bar = bar_value_ ? bar_value_(data_row, static_cast<int>(col)) : std::nullopt;
            if (bar) {
                if (row_style == tui::TextStyle::Reverse) {
                    for (int i = 0; i < widths[col]; ++i) {
                        buffer.set(x + i, y, tui::Cell{U' ', 1, tui::Color::Default(), tui::Color::Default(), row_style});
                    }
                }
                paint_bar(buffer, x, y, widths[col], *bar);
            } else {
                std::string text = cell_text_ ? cell_text_(data_row, static_cast<int>(col)) : "";
                tui::Color fg = tui::Color::Default();
                if (cell_color_) {
                    if (auto c = cell_color_(data_row, static_cast<int>(col))) fg = *c;
                }
                put_text(buffer, x, y, widths[col], text, fg, row_style);
            }
            x += widths[col];
        }
    }

    std::vector<ColumnSpec> columns_;
    int row_count_ = 0;
    CellText cell_text_;
    CellColor cell_color_;
    BarValue bar_value_;
    HeaderClick on_header_click_;
    QuitCallback on_quit_;
    std::optional<int> sort_col_;
    bool sort_ascending_ = true;
    int selected_row_ = 0;
    int scroll_offset_ = 0;
    bool focused_ = false;
};

} // namespace sysmon
