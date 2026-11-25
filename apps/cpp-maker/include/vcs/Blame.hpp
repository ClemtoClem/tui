/* @file vcs/Blame.hpp
 *
 * Annotation « blame » : qui a modifié chaque ligne d'un fichier (composant
 * « Git Blame »). Lecture seule (git blame), séparé de l'UI.
 */

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace vcs {

/// Une ligne annotée du fichier.
struct BlameLine {
    std::string hash;    ///< hash court du commit (8 hex)
    std::string author;  ///< nom de l'auteur
    std::string date;    ///< date (AAAA-MM-JJ)
    std::string content; ///< contenu de la ligne
};

/// Annotation ligne par ligne d'un fichier (git blame --porcelain).
/// Vide si le fichier est introuvable ou non suivi.
std::vector<BlameLine> blame(const fs::path& root, const std::string& file);

} // namespace vcs
