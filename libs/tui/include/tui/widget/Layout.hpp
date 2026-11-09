/**
 * @file Layout.hpp
 * @brief Contrat de dimensionnement utilisé par les conteneurs linéaires (Vertical/Horizontal).
 */

#pragma once

namespace tui {

/// Comment un enfant se dimensionne le long de l'axe principal d'un
/// conteneur linéaire. Volontairement simple (pas de flexbox complet) :
/// taille fixe, taille intrinsèque (measure()), ou partage pondéré de
/// l'espace restant - suffisant pour tout le catalogue de widgets prévu.
enum class LayoutMode {
    Fixed,   // `value` = taille fixe en cellules sur l'axe principal
    Auto,    // taille = measure() de l'enfant sur l'axe principal
    Stretch, // partage l'espace restant au prorata de `value` (poids)
};

/// Alignement sur l'axe secondaire (perpendiculaire à l'axe principal).
enum class CrossAlign {
    Start,
    Center,
    End,
    Stretch, // l'enfant occupe toute la dimension secondaire disponible
};

/// Alignement horizontal/vertical générique, utilisé par Align mais
/// aussi par les widgets de texte (Label) - vit ici plutôt que dans
/// layout/Align.hpp pour ne pas forcer une dépendance à Container.
enum class HAlign { Start, Center, End, Stretch };
enum class VAlign { Start, Center, End, Stretch };

struct LayoutParams {
    LayoutMode mode = LayoutMode::Auto;
    int value = 0; // taille fixe (Fixed) ou poids de stretch (Stretch) ; ignoré si Auto
    CrossAlign cross_align = CrossAlign::Stretch;

    [[nodiscard]] static constexpr LayoutParams fixed(int size) {
        return {LayoutMode::Fixed, size, CrossAlign::Stretch};
    }
    [[nodiscard]] static constexpr LayoutParams stretch(int weight = 1) {
        return {LayoutMode::Stretch, weight, CrossAlign::Stretch};
    }
    [[nodiscard]] static constexpr LayoutParams auto_size() {
        return {LayoutMode::Auto, 0, CrossAlign::Stretch};
    }
};

} // namespace tui
