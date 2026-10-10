/**
 * @file Theme.hpp
 * @brief Palette de couleurs par rôle sémantique + styles de texte associés.
 */

#pragma once

#include "Color.hpp"
#include "TextStyle.hpp"

#include <array>
#include <cstddef>

namespace tui {

class Theme {
public:
    Theme() { role_colors_.fill(Color::Default()); }

    [[nodiscard]] Color resolve(ColorRole role) const {
        return role_colors_[static_cast<size_t>(role)];
    }

    void set(ColorRole role, Color color) {
        role_colors_[static_cast<size_t>(role)] = color;
    }

    [[nodiscard]] TextStyle focus_style() const { return focus_style_; }
    void set_focus_style(TextStyle style) { focus_style_ = style; }

    [[nodiscard]] static const Theme& dark();
    [[nodiscard]] static const Theme& light();

    /// Palette violet/magenta sur fond quasi noir, inspirée de planor
    /// (github.com/mrusme/planor) : bordures et accents indigo, sélection
    /// en magenta, texte secondaire gris discret.
    [[nodiscard]] static const Theme& planor();

private:
    static constexpr size_t kRoleCount = static_cast<size_t>(ColorRole::Muted) + 1;
    std::array<Color, kRoleCount> role_colors_{};
    TextStyle focus_style_ = TextStyle::Bold;
};

} // namespace tui
