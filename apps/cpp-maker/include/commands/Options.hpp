/* @file commands/Options.hpp
 *
 * Options partagées par les commandes de construction.
 *
 * Les positionnels sont des modes (« release », « debug,asan ») ou des
 * cibles (« core.log », « hello », « core ») : un mot qui désigne un mode
 * connu est un mode, les autres sont des cibles.
 */
#pragma once

#include "build/Session.hpp"
#include "commands/Command.hpp"

namespace commands {

enum BuildOpt : unsigned {
    kOptTargets = 1u << 0,    ///< -m/--target (en plus des positionnels)
    kOptToolchain = 1u << 1,  ///< -t, --cc, --cxx
    kOptJobs = 1u << 2,       ///< -j, -k
    kOptVerbose = 1u << 3,    ///< -v, -n
    kOptSanitize = 1u << 4,   ///< --sanitize…
    kOptClean = 1u << 5,      ///< --clean
    kOptTests = 1u << 6,      ///< --tests
    kOptBackend = 1u << 7,    ///< --backend
    kOptAll = 0xffu,
};

void add_build_options(args::Arguments& a, unsigned which);

/// Construit la requête de build ; `positional_targets` : les mots qui ne
/// sont pas des modes sont des cibles (sinon : erreur).
build::Request read_request(Context& ctx, bool positional_targets = true);

/// Cibles demandées (positionnels/-m) résolues ; vide = dossier courant ou tout.
std::vector<std::string> selected_targets(Context& ctx, const build::Request& r);

} // namespace commands
