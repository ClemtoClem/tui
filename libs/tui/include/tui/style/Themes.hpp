/**
 * @file style/Themes.hpp
 * @brief Thèmes de couleurs à la Textual : une poignée de couleurs de base
 * (primary, secondary, accent, warning, error, success, foreground,
 * background, surface, panel) d'où sont générées les variables CSS
 * ($primary, $primary-lighten-1…3, $primary-darken-1…3, $text,
 * $text-muted, $primary-muted, $border, $footer-key-foreground…).
 */

#pragma once

#include "../core/Color.hpp"
#include "../core/Theme.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace tui::themes {

struct ThemeSpec {
    std::string name;
    Color primary;
    std::optional<Color> secondary, accent, warning, error, success;
    std::optional<Color> foreground, background, surface, panel, boost;
    bool dark = true;
    double luminosity_spread = 0.15;
    /// Variables supplémentaires ou surchargées (valeurs CSS brutes).
    std::map<std::string, std::string> variables;
};

/// Thèmes intégrés (textual-dark, textual-light, nord, gruvbox, dracula,
/// tokyo-night, monokai, flexoki, catppuccin-*, solarized-*, rose-pine*,
/// atom-one-*, planor).
[[nodiscard]] const std::vector<ThemeSpec>& builtin();
[[nodiscard]] std::vector<std::string> names();
/// Thème intégré ou enregistré ; nullptr si inconnu.
[[nodiscard]] const ThemeSpec* find(const std::string& name);
/// Ajoute (ou remplace) un thème utilisateur.
void register_theme(ThemeSpec spec);

/// Variables CSS générées pour un thème (valeurs « #RRGGBB »…).
[[nodiscard]] std::map<std::string, std::string> generate_variables(const ThemeSpec& spec);
/// Palette par rôle (tui::Theme historique) dérivée du thème.
[[nodiscard]] Theme to_role_theme(const ThemeSpec& spec);

} // namespace tui::themes
