#pragma once

#include <tui/core/Color.hpp>

namespace ide {

// Palette planor (violet/magenta sur fond quasi noir), partagée par tous
// les panneaux - voir example/ci-dashboard pour l'origine de ces valeurs.
inline constexpr tui::Color kFg{225, 225, 232};
inline constexpr tui::Color kBorder{124, 111, 240};
inline constexpr tui::Color kAccent{217, 87, 190};
inline constexpr tui::Color kMuted{130, 130, 140};
inline constexpr tui::Color kBadgeBg{92, 84, 224};
inline constexpr tui::Color kSuccess{110, 200, 140};
inline constexpr tui::Color kError{224, 90, 90};
inline constexpr tui::Color kWarning{224, 180, 90};

} // namespace ide
