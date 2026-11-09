/**
 * @file Color.hpp
 * @brief Couleur RGB true-color et rôles sémantiques résolus par le thème.
 */

#pragma once

#include <cstdint>

namespace tui {

/// Couleur RGB true-color (24-bit). `is_default` signifie "couleur du
/// terminal par défaut" (pas de séquence SGR de couleur émise) plutôt
/// qu'une couleur RGB précise — utile pour respecter le thème de
/// l'utilisateur final quand aucune couleur explicite n'est voulue.
struct Color {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    bool is_default = false;

    constexpr Color() : is_default(true) {}
    constexpr Color(uint8_t r_, uint8_t g_, uint8_t b_) : r(r_), g(g_), b(b_), is_default(false) {}

    constexpr bool operator==(const Color&) const = default;

    [[nodiscard]] static constexpr Color Default() { return Color{}; }
    [[nodiscard]] static constexpr Color Black() { return {0, 0, 0}; }
    [[nodiscard]] static constexpr Color White() { return {255, 255, 255}; }
    [[nodiscard]] static constexpr Color Red() { return {220, 50, 47}; }
    [[nodiscard]] static constexpr Color Green() { return {0, 170, 0}; }
    [[nodiscard]] static constexpr Color Blue() { return {38, 139, 210}; }
    [[nodiscard]] static constexpr Color Yellow() { return {181, 137, 0}; }
    [[nodiscard]] static constexpr Color Cyan() { return {42, 161, 152}; }
    [[nodiscard]] static constexpr Color Magenta() { return {211, 54, 130}; }
    [[nodiscard]] static constexpr Color Gray() { return {128, 128, 128}; }
};

/// Rôle sémantique d'une couleur, résolu en Color concrète par le Theme
/// actif. Les widgets référencent des rôles, pas des RGB en dur, pour
/// rester cohérents avec le thème (clair/sombre/custom) de l'application.
enum class ColorRole {
    Background,
    Foreground,
    Border,
    BorderFocused,
    Selection,
    SelectionText,
    Accent,
    Error,
    Warning,
    Success,
    Info,
    Disabled,
    /// Texte secondaire discret (compteurs, sous-titres, indices de
    /// raccourcis) : plus doux que Disabled, qui signale un état
    /// désactivé/non interactif plutôt qu'une simple hiérarchie visuelle.
    Muted,
};

} // namespace tui
