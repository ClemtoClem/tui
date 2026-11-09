/**
 * @file style/Style.hpp
 * @brief Style calculé d'un widget (résultat de la cascade CSS).
 *
 * Chaque propriété est optionnelle : absente, le widget garde son rendu
 * propre. Les couleurs « par défaut » qu'un widget peint héritent de
 * `color` / `background` (voir Widget::render), si bien que tous les
 * widgets profitent des thèmes sans code spécifique.
 */

#pragma once

#include "../core/Color.hpp"
#include "../core/TextStyle.hpp"
#include "../widget/BoxFrame.hpp"

#include <map>
#include <optional>
#include <string>

namespace tui {

struct Spacing {
    int top = 0, right = 0, bottom = 0, left = 0;
    [[nodiscard]] constexpr bool operator==(const Spacing&) const = default;
};

/// Dimension CSS : nombre de cellules, pourcentage du parent ou « auto ».
struct Dimension {
    enum class Unit { Cells, Percent, Auto } unit = Unit::Auto;
    int value = 0;
    [[nodiscard]] constexpr bool operator==(const Dimension&) const = default;
    /// Résout la dimension pour un espace disponible ; nullopt si auto.
    [[nodiscard]] std::optional<int> resolve(int available) const {
        if (unit == Unit::Cells) return value;
        if (unit == Unit::Percent && available > 0 && available < (1 << 20)) return available * value / 100;
        return std::nullopt;
    }
};

struct ComputedStyle {
    std::optional<Color> color, background;
    /// « color: auto [N%] » : texte contrasté calculé sur le fond de chaque cellule.
    std::optional<double> color_auto;
    std::optional<TextStyle> text_style;
    std::optional<BorderStyle> border;   ///< absent ou BorderStyle::None : pas de bordure
    std::optional<Color> border_color, border_title_color;
    std::optional<TextStyle> border_title_style;
    std::optional<Spacing> padding;
    std::optional<Dimension> width, height;
    std::optional<int> min_width, max_width, min_height, max_height;
    std::optional<bool> display;         ///< false = display: none
    /// Propriétés propres à certains widgets (« text-align », « accent-color »…),
    /// valeurs déjà résolues (variables substituées).
    std::map<std::string, std::string> extra;

    [[nodiscard]] bool has_border() const { return border && *border != BorderStyle::None; }
    [[nodiscard]] Spacing insets() const {
        Spacing s = padding.value_or(Spacing{});
        if (has_border()) {
            ++s.top;
            ++s.right;
            ++s.bottom;
            ++s.left;
        }
        return s;
    }
    /// Valeur brute d'une propriété « extra » (vide si absente).
    [[nodiscard]] std::string extra_value(const std::string& name) const {
        auto it = extra.find(name);
        return it == extra.end() ? std::string() : it->second;
    }
    /// Couleur d'une propriété « extra » (nullopt si absente ou invalide).
    [[nodiscard]] std::optional<Color> extra_color(const std::string& name) const;
    /// Style de texte d'une propriété « extra » (nullopt si absente).
    [[nodiscard]] std::optional<TextStyle> extra_text_style(const std::string& name) const;
    [[nodiscard]] bool empty() const {
        return !color && !background && !color_auto && !text_style && !border && !border_color &&
               !border_title_color && !border_title_style && !padding && !width && !height && !min_width &&
               !max_width && !min_height && !max_height && !display && extra.empty();
    }
    /// Applique par-dessus `*this` les propriétés définies dans `o`.
    void merge(const ComputedStyle& o);
};

} // namespace tui
