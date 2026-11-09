/* @file vcs/Git.hpp
 *
 * Intégration git : état du dépôt rattaché aux cibles du projet, crochet
 * pre-commit installé par cpp-maker et exécution des règles
 * [git.pre_commit.<nom>] déclarées dans project.toml, layer.toml,
 * module.toml et application.toml.
 *
 * Portée d'une règle (fichiers indexés qui la déclenchent) :
 *   project.toml       tous les fichiers indexés du projet
 *   layer.toml         ceux du dossier du layer
 *   module.toml        ceux du dossier du module
 *   application.toml   ceux du dossier de l'application
 * `when` restreint encore la portée à des motifs (relatifs à la racine).
 *
 * Variables : {targets} cibles touchées de la portée (séparées par des
 * virgules), {files} fichiers concernés, {root} racine du projet,
 * {name} nom du projet.
 */
#pragma once

#include "model/Editor.hpp"
#include "model/Workspace.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace vcs {

/// Racine du dépôt git contenant `dir` (nullopt hors dépôt).
std::optional<fs::path> toplevel(const fs::path& dir);
/// Lance git dans `dir` ; renvoie le code de sortie (sortie affichée).
int git(const fs::path& dir, const std::vector<std::string>& args);
/// Lance git et capture sa sortie.
std::optional<std::string> git_capture(const fs::path& dir, const std::vector<std::string>& args);

/// Indexe / désindexe des chemins (absolus) ; message d'erreur, vide si succès.
/// Le désindexage fonctionne aussi dans un dépôt sans commit.
std::string stage(const fs::path& project_root, const std::vector<fs::path>& paths);
std::string unstage(const fs::path& project_root, const std::vector<fs::path>& paths);

/// Fichiers indexés (absolus) situés sous la racine du projet.
std::vector<fs::path> staged_files(const fs::path& project_root);
/// Fichiers suivis (absolus) situés sous la racine du projet.
std::vector<fs::path> tracked_files(const fs::path& project_root);

struct StatusEntry {
    char index = ' ', worktree = ' ';   ///< colonnes XY de « git status --porcelain »
    fs::path path;                      ///< absolu
    [[nodiscard]] bool staged() const { return index != ' ' && index != '?'; }
    [[nodiscard]] bool untracked() const { return index == '?'; }
};

struct Status {
    std::string branch, upstream;
    int ahead = 0, behind = 0;
    std::vector<StatusEntry> entries;
};

Status status(const fs::path& project_root);

/// Cible (id) contenant `file`, sinon « layer:<nom> », sinon « project ».
std::string owner_of(const model::Workspace& ws, const fs::path& file);

// --- Crochet pre-commit ------------------------------------------------------
enum class HookState { Absent, Ours, Foreign };
fs::path hook_path(const fs::path& project_root);
HookState hook_state(const fs::path& project_root);
/// Installe le crochet ; un crochet étranger est conservé en .cpp-maker-backup (avec `force`).
void install_hook(const fs::path& project_root, const fs::path& self, bool force);
void uninstall_hook(const fs::path& project_root);

// --- Règles ----------------------------------------------------------------------
struct RuleRef {
    const model::PreCommitRule* rule = nullptr;
    std::string scope;    ///< « projet », « layer core », « module core.log »…
    fs::path dir;         ///< dossier de la portée (absolu)
    std::string layer;    ///< portée layer
    std::string target;   ///< portée cible
};

/// Toutes les règles, dans l'ordre d'exécution (projet, layers, cibles).
std::vector<RuleRef> all_rules(const model::Workspace& ws);

struct PreCommitOptions {
    bool all = false;          ///< tous les fichiers suivis au lieu des fichiers indexés
    bool dry_run = false;
    bool keep_going = false;   ///< poursuivre après un échec bloquant
    std::vector<std::string> files;   ///< fichiers imposés (relatifs au dossier courant)
    fs::path self;             ///< exécutable cpp-maker pour les règles « run »
    fs::path cwd;
};

/// Exécute les règles concernées ; 0 si aucune règle bloquante n'échoue.
int run_pre_commit(const model::Workspace& ws, const PreCommitOptions& o);

// --- Maintenance des références (rename, remove, doctor) -----------------------
/// Transformation d'une référence : nullopt = inchangée, "" = retirée, sinon nouvelle valeur.
using RefMap = std::function<std::optional<std::string>(const std::string&)>;

/// Références de cibles d'une règle « run » : mots qui suivent la sous-commande
/// (hors options et valeurs d'options), valeurs de -m/--target, listes « a,b ».
std::vector<std::string> run_selectors(const std::string& run);

/// Applique `items` (références de cibles dans run) et `paths` (motifs when,
/// chemins dans command) à toutes les règles du projet, dans `tx`.
/// Une règle dont une liste de cibles ou tous les motifs when sont vidés est
/// supprimée (`drop_emptied`) ou désactivée. Les fichiers situés sous
/// `skip_dir` (cible supprimée) ne sont pas modifiés. Renvoie un compte rendu.
std::vector<std::string> update_rules(model::Transaction& tx, const model::Workspace& ws, const RefMap& items,
                                      const RefMap& paths, bool drop_emptied, const fs::path& skip_dir = {});

/// Diagnostic : références mortes de chaque règle (vide si tout est valide).
std::vector<std::string> stale_rule_refs(const model::Workspace& ws, const model::PreCommitRule& rule);
/// RefMap retirant les références mortes (pour doctor --fix).
RefMap stale_items_map(const model::Workspace& ws);
RefMap stale_paths_map(const model::Workspace& ws);

} // namespace vcs
