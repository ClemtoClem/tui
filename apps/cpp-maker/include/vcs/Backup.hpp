/* @file vcs/Backup.hpp
 *
 * Sauvegarde et restauration du travail en cours (composant « Git Backup
 * Manager »). Une branche git ne stocke que des commits : pour préserver les
 * modifications NON commitées (indexées, non indexées, fichiers non suivis),
 * on combine :
 *  - `git stash push --include-untracked` (commits dédiés : index + arbre + non suivis) ;
 *  - une référence privée `refs/cpp-maker/backups/<id>` par sauvegarde, qui
 *    pointe vers le commit de stash et le protège du garbage collector (C.5).
 *
 * Sécurité : `restore` utilise `git stash apply --index` (préserve l'état
 * indexé/non indexé) et ne supprime jamais la sauvegarde ; en cas de conflit,
 * git refuse l'apply sans écraser les modifications locales plus récentes.
 */

#pragma once

#include "vcs/Result.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace vcs {

/// Une sauvegarde (référence privée + métadonnées).
struct BackupEntry {
    std::string id;      ///< identifiant court (dernier composant de la référence)
    std::string ref;     ///< référence complète (refs/cpp-maker/backups/<id>)
    std::string branch;  ///< branche d'origine
    std::string message; ///< message de la sauvegarde
    std::string date;    ///< date (format court)
};

/// Vrai si le dépôt a des modifications (indexées, non indexées ou non suivies).
bool working_tree_dirty(const fs::path& root);

/// Capture le travail en cours dans une nouvelle sauvegarde (index + arbre +
/// non suivis), et crée la référence privée. `include_ignored` ajoute les
/// fichiers ignorés (--all). Message d'erreur vide = succès.
OpResult backup_capture(const fs::path& root, bool include_ignored = false);

/// Liste les sauvegardes (la plus récente d'abord).
std::vector<BackupEntry> backup_list(const fs::path& root);

/// Diff de ce que la restauration apporterait (prévisualisation, C.3).
std::string backup_preview(const fs::path& root, const std::string& ref);

/// Restaure une sauvegarde (index + arbre + non suivis). La sauvegarde reste
/// en place (drop explicite requis). Échec sans écrasement en cas de conflit.
OpResult backup_restore(const fs::path& root, const std::string& ref);

/// Supprime une sauvegarde (référence privée + entrée de stash si présente).
OpResult backup_drop(const fs::path& root, const std::string& ref);

} // namespace vcs
