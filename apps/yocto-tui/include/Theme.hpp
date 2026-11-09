/* @file yocto-tui/Theme.hpp */

#pragma once

#include <tui/core/Color.hpp>
#include <tui/core/TextStyle.hpp>
#include <tui/core/Theme.hpp>
#include <string>

namespace yocto {

// --- Palette planor : violet/magenta sur fond quasi noir ----------------
// (mêmes valeurs que example/code-editor/Theme.hpp, pour que la palette
// reste cohérente entre tous les exemples du module TUI)
inline constexpr tui::Color kFg{225, 225, 232};
inline constexpr tui::Color kBorder{124, 111, 240};
inline constexpr tui::Color kAccent{217, 87, 190};
inline constexpr tui::Color kMuted{130, 130, 140};
inline constexpr tui::Color kBadgeBg{92, 84, 224};
inline constexpr tui::Color kSuccess{110, 200, 140};
inline constexpr tui::Color kError{224, 90, 90};
inline constexpr tui::Color kWarning{224, 180, 90};
inline constexpr tui::Color kInfo{124, 111, 240};
inline constexpr tui::Color kBackground{13, 12, 18};

/// Construit un tui::Theme pré-rempli avec la palette planor. Utilisé par
/// les widgets qui résolvent les rôles sémantiques (ColorRole) plutôt que
/// de référencer directement les constantes ci-dessus - utile si on
/// souhaite un jour ajouter un thème clair alternatif sans toucher aux
/// panneaux.
[[nodiscard]] tui::Theme make_planor_theme();

/// Couleur associée à un code de sortie de commande externe : vert pour
/// un succès (0), rouge sinon. Regroupé ici pour que tous les panneaux
/// (Build, Recipes, Layers…) colorent leurs statuts de la même façon.
[[nodiscard]] tui::Color status_color(int exit_code);

/// Couleur associée à un niveau de log Yocto ("NOTE", "WARNING", "ERROR",
/// "FATAL"). Retourne kMuted pour tout ce qui n'est pas reconnu, pour ne
/// pas surcharger visuellement la sortie de build.
[[nodiscard]] tui::Color log_level_color(const std::string& level);

} // namespace yocto