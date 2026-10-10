/**
 * @file UndoRedoHistory.hpp
 * @brief Pile de commandes d'édition (Insert/Delete) avec coalescing des frappes consécutives.
 *
 * Stocke des commandes, pas des snapshots complets du buffer (mémoire
 * bornée). Ne connaît rien du buffer de TextArea : undo()/redo()
 * retournent la commande à inverser/rejouer, TextArea l'applique.
 *
 * Coalescing : des insertions d'un seul caractère consécutives et
 * contiguës fusionnent dans la même commande tant qu'aucune "frontière"
 * (espace, saut de ligne, action non-frappe comme un undo/collage)
 * n'a été franchie - ni frappe-par-frappe (inutilisable), ni un seul
 * énorme undo (tout aussi inutilisable).
 */

#pragma once

#include "SelectionState.hpp"

#include <optional>
#include <string>
#include <vector>

namespace tui {

struct EditCommand {
    enum class Kind { Insert, Delete };
    Kind kind;
    SelectionState::Pos pos;
    std::u32string text;
};

class UndoRedoHistory {
public:
    void push_insert(SelectionState::Pos pos, std::u32string text, bool allow_coalesce);
    void push_delete(SelectionState::Pos pos, std::u32string text, bool allow_coalesce);

    [[nodiscard]] bool can_undo() const { return !undo_stack_.empty(); }
    [[nodiscard]] bool can_redo() const { return !redo_stack_.empty(); }

    /// Retourne la commande à inverser ; ne modifie pas le buffer lui-même.
    std::optional<EditCommand> undo();
    std::optional<EditCommand> redo();

    void clear();

private:
    std::vector<EditCommand> undo_stack_;
    std::vector<EditCommand> redo_stack_;
    bool coalescing_open_ = false;
};

} // namespace tui
