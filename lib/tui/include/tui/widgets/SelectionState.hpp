/**
 * @file SelectionState.hpp
 * @brief Position et sélection dans un buffer multi-ligne (ligne/colonne en codepoints).
 */

#pragma once

namespace tui {

struct SelectionState {
    struct Pos {
        int line = 0;
        int col = 0;

        constexpr bool operator==(const Pos&) const = default;
        constexpr bool operator<(const Pos& other) const {
            return line != other.line ? line < other.line : col < other.col;
        }
    };

    Pos anchor;
    Pos cursor;

    [[nodiscard]] bool empty() const { return anchor == cursor; }
    [[nodiscard]] Pos start() const { return anchor < cursor ? anchor : cursor; }
    [[nodiscard]] Pos end() const { return anchor < cursor ? cursor : anchor; }
    void collapse_to_cursor() { anchor = cursor; }
};

} // namespace tui
