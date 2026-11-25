/* @file vcs/Diff.hpp
 *
 * Calcul et récupération des différences (composant « Git Diff Engine »).
 * Lecture seule pour `diff`/`show_commit` ; `apply_hunks` modifie l'index
 * (staging partiel par hunk).
 */

#pragma once

#include "vcs/Result.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace vcs {

/// Diff unifié entre deux références (`from`/`to` : commits, branches, tags,
/// ou chaînes vides). `--stat --patch`.
std::string diff(const fs::path& root, const std::string& from, const std::string& to);

/// Diff du travail courant (non indexé) ; `staged` = index ↔ HEAD.
std::string diff_working(const fs::path& root, bool staged);

/// `git show` d'un commit : métadonnées + stat + patch.
std::string show_commit(const fs::path& root, const std::string& commit);

/// Diff d'un fichier (non indexé) ou de l'index (staged).
std::string file_diff(const fs::path& root, const std::string& file, bool staged);

/// Un hunk d'un diff unifié.
struct Hunk {
    std::string header; ///< « @@ -a,b +c,d @@ … »
    std::string body;   ///< lignes (contexte / + / −)
};

/// Diff d'un fichier découpé en hunks.
struct FilePatch {
    std::string header; ///< en-tête de fichier (diff --git, index, ---, +++).
    std::vector<Hunk> hunks;
};

/// Découpe le diff d'un fichier en hunks.
FilePatch diff_hunks(const fs::path& root, const std::string& file, bool staged);

/// Applique une sélection de hunks à l'index (`git apply --cached`). `reverse`
/// pour désindexer. `selected` = indices des hunks à appliquer.
OpResult apply_hunks(const fs::path& root, const FilePatch& patch,
                     const std::vector<size_t>& selected, bool reverse);

} // namespace vcs

