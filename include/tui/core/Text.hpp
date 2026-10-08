/**
 * @file Text.hpp
 * @brief Décodage UTF-8, largeur d'affichage Unicode, troncature et wrap.
 *
 * Le framework travaille en interne en `char32_t` (un codepoint par
 * élément) plutôt qu'en octets UTF-8 bruts, pour que le calcul de
 * curseur/largeur/troncature n'ait jamais à re-décoder de l'UTF-8.
 * La largeur d'affichage est calculée par une table de plages statique
 * (pas de dépendance à `wcwidth()` de la libc, dont le comportement
 * varie selon la locale et la plateforme).
 */

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace tui {

struct TextHelper {
    /// Décode une chaîne UTF-8 en séquence de codepoints. Les séquences
    /// invalides sont remplacées par U+FFFD (caractère de remplacement).
    [[nodiscard]] static std::u32string decode_utf8(std::string_view s);

    /// Ré-encode une séquence de codepoints en UTF-8.
    [[nodiscard]] static std::string encode_utf8(std::u32string_view s);

    /// Largeur d'affichage d'un codepoint : 0 (combinant / zéro-largeur /
    /// contrôle), 1 (largeur normale) ou 2 (CJK large / pleine largeur).
    [[nodiscard]] static int codepoint_width(char32_t cp);

    /// Somme des largeurs d'affichage de chaque codepoint de `s`.
    [[nodiscard]] static int display_width(std::u32string_view s);

    /// Tronque `s` pour qu'elle tienne dans `max_width` colonnes,
    /// en respectant les glyphes larges (ne coupe jamais un glyphe en
    /// deux). Si `ellipsis` est non-vide et que la troncature a lieu,
    /// les dernières colonnes sont remplacées par `ellipsis`.
    [[nodiscard]] static std::u32string truncate_to_width(
        std::u32string_view s, int max_width, std::u32string_view ellipsis = U"");

    /// Découpe `s` en lignes d'au plus `width` colonnes d'affichage,
    /// en coupant de préférence aux espaces (word-wrap glouton). Un mot
    /// unique plus large que `width` est coupé de force au milieu.
    [[nodiscard]] static std::vector<std::u32string> wrap_to_width(std::u32string_view s, int width);
};

} // namespace tui
