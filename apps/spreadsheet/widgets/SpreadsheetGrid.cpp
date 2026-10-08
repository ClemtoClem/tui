#include "SpreadsheetGrid.hpp"

#include <algorithm>

namespace sheetapp {

namespace {

bool is_error_text(const std::string& s) { return !s.empty() && s[0] == '#'; }

bool parses_as_number(const std::string& s) {
    if (s.empty()) return false;
    try {
        size_t consumed = 0;
        std::stod(s, &consumed);
        return consumed == s.size();
    } catch (...) {
        return false;
    }
}

} // namespace

tui::Size SpreadsheetGrid::measure(const tui::Constraints& c) const { return c.clamp({c.max_width, c.max_height}); }

void SpreadsheetGrid::arrange(tui::Rect final_rect) {
    bounds_ = final_rect;
    ensure_visible();
}

void SpreadsheetGrid::compute_viewport(int& gutter_w, int& visible_cols, int& visible_rows) const {
    int max_row_number = scroll_row_ + std::max(bounds_.height - 1, 0) + 1;
    int digits = 1;
    for (int n = max_row_number; n >= 10; n /= 10) ++digits;
    gutter_w = std::max(digits + 1, 4);
    visible_cols = std::max((bounds_.width - gutter_w) / kColumnWidth, 0);
    visible_rows = std::max(bounds_.height - 1, 0);
}

void SpreadsheetGrid::ensure_visible() {
    int gutter_w = 0, visible_cols = 0, visible_rows = 0;
    compute_viewport(gutter_w, visible_cols, visible_rows);
    visible_cols = std::max(visible_cols, 1);
    visible_rows = std::max(visible_rows, 1);

    if (selected_.col < scroll_col_) scroll_col_ = selected_.col;
    if (selected_.col >= scroll_col_ + visible_cols) scroll_col_ = selected_.col - visible_cols + 1;
    if (selected_.row < scroll_row_) scroll_row_ = selected_.row;
    if (selected_.row >= scroll_row_ + visible_rows) scroll_row_ = selected_.row - visible_rows + 1;
    scroll_col_ = std::max(scroll_col_, 0);
    scroll_row_ = std::max(scroll_row_, 0);
}

void SpreadsheetGrid::put_text(tui::Buffer& buffer, int x, int y, int width, const std::string& text, tui::Color fg,
                                tui::Color bg, tui::TextStyle style, Align align) const {
    if (width <= 0) return;
    auto decoded = tui::TextHelper::decode_utf8(text);
    auto truncated = tui::TextHelper::truncate_to_width(decoded, width);
    int len = tui::TextHelper::display_width(truncated);
    int pad = std::max(width - len, 0);
    int start = align == Align::Left ? 0 : align == Align::Right ? pad : pad / 2;
    for (int i = 0; i < width; ++i) {
        char32_t ch = U' ';
        int idx = i - start;
        if (idx >= 0 && idx < static_cast<int>(truncated.size())) ch = truncated[static_cast<size_t>(idx)];
        buffer.set(x + i, y, tui::Cell{ch, 1, fg, bg, style});
    }
}

void SpreadsheetGrid::paint(tui::Buffer& buffer) const {
    tui::ClipGuard clip(buffer, bounds_);
    if (bounds_.width <= 0 || bounds_.height <= 0) return;
    int gutter_w = 0, visible_cols = 0, visible_rows = 0;
    compute_viewport(gutter_w, visible_cols, visible_rows);
    paint_headers(buffer, gutter_w, visible_cols, visible_rows);
    paint_cells(buffer, gutter_w, visible_cols, visible_rows);
}

void SpreadsheetGrid::paint_headers(tui::Buffer& buffer, int gutter_w, int visible_cols, int visible_rows) const {
    using namespace tui;
    TextStyle header_style = TextStyle::Bold | TextStyle::Reverse;

    put_text(buffer, bounds_.x, bounds_.y, gutter_w, "", Color::Default(), Color::Default(), header_style,
              Align::Left);
    for (int c = 0; c < visible_cols; ++c) {
        int col = scroll_col_ + c;
        int cell_x = bounds_.x + gutter_w + c * kColumnWidth;
        put_text(buffer, cell_x, bounds_.y, kColumnWidth, column_label(col), Color::Default(), Color::Default(),
                  header_style, Align::Center);
    }
    for (int r = 0; r < visible_rows; ++r) {
        int row = scroll_row_ + r;
        int y = bounds_.y + 1 + r;
        put_text(buffer, bounds_.x, y, gutter_w, std::to_string(row + 1), Color::Default(), Color::Default(),
                  header_style, Align::Right);
    }
}

void SpreadsheetGrid::paint_cells(tui::Buffer& buffer, int gutter_w, int visible_cols, int visible_rows) const {
    using namespace tui;
    for (int r = 0; r < visible_rows; ++r) {
        int row = scroll_row_ + r;
        int y = bounds_.y + 1 + r;
        for (int c = 0; c < visible_cols; ++c) {
            int col = scroll_col_ + c;
            CellPos pos{col, row};
            int x = bounds_.x + gutter_w + c * kColumnWidth;
            bool is_selected = (pos == selected_);

            if (is_selected && editing_) {
                int width = kColumnWidth;
                int off = 0;
                if (static_cast<int>(edit_cursor_) >= width) off = static_cast<int>(edit_cursor_) - width + 1;
                for (int i = 0; i < width; ++i) {
                    size_t idx = static_cast<size_t>(off + i);
                    char32_t ch = idx < edit_buffer_.size() ? edit_buffer_[idx] : U' ';
                    TextStyle cstyle = TextStyle::None;
                    if (focused_ && idx == edit_cursor_) cstyle = TextStyle::Reverse;
                    buffer.set(x + i, y, Cell{ch, 1, Color::Default(), Color::Default(), cstyle});
                }
                continue;
            }

            std::string text = sheet_.display(pos);
            Align align = is_error_text(text) ? Align::Left : (parses_as_number(text) ? Align::Right : Align::Left);
            Color fg = is_error_text(text) ? Color::Red() : Color::Default();
            TextStyle style = (is_selected && focused_) ? TextStyle::Reverse : TextStyle::None;
            put_text(buffer, x, y, kColumnWidth, text, fg, Color::Default(), style, align);
        }
    }
}

void SpreadsheetGrid::on_focus() { focused_ = true; need_repaint(); }

void SpreadsheetGrid::on_blur() {
    focused_ = false;
    if (editing_) commit_edit(false);
    need_repaint();
}

void SpreadsheetGrid::move_selection(int dcol, int drow) {
    int new_col = std::clamp(selected_.col + dcol, 0, kMaxIndex);
    int new_row = std::clamp(selected_.row + drow, 0, kMaxIndex);
    selected_ = CellPos{new_col, new_row};
    ensure_visible();
    need_repaint();
    notify_selection();
}

void SpreadsheetGrid::begin_edit(bool replace, char32_t initial_char) {
    editing_ = true;
    if (replace) {
        edit_buffer_.clear();
        if (initial_char != 0) edit_buffer_.push_back(initial_char);
    } else {
        edit_buffer_ = tui::TextHelper::decode_utf8(sheet_.raw(selected_));
    }
    edit_cursor_ = edit_buffer_.size();
    need_repaint();
    notify_selection();
}

void SpreadsheetGrid::commit_edit(bool move_down) {
    sheet_.set_cell(selected_, tui::TextHelper::encode_utf8(edit_buffer_));
    editing_ = false;
    edit_buffer_.clear();
    edit_cursor_ = 0;
    if (move_down) {
        move_selection(0, 1); // appelle deja need_repaint() + notify_selection()
    } else {
        need_repaint();
        notify_selection();
    }
}

void SpreadsheetGrid::cancel_edit() {
    editing_ = false;
    edit_buffer_.clear();
    edit_cursor_ = 0;
    need_repaint();
    notify_selection();
}

void SpreadsheetGrid::notify_selection() const {
    if (!on_selection_changed_) return;
    std::string content = editing_ ? tui::TextHelper::encode_utf8(edit_buffer_) : sheet_.raw(selected_);
    on_selection_changed_(selected_, content);
}

bool SpreadsheetGrid::on_key(const tui::KeyEvent& e) {
    using tui::Key;
    if (e.key == Key::Char && e.ctrl && (e.codepoint == U'q' || e.codepoint == U'Q')) {
        if (on_quit_) on_quit_();
        return true;
    }
    if (e.key == Key::Char && e.ctrl && (e.codepoint == U's' || e.codepoint == U'S')) {
        if (editing_) commit_edit(false);
        if (on_save_requested_) on_save_requested_();
        return true;
    }
    if (e.key == Key::Char && e.ctrl && (e.codepoint == U'o' || e.codepoint == U'O')) {
        if (editing_) commit_edit(false);
        if (on_open_requested_) on_open_requested_();
        return true;
    }

    return editing_ ? on_key_editing(e) : on_key_navigating(e);
}

bool SpreadsheetGrid::on_key_editing(const tui::KeyEvent& e) {
    using tui::Key;
    switch (e.key) {
        case Key::Left:
            if (edit_cursor_ > 0) --edit_cursor_;
            need_repaint();
            return true;
        case Key::Right:
            if (edit_cursor_ < edit_buffer_.size()) ++edit_cursor_;
            need_repaint();
            return true;
        case Key::Home:
            edit_cursor_ = 0;
            need_repaint();
            return true;
        case Key::End:
            edit_cursor_ = edit_buffer_.size();
            need_repaint();
            return true;
        case Key::Backspace:
            if (edit_cursor_ > 0) {
                edit_buffer_.erase(edit_cursor_ - 1, 1);
                --edit_cursor_;
                need_repaint();
            }
            return true;
        case Key::Delete:
            if (edit_cursor_ < edit_buffer_.size()) {
                edit_buffer_.erase(edit_cursor_, 1);
                need_repaint();
            }
            return true;
        case Key::Enter:
            commit_edit(true);
            return true;
        case Key::Escape:
            cancel_edit();
            return true;
        case Key::Char:
            if (e.ctrl || e.alt) return true;
            edit_buffer_.insert(edit_buffer_.begin() + static_cast<std::ptrdiff_t>(edit_cursor_), e.codepoint);
            ++edit_cursor_;
            need_repaint();
            return true;
        default:
            return false;
    }
}

bool SpreadsheetGrid::on_key_navigating(const tui::KeyEvent& e) {
    using tui::Key;
    int gutter_w = 0, visible_cols = 0, visible_rows = 0;
    compute_viewport(gutter_w, visible_cols, visible_rows);
    switch (e.key) {
        case Key::Up: move_selection(0, -1); return true;
        case Key::Down: move_selection(0, 1); return true;
        case Key::Left: move_selection(-1, 0); return true;
        case Key::Right: move_selection(1, 0); return true;
        case Key::PageUp: move_selection(0, -std::max(visible_rows, 1)); return true;
        case Key::PageDown: move_selection(0, std::max(visible_rows, 1)); return true;
        case Key::Home:
            selected_.col = 0;
            if (e.ctrl) selected_.row = 0;
            ensure_visible();
            need_repaint();
            notify_selection();
            return true;
        case Key::Backspace:
        case Key::Delete:
            sheet_.set_cell(selected_, "");
            need_repaint();
            notify_selection();
            return true;
        case Key::Enter:
        case Key::F2:
            begin_edit(false);
            return true;
        case Key::Char:
            if (e.ctrl || e.alt) return false;
            begin_edit(true, e.codepoint);
            return true;
        default:
            return false;
    }
}

bool SpreadsheetGrid::on_mouse(const tui::MouseEvent& e) {
    if (e.action != tui::MouseEvent::Action::Press || e.button != tui::MouseEvent::Button::Left) return false;
    if (!bounds_.contains(tui::Point{e.x, e.y})) return false;

    int gutter_w = 0, visible_cols = 0, visible_rows = 0;
    compute_viewport(gutter_w, visible_cols, visible_rows);
    if (e.y == bounds_.y) return true; // clic sur l'en-tete de colonnes : absorbe, pas d'action

    int row_idx = e.y - bounds_.y - 1;
    if (row_idx < 0 || row_idx >= visible_rows) return false;
    if (e.x < bounds_.x + gutter_w) return true; // clic sur la colonne de numeros de ligne

    int col_idx = (e.x - bounds_.x - gutter_w) / kColumnWidth;
    if (col_idx < 0 || col_idx >= visible_cols) return false;

    if (editing_) commit_edit(false);
    selected_ = CellPos{scroll_col_ + col_idx, scroll_row_ + row_idx};
    ensure_visible();
    need_repaint();
    notify_selection();
    return true;
}

} // namespace sheetapp
