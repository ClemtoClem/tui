#include <tui/widgets/UndoRedoHistory.hpp>

namespace tui {

namespace {
using Pos = SelectionState::Pos;
}

void UndoRedoHistory::push_insert(Pos pos, std::u32string text, bool allow_coalesce) {
    redo_stack_.clear();

    bool is_single_char = text.size() == 1;
    bool is_boundary_char = is_single_char && (text[0] == U' ' || text[0] == U'\n' || text[0] == U'\t');

    if (allow_coalesce && is_single_char && coalescing_open_ && !undo_stack_.empty()) {
        auto& last = undo_stack_.back();
        Pos expected_pos = last.pos;
        expected_pos.col += static_cast<int>(last.text.size());
        if (last.kind == EditCommand::Kind::Insert && last.pos.line == pos.line && expected_pos == pos) {
            last.text += text;
            if (is_boundary_char) coalescing_open_ = false;
            return;
        }
    }

    undo_stack_.push_back({EditCommand::Kind::Insert, pos, std::move(text)});
    coalescing_open_ = allow_coalesce && is_single_char && !is_boundary_char;
}

void UndoRedoHistory::push_delete(Pos pos, std::u32string text, bool allow_coalesce) {
    redo_stack_.clear();

    bool is_single_char = text.size() == 1;

    if (allow_coalesce && is_single_char && coalescing_open_ && !undo_stack_.empty()) {
        auto& last = undo_stack_.back();
        if (last.kind == EditCommand::Kind::Delete && last.pos.line == pos.line) {
            Pos before = last.pos;
            before.col -= 1;
            if (pos == before) { // Backspace : le buffer rétrécit vers la gauche
                last.text = text + last.text;
                last.pos = pos;
                return;
            }
            if (pos == last.pos) { // Touche Suppr : le buffer rétrécit vers la droite
                last.text += text;
                return;
            }
        }
    }

    undo_stack_.push_back({EditCommand::Kind::Delete, pos, std::move(text)});
    coalescing_open_ = allow_coalesce && is_single_char;
}

std::optional<EditCommand> UndoRedoHistory::undo() {
    if (undo_stack_.empty()) return std::nullopt;
    EditCommand cmd = std::move(undo_stack_.back());
    undo_stack_.pop_back();
    redo_stack_.push_back(cmd);
    coalescing_open_ = false;
    return cmd;
}

std::optional<EditCommand> UndoRedoHistory::redo() {
    if (redo_stack_.empty()) return std::nullopt;
    EditCommand cmd = std::move(redo_stack_.back());
    redo_stack_.pop_back();
    undo_stack_.push_back(cmd);
    coalescing_open_ = false;
    return cmd;
}

void UndoRedoHistory::clear() {
    undo_stack_.clear();
    redo_stack_.clear();
    coalescing_open_ = false;
}

} // namespace tui
