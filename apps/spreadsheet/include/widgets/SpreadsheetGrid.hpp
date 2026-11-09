#pragma once

// Widget grille 2D du tableur : navigation, edition inline (sans
// sous-widget separe, cf. plan - la grille est le seul widget focusable
// en usage normal et reprend directement la logique curseur de
// tui::Input pour la cellule en cours d'edition), defilement horizontal
// et vertical, selection a la souris.

#include "model/CellRef.hpp"
#include "model/Sheet.hpp"

#include <tui/Buffer.hpp>
#include <tui/core/Text.hpp>
#include <tui/widget/Widget.hpp>

#include <functional>
#include <string>

namespace sheetapp {

class SpreadsheetGrid : public tui::Widget {
public:
    explicit SpreadsheetGrid(Sheet& sheet) : sheet_(sheet) {}

    void set_on_quit(std::function<void()> fn) { on_quit_ = std::move(fn); }
    void set_on_save_requested(std::function<void()> fn) { on_save_requested_ = std::move(fn); }
    void set_on_open_requested(std::function<void()> fn) { on_open_requested_ = std::move(fn); }
    void set_on_selection_changed(std::function<void(CellPos, const std::string&)> fn) {
        on_selection_changed_ = std::move(fn);
    }

    [[nodiscard]] CellPos selected() const { return selected_; }

    [[nodiscard]] tui::Size measure(const tui::Constraints& c) const override;
    void arrange(tui::Rect final_rect) override;
    void paint(tui::Buffer& buffer) const override;

    [[nodiscard]] bool focusable() const override { return true; }
    void on_focus() override;
    void on_blur() override;
    bool on_key(const tui::KeyEvent& e) override;
    bool on_mouse(const tui::MouseEvent& e) override;

private:
    enum class Align { Left, Right, Center };

    void compute_viewport(int& gutter_w, int& visible_cols, int& visible_rows) const;
    void ensure_visible();
    void move_selection(int dcol, int drow);
    void begin_edit(bool replace, char32_t initial_char = 0);
    void commit_edit(bool move_down);
    void cancel_edit();
    void notify_selection() const;

    bool on_key_editing(const tui::KeyEvent& e);
    bool on_key_navigating(const tui::KeyEvent& e);

    void paint_headers(tui::Buffer& buffer, int gutter_w, int visible_cols, int visible_rows) const;
    void paint_cells(tui::Buffer& buffer, int gutter_w, int visible_cols, int visible_rows) const;
    void put_text(tui::Buffer& buffer, int x, int y, int width, const std::string& text, tui::Color fg,
                  tui::Color bg, tui::TextStyle style, Align align) const;

    static constexpr int kColumnWidth = 10;

    Sheet& sheet_;
    CellPos selected_{0, 0};
    int scroll_col_ = 0;
    int scroll_row_ = 0;
    bool focused_ = false;

    bool editing_ = false;
    std::u32string edit_buffer_;
    size_t edit_cursor_ = 0;

    std::function<void()> on_quit_;
    std::function<void()> on_save_requested_;
    std::function<void()> on_open_requested_;
    std::function<void(CellPos, const std::string&)> on_selection_changed_;
};

} // namespace sheetapp
