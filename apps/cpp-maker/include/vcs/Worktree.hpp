/* @file vcs/Worktree.hpp
 *
 * Worktrees git (composant « Git Worktree ») : plusieurs copies de travail
 * liées au même dépôt. Lecture/écriture séparées de l'UI. Suppression
 * uniquement via `git worktree remove` (sans --force : git refuse un arbre de
 * travail sale), pour ne jamais perdre de modifications.
 */

#pragma once

#include "vcs/Result.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace vcs {

/// Un worktree lié au dépôt.
struct WorktreeEntry {
    std::string path;   ///< chemin absolu de la copie de travail
    std::string head;   ///< hash court du HEAD
    std::string branch; ///< nom de branche (vide si détaché)
    bool detached = false;
    bool bare = false;
    bool prunable = false;
};

/// Liste les worktrees du dépôt.
std::vector<WorktreeEntry> worktree_list(const fs::path& root);

/// Ajoute un worktree au chemin `path` (dossier inexistant ou vide), sur la
/// branche `branch` (créée si non vide) ou à partir de `base` (vide = HEAD).
OpResult worktree_add(const fs::path& root, const std::string& path,
                      const std::string& branch, const std::string& base);

/// Supprime un worktree (git worktree remove ; échoue s'il est sale).
OpResult worktree_remove(const fs::path& root, const std::string& path);

/// Élague les métadonnées de worktrees disparus (git worktree prune).
OpResult worktree_prune(const fs::path& root);

} // namespace vcs
