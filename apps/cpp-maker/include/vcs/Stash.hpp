/* @file vcs/Stash.hpp
 *
 * Remisage git (composant « Git Stash ») : mettre de côté le travail en cours
 * pour y revenir plus tard. Séparé de l'UI. Complément des sauvegardes de
 * cpp-maker (vcs/Backup.hpp) : ici on manipule directement la pile de stash
 * native de git, visible par `git stash list`.
 */

#pragma once

#include "vcs/Result.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace vcs {

/// Une entrée de la pile de stash.
struct StashEntry {
    std::string ref;     ///< « stash@{0} »
    std::string hash;    ///< hash court
    std::string message; ///< message (sans le préfixe « On <branche>: »)
    std::string date;    ///< date (format court)
};

/// Liste la pile de stash (le plus récent d'abord).
std::vector<StashEntry> stash_list(const fs::path& root);

/// Remise le travail en cours (index + arbre + non suivis) avec `message`.
OpResult stash_push(const fs::path& root, const std::string& message);

/// Applique un stash et le retire de la pile (git stash pop).
OpResult stash_pop(const fs::path& root, const std::string& ref);

/// Applique un stash sans le retirer (git stash apply --index).
OpResult stash_apply(const fs::path& root, const std::string& ref);

/// Supprime un stash de la pile (git stash drop).
OpResult stash_drop(const fs::path& root, const std::string& ref);

/// Diff de ce qu'un stash contient (prévisualisation).
std::string stash_show(const fs::path& root, const std::string& ref);

} // namespace vcs
