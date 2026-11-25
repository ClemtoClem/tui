/* @file vcs/History.hpp
 *
 * Historique des commits d'un dépôt : modèle du graphe de commits et
 * récupération depuis git (composant « Git History »).
 *
 * Séparé de l'UI (ui/GitGraph.hpp) et des opérations Git : ce composant ne
 * fait que lire l'historique (git log) et le modéliser. La récupération se
 * fait en UNE seule invocation de git (format %x01) pour rester efficace sur
 * des dépôts de plusieurs milliers de commits.
 */

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace vcs {

/// Un commit de l'historique, avec ses références.
struct CommitRef {
    std::string hash;      ///< hash complet (40 hex)
    std::string short_hash;///< hash court (git %h)
    std::string subject;   ///< première ligne du message
    std::string author;    ///< nom de l'auteur
    std::string date;      ///< date (format court %ad)
    std::vector<std::string> parents;   ///< hashes complets des parents
    std::vector<std::string> branches;  ///< branches locales/distantes pointant ici
    std::vector<std::string> tags;      ///< tags pointant ici
    bool head = false;                  ///< HEAD pointe sur ce commit
    std::string head_ref;               ///< branche pointée par HEAD (vide si détaché)

    [[nodiscard]] bool is_merge() const { return parents.size() > 1; }
};

/// Historique complet (borné) + position de HEAD.
struct History {
    std::vector<CommitRef> commits;
    std::string branch; ///< branche courante (vide si détachée)
    std::string head;   ///< hash court de HEAD
    std::string error;  ///< message d'erreur si la récupération a échoué
};

/// Détails complets d'un commit (pour l'interface).
struct CommitDetails {
    std::string hash, short_hash;
    std::string subject, body;      ///< message (titre + corps)
    std::string author_name, author_email, author_date;
    std::string committer_name, committer_email, committer_date;
    std::vector<std::string> parents;   ///< hashes complets
    std::vector<std::string> children;  ///< hashes complets
    std::vector<std::string> branches;  ///< branches pointant ici
    std::vector<std::string> tags;      ///< tags pointant ici
    bool head = false;
};

/// Récupère l'historique de la racine `root` (déjà un dépôt git).
/// `max_count` borne le nombre de commits (0 = 1000 par défaut).
History history(const fs::path& root, int max_count = 1000);

/// Détails complets d'un commit (message entier, auteur/committer, dates,
/// parents/enfants, branches/tags).
CommitDetails commit_details(const fs::path& root, const std::string& commit);

} // namespace vcs
