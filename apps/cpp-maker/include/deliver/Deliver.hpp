/* @file deliver/Deliver.hpp
 *
 * Livraison : staging des artefacts installables (bin/, lib/, include/,
 * lib/pkgconfig/, share/doc/), archives (tar.gz, tar.xz, zip, deb),
 * installation locale et déploiement (rsync, scp, copy, script).
 */
#pragma once

#include "build/Session.hpp"

#include <string>
#include <vector>

namespace deliver {

bool mode_has_debug(const model::ModeConfig& m);

/// Copie les artefacts de la session dans `dest` ; renvoie les fichiers copiés.
std::vector<fs::path> stage(const build::Session& s, const fs::path& dest, bool strip);

std::string package_basename(const model::Workspace& ws, const toolchain::Toolchain& t);

/// Produit les paquets ; `stage_out` reçoit le dossier de staging.
std::vector<fs::path> make_packages(const build::Session& s, const std::vector<std::string>& formats, bool strip,
                                    bool verbose, fs::path* stage_out = nullptr);

struct DeployOptions {
    bool dry_run = false;
    bool verbose = false;
};

/// Déploie la session (déjà construite) vers la cible `d`.
int deploy(const build::Session& s, const model::DeployConfig& d, const DeployOptions& o);

} // namespace deliver
