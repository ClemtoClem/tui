/* @file analysis/Includes.hpp
 *
 * Analyse statique des dépendances d'une cible : détecte les dépendances
 * implicites (une cible inclut l'en-tête public — ou importe le module C++ —
 * d'un module qu'elle ne déclare pas, directement ou transitivement).
 *
 * Deux familles de références sont reconnues :
 *   - les directives #include « <…> » ou « "…" » ;
 *   - les importations de modules C++20/C++23 : « import x.y; » et
 *     « export import x.y; » (les unités d'en-tête « import <…>; » et
 *     « import "…"; » sont ignorées).
 *
 * Un en-tête public peut être référencé :
 *   - par son chemin complet (« core/log.hpp ») ;
 *   - par un chemin qualifié du layer (« layer/core/log.hpp »), qui lève
 *     l'ambiguïté quand le même nom de module existe dans plusieurs layers ;
 *   - par le raccourci du fichier principal (« log ») si le module possède
 *     un en-tête portant son nom (include/<nom>/<nom>.hpp) ;
 *   - par ce même raccourci qualifié (« layer/log »).
 */
#pragma once

#include "model/Workspace.hpp"

#include <string>
#include <utility>
#include <vector>

namespace analysis {

/// Chemins référencés par les directives #include (sans les délimiteurs).
std::vector<std::string> scan_includes(const fs::path& file);

/// Noms de modules C++ importés : « import x.y; » et « export import x.y; ».
/// Les unités d'en-tête (« import <…>; », « import "…"; ») sont ignorées.
std::vector<std::string> scan_imports(const fs::path& file);

/// Noms de modules C++ déclarés : « export module x.y; » et « module x.y; »
/// (le fragment global « module; » et « export module; » sont ignorés).
std::vector<std::string> scan_module_decls(const fs::path& file);

struct MissingDep {
    std::string target;    ///< cible qui inclut / importe
    std::string module;    ///< module à déclarer en dépendance (vide si ambigu)
    std::vector<std::string> alternatives;   ///< modules possibles (ambiguïté)
    std::string header;    ///< en-tête ou nom de module concerné
    fs::path file;         ///< fichier qui inclut / importe
    bool via_import = false;   ///< détecté via « import » (sinon #include)

    [[nodiscard]] bool ambiguous() const { return module.empty() && !alternatives.empty(); }
};

std::vector<MissingDep> detect_missing_deps(const model::Workspace& ws);

} // namespace analysis
