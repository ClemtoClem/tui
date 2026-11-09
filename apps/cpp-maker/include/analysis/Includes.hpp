/* @file analysis/Includes.hpp
 *
 * Analyse statique des #include : détecte les dépendances implicites (une
 * cible inclut l'en-tête public d'un module qu'elle ne déclare pas,
 * directement ou transitivement).
 */
#pragma once

#include "model/Workspace.hpp"

#include <string>
#include <utility>
#include <vector>

namespace analysis {

std::vector<std::string> scan_includes(const fs::path& file);

struct MissingDep {
    std::string target;    ///< cible qui inclut
    std::string module;    ///< module dont l'en-tête est inclus
    std::string header;    ///< exemple d'en-tête concerné
    fs::path file;         ///< fichier qui l'inclut
};

std::vector<MissingDep> detect_missing_deps(const model::Workspace& ws);

} // namespace analysis
