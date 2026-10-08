#include <tui/widgets/TextArea.hpp>

#include <algorithm>
#include <cstddef>

namespace tui {

namespace {

bool is_word_char(char32_t c) {
    return (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z') || (c >= U'0' && c <= U'9') || c == U'_';
}

SelectionState::Pos pos_after_text(SelectionState::Pos start, const std::u32string& text) {
    size_t last_nl = text.rfind(U'\n');
    if (last_nl == std::u32string::npos) {
        return {start.line, start.col + static_cast<int>(text.size())};
    }
    int newlines = static_cast<int>(std::count(text.begin(), text.end(), U'\n'));
    return {start.line + newlines, static_cast<int>(text.size() - last_nl - 1)};
}

bool pos_in_half_open_range(SelectionState::Pos p, SelectionState::Pos start, SelectionState::Pos end) {
    bool after_start = (p.line > start.line) || (p.line == start.line && p.col >= start.col);
    bool before_end = (p.line < end.line) || (p.line == end.line && p.col < end.col);
    return after_start && before_end;
}

} // namespace

void TextArea::set_text(const std::string& text) {
    auto decoded = TextHelper::decode_utf8(text);
    lines_.clear();
    size_t start = 0;
    while (true) {
        size_t nl = decoded.find(U'\n', start);
        if (nl == std::u32string::npos) {
            lines_.push_back(decoded.substr(start));
            break;
        }
        lines_.push_back(decoded.substr(start, nl - start));
        start = nl + 1;
    }
    if (lines_.empty()) lines_.push_back(U"");

    selection_ = SelectionState{};
    secondary_cursors_.clear();
    history_.clear();
    scroll_line_ = 0;
    scroll_col_ = 0;
    need_repaint();
}

std::string TextArea::text() const {
    std::u32string joined;
    for (size_t i = 0; i < lines_.size(); ++i) {
        if (i > 0) joined += U'\n';
        joined += lines_[i];
    }
    return TextHelper::encode_utf8(joined);
}

Size TextArea::measure(const Constraints& c) const {
    return c.clamp({std::max(c.min_width, 20), std::max(c.min_height, 3)});
}

void TextArea::arrange(Rect final_rect) {
    bounds_ = final_rect;
    ensure_cursor_visible();
}

void TextArea::paint(Buffer& buffer) const {
    if (bounds_.width <= 0 || bounds_.height <= 0) return;

    bool has_sel = !selection_.empty();
    Pos sel_start = selection_.start();
    Pos sel_end = selection_.end();
    int width = content_width();

    for (int row = 0; row < bounds_.height; ++row) {
        int line_idx = scroll_line_ + row;
        bool line_exists = line_idx >= 0 && static_cast<size_t>(line_idx) < lines_.size();
        const std::u32string empty_line;
        const std::u32string& line_text = line_exists ? lines_[static_cast<size_t>(line_idx)] : empty_line;

        for (int col = 0; col < width; ++col) {
            int char_idx = scroll_col_ + col;
            char32_t ch = (char_idx >= 0 && static_cast<size_t>(char_idx) < line_text.size())
                ? line_text[static_cast<size_t>(char_idx)] : U' ';

            CharStyle cs = style_hook_ ? style_hook_(line_idx, char_idx) : CharStyle{};
            TextStyle style = cs.style;

            if (line_exists) {
                bool in_selection = has_sel && pos_in_half_open_range({line_idx, char_idx}, sel_start, sel_end);
                bool is_cursor = focused_ && !has_sel && line_idx == selection_.cursor.line && char_idx == selection_.cursor.col;
                bool is_secondary_cursor = focused_ && std::any_of(secondary_cursors_.begin(), secondary_cursors_.end(),
                    [&](Pos p) { return p.line == line_idx && p.col == char_idx; });
                if (in_selection || is_cursor) style = style | TextStyle::Reverse;
                // Curseur secondaire visuellement distinct du principal
                // (Reverse+Underline) sans dépendre d'une couleur dédiée.
                if (is_secondary_cursor) style = style | TextStyle::Reverse | TextStyle::Underline;
            }

            buffer.set(bounds_.x + col, bounds_.y + row, Cell{ch, 1, cs.fg, Color::Default(), style});
        }
    }

    if (show_scrollbar_ && bounds_.width > 1) {
        Rect track{bounds_.x + bounds_.width - 1, bounds_.y, 1, bounds_.height};
        draw_vertical_scrollbar(buffer, track, static_cast<int>(lines_.size()), std::max(bounds_.height, 1),
                                 scroll_line_, accent_color_);
    }
}

bool TextArea::on_key(const KeyEvent& e) {
    if (key_intercept_ && key_intercept_(e)) return true;

    bool shift = e.shift;

    if (e.ctrl && e.alt && (e.key == Key::Down || e.key == Key::Up)) {
        Pos p = selection_.cursor;
        int target_line = p.line + (e.key == Key::Down ? 1 : -1);
        if (target_line >= 0 && static_cast<size_t>(target_line) < lines_.size()) {
            int col = std::min(p.col, static_cast<int>(lines_[static_cast<size_t>(target_line)].size()));
            add_cursor_at({target_line, col});
        }
        return true;
    }
    if (e.key == Key::Escape && has_multiple_cursors()) {
        clear_secondary_cursors();
        return true;
    }
    // Toute autre touche de navigation retombe en mode mono-curseur : voir
    // la note multicurseur en tête de fichier.
    bool is_navigation = e.key == Key::Left || e.key == Key::Right || e.key == Key::Up || e.key == Key::Down ||
                          e.key == Key::Home || e.key == Key::End || e.key == Key::PageUp || e.key == Key::PageDown;
    if (is_navigation && has_multiple_cursors()) clear_secondary_cursors();

    switch (e.key) {
        case Key::Left:
            move_cursor(e.ctrl ? move_word_left(selection_.cursor) : move_left(selection_.cursor), shift);
            return true;
        case Key::Right:
            move_cursor(e.ctrl ? move_word_right(selection_.cursor) : move_right(selection_.cursor), shift);
            return true;
        case Key::Up: {
            Pos p = selection_.cursor;
            if (p.line > 0) {
                --p.line;
                p.col = std::min(p.col, static_cast<int>(lines_[static_cast<size_t>(p.line)].size()));
            } else {
                p.col = 0;
            }
            move_cursor(p, shift);
            return true;
        }
        case Key::Down: {
            Pos p = selection_.cursor;
            if (static_cast<size_t>(p.line) + 1 < lines_.size()) {
                ++p.line;
                p.col = std::min(p.col, static_cast<int>(lines_[static_cast<size_t>(p.line)].size()));
            } else {
                p.col = static_cast<int>(lines_[static_cast<size_t>(p.line)].size());
            }
            move_cursor(p, shift);
            return true;
        }
        case Key::Home: {
            Pos p = selection_.cursor;
            p.col = 0;
            move_cursor(p, shift);
            return true;
        }
        case Key::End: {
            Pos p = selection_.cursor;
            p.col = static_cast<int>(lines_[static_cast<size_t>(p.line)].size());
            move_cursor(p, shift);
            return true;
        }
        case Key::PageUp: {
            Pos p = selection_.cursor;
            p.line = std::max(p.line - std::max(bounds_.height, 1), 0);
            p.col = std::min(p.col, static_cast<int>(lines_[static_cast<size_t>(p.line)].size()));
            move_cursor(p, shift);
            return true;
        }
        case Key::PageDown: {
            Pos p = selection_.cursor;
            p.line = std::min(p.line + std::max(bounds_.height, 1), static_cast<int>(lines_.size()) - 1);
            p.col = std::min(p.col, static_cast<int>(lines_[static_cast<size_t>(p.line)].size()));
            move_cursor(p, shift);
            return true;
        }
        case Key::Enter:
            if (read_only_) return false;
            insert_text_at_cursor(U"\n", false);
            return true;
        case Key::Backspace:
            if (read_only_) return false;
            if (has_multiple_cursors()) {
                backspace_at_all_cursors();
            } else if (has_selection()) {
                delete_selection();
            } else {
                Pos p = selection_.cursor;
                Pos before = move_left(p);
                if (before == p) return true;
                std::u32string removed = text_in_range(before, p);
                erase_range(before, p);
                history_.push_delete(before, removed, removed.size() == 1);
                selection_.cursor = before;
                selection_.collapse_to_cursor();
                ensure_cursor_visible();
                notify_changed();
            }
            return true;
        case Key::Delete:
            if (read_only_) return false;
            if (has_multiple_cursors()) {
                delete_at_all_cursors();
            } else if (has_selection()) {
                delete_selection();
            } else {
                Pos p = selection_.cursor;
                Pos after = move_right(p);
                if (after == p) return true;
                std::u32string removed = text_in_range(p, after);
                erase_range(p, after);
                history_.push_delete(p, removed, removed.size() == 1);
                ensure_cursor_visible();
                notify_changed();
            }
            return true;
        case Key::Char:
            if (read_only_) return false;
            if (e.ctrl && (e.codepoint == U'z' || e.codepoint == U'Z')) { undo(); return true; }
            if (e.ctrl && (e.codepoint == U'y' || e.codepoint == U'Y')) { redo(); return true; }
            if (e.ctrl) return false;
            insert_text_at_cursor(std::u32string(1, e.codepoint), true);
            return true;
        default:
            return false;
    }
}

bool TextArea::on_mouse(const MouseEvent& e) {
    if (e.button == MouseEvent::Button::WheelUp) { scroll_lines_by(-3); return true; }
    if (e.button == MouseEvent::Button::WheelDown) { scroll_lines_by(3); return true; }

    bool on_scrollbar_column = show_scrollbar_ && bounds_.width > 1 && e.x == bounds_.x + bounds_.width - 1;
    if (on_scrollbar_column && e.button == MouseEvent::Button::Left &&
        (e.action == MouseEvent::Action::Press || e.action == MouseEvent::Action::Drag)) {
        Rect track{bounds_.x + bounds_.width - 1, bounds_.y, 1, bounds_.height};
        scroll_line_ = scrollbar_hit_to_position(track, static_cast<int>(lines_.size()), std::max(bounds_.height, 1), e.y);
        need_repaint();
        return true;
    }

    if (e.button != MouseEvent::Button::Left || e.action != MouseEvent::Action::Press) return false;
    int line = std::clamp(scroll_line_ + (e.y - bounds_.y), 0, static_cast<int>(lines_.size()) - 1);
    int col = std::clamp(scroll_col_ + (e.x - bounds_.x), 0, static_cast<int>(lines_[static_cast<size_t>(line)].size()));
    // Alt+clic ajoute un curseur secondaire au lieu de déplacer le principal.
    if (e.alt) { add_cursor_at({line, col}); return true; }
    move_cursor({line, col}, e.shift);
    return true;
}

std::string TextArea::selected_text() const {
    if (selection_.empty()) return "";
    return TextHelper::encode_utf8(text_in_range(selection_.start(), selection_.end()));
}

std::string TextArea::cut_selection() {
    std::string result = selected_text();
    delete_selection();
    return result;
}

void TextArea::undo() {
    auto cmd = history_.undo();
    if (!cmd) return;
    apply_edit_command_inverse(*cmd);
    notify_changed();
}

void TextArea::redo() {
    auto cmd = history_.redo();
    if (!cmd) return;
    apply_edit_command_forward(*cmd);
    notify_changed();
}

void TextArea::notify_changed() {
    need_repaint();
    if (on_change_) on_change_();
}

void TextArea::insert_text_at_cursor(std::u32string text, bool allow_coalesce) {
    if (has_multiple_cursors()) {
        insert_text_at_all_cursors(text);
        return;
    }
    if (has_selection()) delete_selection();

    Pos pos = selection_.cursor;
    insert_raw(pos, text);
    Pos new_pos = pos_after_text(pos, text);
    history_.push_insert(pos, std::move(text), allow_coalesce);

    selection_.cursor = new_pos;
    selection_.collapse_to_cursor();
    ensure_cursor_visible();
    notify_changed();
}

namespace {
// Une entrée par curseur (principal ou secondaire) à éditer en une passe
// multicurseur ; `which` vaut -1 pour le principal, sinon l'index dans
// secondary_cursors_. Traité en ordre décroissant (bas du document vers
// le haut) : voir la note multicurseur en tête de TextArea.hpp.
struct CursorRef {
    SelectionState::Pos pos;
    int which;
};

std::vector<CursorRef> collect_cursor_refs(const SelectionState::Pos& primary,
                                            const std::vector<SelectionState::Pos>& secondary) {
    std::vector<CursorRef> refs;
    refs.reserve(secondary.size() + 1);
    refs.push_back({primary, -1});
    for (size_t i = 0; i < secondary.size(); ++i) refs.push_back({secondary[i], static_cast<int>(i)});
    std::sort(refs.begin(), refs.end(), [](const CursorRef& a, const CursorRef& b) {
        return a.pos.line != b.pos.line ? a.pos.line > b.pos.line : a.pos.col > b.pos.col;
    });
    return refs;
}
} // namespace

void TextArea::insert_text_at_all_cursors(const std::u32string& text) {
    auto refs = collect_cursor_refs(selection_.cursor, secondary_cursors_);
    int newline_count = static_cast<int>(std::count(text.begin(), text.end(), U'\n'));

    std::vector<Pos> new_positions(refs.size());
    for (size_t i = 0; i < refs.size(); ++i) {
        Pos pos = refs[i].pos;
        insert_raw(pos, text);
        history_.push_insert(pos, text, false);
        new_positions[i] = pos_after_text(pos, text);
        // Les entrées déjà finalisées sont plus bas dans le document
        // (ordre décroissant) : un saut de ligne inséré ici les décale.
        if (newline_count > 0) {
            for (size_t j = 0; j < i; ++j) {
                if (new_positions[j].line > pos.line) new_positions[j].line += newline_count;
            }
        }
    }
    for (size_t i = 0; i < refs.size(); ++i) {
        if (refs[i].which < 0) selection_.cursor = new_positions[i];
        else secondary_cursors_[static_cast<size_t>(refs[i].which)] = new_positions[i];
    }
    selection_.collapse_to_cursor();
    ensure_cursor_visible();
    notify_changed();
}

void TextArea::backspace_at_all_cursors() {
    auto refs = collect_cursor_refs(selection_.cursor, secondary_cursors_);
    std::vector<Pos> new_positions(refs.size());
    for (size_t i = 0; i < refs.size(); ++i) {
        Pos p = refs[i].pos;
        Pos before = move_left(p);
        if (before == p) { new_positions[i] = p; continue; }
        bool removes_newline = before.line != p.line;
        std::u32string removed = text_in_range(before, p);
        erase_range(before, p);
        history_.push_delete(before, removed, false);
        new_positions[i] = before;
        if (removes_newline) {
            for (size_t j = 0; j < i; ++j) {
                if (new_positions[j].line > p.line) new_positions[j].line -= 1;
            }
        }
    }
    for (size_t i = 0; i < refs.size(); ++i) {
        if (refs[i].which < 0) selection_.cursor = new_positions[i];
        else secondary_cursors_[static_cast<size_t>(refs[i].which)] = new_positions[i];
    }
    selection_.collapse_to_cursor();
    ensure_cursor_visible();
    notify_changed();
}

void TextArea::delete_at_all_cursors() {
    auto refs = collect_cursor_refs(selection_.cursor, secondary_cursors_);
    std::vector<Pos> new_positions(refs.size());
    for (size_t i = 0; i < refs.size(); ++i) {
        Pos p = refs[i].pos;
        Pos after = move_right(p);
        if (after == p) { new_positions[i] = p; continue; }
        bool removes_newline = after.line != p.line;
        std::u32string removed = text_in_range(p, after);
        erase_range(p, after);
        history_.push_delete(p, removed, false);
        new_positions[i] = p;
        if (removes_newline) {
            for (size_t j = 0; j < i; ++j) {
                if (new_positions[j].line > after.line) new_positions[j].line -= 1;
            }
        }
    }
    for (size_t i = 0; i < refs.size(); ++i) {
        if (refs[i].which < 0) selection_.cursor = new_positions[i];
        else secondary_cursors_[static_cast<size_t>(refs[i].which)] = new_positions[i];
    }
    selection_.collapse_to_cursor();
    ensure_cursor_visible();
    notify_changed();
}

void TextArea::add_cursor_at(Pos p) {
    p = clamp_pos(p);
    if (p == selection_.cursor) return;
    for (auto& sc : secondary_cursors_) if (sc == p) return;
    secondary_cursors_.push_back(p);
    need_repaint();
}

void TextArea::clear_secondary_cursors() {
    if (secondary_cursors_.empty()) return;
    secondary_cursors_.clear();
    need_repaint();
}

void TextArea::delete_selection() {
    if (selection_.empty()) return;
    Pos start = selection_.start();
    Pos end = selection_.end();
    std::u32string removed = text_in_range(start, end);
    erase_range(start, end);
    history_.push_delete(start, removed, false); // une sélection supprimée n'est jamais coalescée
    selection_.cursor = start;
    selection_.collapse_to_cursor();
    ensure_cursor_visible();
    notify_changed();
}

void TextArea::erase_range(Pos start, Pos end) {
    if (start.line == end.line) {
        auto& line = lines_[static_cast<size_t>(start.line)];
        line.erase(static_cast<size_t>(start.col), static_cast<size_t>(end.col - start.col));
        return;
    }
    auto& first = lines_[static_cast<size_t>(start.line)];
    auto& last = lines_[static_cast<size_t>(end.line)];
    std::u32string merged = first.substr(0, static_cast<size_t>(start.col)) + last.substr(static_cast<size_t>(end.col));
    lines_.erase(lines_.begin() + start.line, lines_.begin() + end.line + 1);
    lines_.insert(lines_.begin() + start.line, merged);
}

void TextArea::insert_raw(Pos pos, const std::u32string& text) {
    if (text.find(U'\n') == std::u32string::npos) {
        lines_[static_cast<size_t>(pos.line)].insert(static_cast<size_t>(pos.col), text);
        return;
    }

    std::u32string& current = lines_[static_cast<size_t>(pos.line)];
    std::u32string tail = current.substr(static_cast<size_t>(pos.col));
    current.erase(static_cast<size_t>(pos.col));

    std::vector<std::u32string> new_lines;
    size_t start = 0;
    while (true) {
        size_t nl = text.find(U'\n', start);
        if (nl == std::u32string::npos) {
            new_lines.push_back(text.substr(start));
            break;
        }
        new_lines.push_back(text.substr(start, nl - start));
        start = nl + 1;
    }

    current += new_lines.front();
    std::vector<std::u32string> middle(new_lines.begin() + 1, new_lines.end());
    middle.back() += tail;
    lines_.insert(lines_.begin() + pos.line + 1, middle.begin(), middle.end());
}

std::u32string TextArea::text_in_range(Pos start, Pos end) const {
    if (start.line == end.line) {
        return lines_[static_cast<size_t>(start.line)].substr(static_cast<size_t>(start.col), static_cast<size_t>(end.col - start.col));
    }
    std::u32string result = lines_[static_cast<size_t>(start.line)].substr(static_cast<size_t>(start.col));
    for (int l = start.line + 1; l < end.line; ++l) {
        result += U'\n';
        result += lines_[static_cast<size_t>(l)];
    }
    result += U'\n';
    result += lines_[static_cast<size_t>(end.line)].substr(0, static_cast<size_t>(end.col));
    return result;
}

void TextArea::move_cursor(Pos new_pos, bool extend_selection) {
    new_pos = clamp_pos(new_pos);
    selection_.cursor = new_pos;
    if (!extend_selection) selection_.collapse_to_cursor();
    ensure_cursor_visible();
    need_repaint();
}

TextArea::Pos TextArea::clamp_pos(Pos p) const {
    p.line = std::clamp(p.line, 0, static_cast<int>(lines_.size()) - 1);
    p.col = std::clamp(p.col, 0, static_cast<int>(lines_[static_cast<size_t>(p.line)].size()));
    return p;
}

TextArea::Pos TextArea::move_left(Pos p) const {
    if (p.col > 0) return {p.line, p.col - 1};
    if (p.line > 0) return {p.line - 1, static_cast<int>(lines_[static_cast<size_t>(p.line - 1)].size())};
    return p;
}

TextArea::Pos TextArea::move_right(Pos p) const {
    if (static_cast<size_t>(p.col) < lines_[static_cast<size_t>(p.line)].size()) return {p.line, p.col + 1};
    if (static_cast<size_t>(p.line) + 1 < lines_.size()) return {p.line + 1, 0};
    return p;
}

TextArea::Pos TextArea::move_word_left(Pos p) const {
    if (p.col == 0) return move_left(p);
    const auto& line = lines_[static_cast<size_t>(p.line)];
    int col = p.col;
    while (col > 0 && !is_word_char(line[static_cast<size_t>(col - 1)])) --col;
    while (col > 0 && is_word_char(line[static_cast<size_t>(col - 1)])) --col;
    return {p.line, col};
}

TextArea::Pos TextArea::move_word_right(Pos p) const {
    const auto& line = lines_[static_cast<size_t>(p.line)];
    int col = p.col;
    int len = static_cast<int>(line.size());
    if (col >= len) return move_right(p);
    while (col < len && !is_word_char(line[static_cast<size_t>(col)])) ++col;
    while (col < len && is_word_char(line[static_cast<size_t>(col)])) ++col;
    return {p.line, col};
}

void TextArea::ensure_cursor_visible() {
    if (bounds_.height > 0) {
        if (selection_.cursor.line < scroll_line_) scroll_line_ = selection_.cursor.line;
        if (selection_.cursor.line >= scroll_line_ + bounds_.height) scroll_line_ = selection_.cursor.line - bounds_.height + 1;
    }
    int width = content_width();
    if (width > 0) {
        if (selection_.cursor.col < scroll_col_) scroll_col_ = selection_.cursor.col;
        if (selection_.cursor.col >= scroll_col_ + width) scroll_col_ = selection_.cursor.col - width + 1;
    }
}

void TextArea::scroll_lines_by(int delta) {
    int max_scroll = std::max(static_cast<int>(lines_.size()) - std::max(bounds_.height, 1), 0);
    scroll_line_ = std::clamp(scroll_line_ + delta, 0, max_scroll);
    need_repaint();
}

int TextArea::content_width() const {
    return show_scrollbar_ && bounds_.width > 1 ? bounds_.width - 1 : bounds_.width;
}

void TextArea::apply_edit_command_inverse(const EditCommand& cmd) {
    if (cmd.kind == EditCommand::Kind::Insert) {
        Pos end = pos_after_text(cmd.pos, cmd.text);
        erase_range(cmd.pos, end);
        selection_.cursor = cmd.pos;
    } else {
        insert_raw(cmd.pos, cmd.text);
        selection_.cursor = pos_after_text(cmd.pos, cmd.text);
    }
    selection_.collapse_to_cursor();
    ensure_cursor_visible();
}

void TextArea::apply_edit_command_forward(const EditCommand& cmd) {
    if (cmd.kind == EditCommand::Kind::Insert) {
        insert_raw(cmd.pos, cmd.text);
        selection_.cursor = pos_after_text(cmd.pos, cmd.text);
    } else {
        Pos end = pos_after_text(cmd.pos, cmd.text);
        erase_range(cmd.pos, end);
        selection_.cursor = cmd.pos;
    }
    selection_.collapse_to_cursor();
    ensure_cursor_visible();
}

} // namespace tui
