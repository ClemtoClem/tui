/* @file vcs/Operations.hpp
 *
 * Opérations Git de base (composant « Git Operations ») : checkout, branches,
 * tags, cherry-pick, revert, merge, rebase, reset. Séparées de l'UI et du
 * modèle d'historique ; chaque opération retourne un résultat (succès/erreur).
 *
 * Toutes les commandes sont exécutées avec des arguments séparés (aucun shell),
 * via proc::capture : protection contre l'injection, capture stdout+stderr,
 * gestion des codes de retour.
 */

#pragma once

#include "vcs/Result.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace vcs {

// --- Commits -----------------------------------------------------------------
/// Checkout d'un commit (HEAD détaché) ou d'une référence.
OpResult checkout(const fs::path& root, const std::string& ref);
/// Applique un commit par-dessus HEAD.
OpResult cherry_pick(const fs::path& root, const std::string& commit);
/// Crée un commit annulant `commit`.
OpResult revert(const fs::path& root, const std::string& commit);
/// Réinitialise la branche courante sur `commit` (soft par défaut, --hard sinon).
OpResult reset(const fs::path& root, const std::string& commit, bool hard);
/// Modifie le message du dernier commit (git commit --amend -m).
OpResult amend_message(const fs::path& root, const std::string& message);

// --- Branches ----------------------------------------------------------------
/// Crée `name` sur `start` (vide = HEAD) ; bascule dessus si `checkout_new`.
OpResult create_branch(const fs::path& root, const std::string& name, const std::string& start, bool checkout_new);
OpResult delete_branch(const fs::path& root, const std::string& name, bool force = false);
/// Renomme une branche locale.
OpResult rename_branch(const fs::path& root, const std::string& old_name, const std::string& new_name);
OpResult merge(const fs::path& root, const std::string& branch);
OpResult rebase(const fs::path& root, const std::string& branch);
/// Noms des branches locales (sans le préfixe « * »).
std::vector<std::string> local_branches(const fs::path& root);

// --- Tags --------------------------------------------------------------------
/// Tag léger (`annotated` = false) ou annoté (`annotated` = true).
OpResult create_tag(const fs::path& root, const std::string& name, const std::string& message,
                    const std::string& commit, bool annotated);
OpResult delete_tag(const fs::path& root, const std::string& name);
/// Renomme un tag local.
OpResult rename_tag(const fs::path& root, const std::string& old_name, const std::string& new_name);
std::vector<std::string> tags(const fs::path& root);

// --- Remotes / publication ----------------------------------------------------
/// Branche amont de HEAD (« origin/main ») ou vide si aucune.
std::string upstream(const fs::path& root);
/// Noms des remotes configurés.
std::vector<std::string> remotes(const fs::path& root);
/// Branches distantes (« origin/main », …).
std::vector<std::string> remote_branches(const fs::path& root);
/// Pousse une branche ou un tag vers le premier remote.
OpResult push(const fs::path& root, const std::string& ref);
/// Supprime une branche distante (git push <remote> --delete).
OpResult push_delete_branch(const fs::path& root, const std::string& branch);
/// Supprime un tag distant.
OpResult push_delete_tag(const fs::path& root, const std::string& tag);

/// URL d'un remote (vide si absent).
std::string remote_url(const fs::path& root, const std::string& name);
/// Ajoute un remote.
OpResult add_remote(const fs::path& root, const std::string& name, const std::string& url);
/// Supprime un remote.
OpResult remove_remote(const fs::path& root, const std::string& name);
/// Renomme un remote.
OpResult rename_remote(const fs::path& root, const std::string& old_name, const std::string& new_name);
/// Modifie l'URL d'un remote.
OpResult set_remote_url(const fs::path& root, const std::string& name, const std::string& url);
/// Récupère les objets du remote (git fetch).
OpResult fetch(const fs::path& root);
/// Récupère et supprime les références distantes disparues (git fetch --prune).
OpResult prune(const fs::path& root);
/// Récupère et fusionne (git pull).
OpResult pull(const fs::path& root);
/// Associe une branche locale à son amont (git branch --set-upstream-to).
OpResult set_upstream(const fs::path& root, const std::string& branch, const std::string& upstream);

/// Branches locales fusionnées dans HEAD (git branch --merged).
std::vector<std::string> merged_branches(const fs::path& root);
/// Vrai si la branche locale a un amont configuré.
bool branch_has_upstream(const fs::path& root, const std::string& branch);

// --- Existence (détection de noms déjà utilisés) ------------------------------
bool branch_exists(const fs::path& root, const std::string& name);
bool tag_exists(const fs::path& root, const std::string& name);

// --- Conflits -----------------------------------------------------------------
/// Fichiers en conflit (git diff --name-only --diff-filter=U).
std::vector<std::string> conflicted_files(const fs::path& root);
/// Opération en cours : « merge », « rebase », « cherry-pick », « revert », ou vide.
std::string operation_in_progress(const fs::path& root);
/// Résout un conflit en prenant la version locale (--ours).
OpResult resolve_ours(const fs::path& root, const std::string& file);
/// Résout un conflit en prenant la version entrante (--theirs).
OpResult resolve_theirs(const fs::path& root, const std::string& file);
/// Marque un conflit comme résolu (git add).
OpResult mark_resolved(const fs::path& root, const std::string& file);
/// Annule l'opération en cours (merge/rebase/cherry-pick/revert --abort).
OpResult abort_operation(const fs::path& root);

// --- Reflog -------------------------------------------------------------------
/// Une entrée du reflog (ancien HEAD).
struct ReflogEntry {
    std::string selector; ///< « HEAD@{0} »
    std::string hash;     ///< hash court
    std::string message;  ///< message du reflog
    std::string date;     ///< date
};
/// Reflog de HEAD (les plus récents d'abord).
std::vector<ReflogEntry> reflog(const fs::path& root, int max_count = 200);

// --- Réécriture d'historique (squash / fixup / drop) --------------------------
/// Vrai si `ancestor` est un ancêtre de `descendant` (git merge-base --is-ancestor).
bool is_ancestor(const fs::path& root, const std::string& ancestor, const std::string& descendant);
/// Première ligne du message du commit pointé par HEAD (vide si aucun).
std::string head_subject(const fs::path& root);
/// Première ligne du message d'un commit (vide si introuvable).
std::string commit_subject(const fs::path& root, const std::string& commit);
/// Commit (git commit -m).
OpResult commit(const fs::path& root, const std::string& message);
/// Squash : réinitialise (soft) sur `commit` puis valide avec `message`.
/// Tous les commits situés après `commit` sont fusionnés en un seul.
OpResult squash_onto(const fs::path& root, const std::string& commit, const std::string& message);
/// Fixup : fusionne les modifications indexées dans le dernier commit
/// (sans changer son message). `git commit --amend --no-edit`.
OpResult fixup_last(const fs::path& root);
/// Drop : supprime le dernier commit en conservant les modifications (soft).
OpResult drop_last(const fs::path& root);

/// Liste des commits de `base..HEAD` (le plus ancien d'abord), via rev-list.
std::vector<std::string> commits_in_range(const fs::path& root, const std::string& base);

/// Supprime un commit non-fusion de la branche courante (git rebase --onto
/// <commit>^ <commit>). Refuse si le commit est HEAD, un merge, n'est pas un
/// ancêtre de HEAD, ou si l'arbre de travail est sale.
OpResult drop_commit(const fs::path& root, const std::string& commit);

/// Réordonne les commits de `base..HEAD` selon `order` (hashes complets, du
/// plus ancien au plus récent). `order` doit être une permutation exacte de la
/// plage (vérifié). Refuse si l'arbre de travail est sale.
OpResult reorder_commits(const fs::path& root, const std::string& base,
                         const std::vector<std::string>& order);

} // namespace vcs
