/**
 * @file TextStyle.hpp
 * @brief Attributs de style de texte (gras, souligné, etc.), combinables via `|`.
 */

#pragma once

#include <cstdint>

namespace tui {

enum class TextStyle : uint8_t {
    None = 0,
    Bold = 1 << 0,
    Italic = 1 << 1,
    Underline = 1 << 2,
    Reverse = 1 << 3,
    Blink = 1 << 4,
    Dim = 1 << 5,
    Strikethrough = 1 << 6,
};

[[nodiscard]] constexpr TextStyle operator|(TextStyle a, TextStyle b) {
    return static_cast<TextStyle>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

constexpr TextStyle& operator|=(TextStyle& a, TextStyle b) {
    a = a | b;
    return a;
}

[[nodiscard]] constexpr TextStyle operator&(TextStyle a, TextStyle b) {
    return static_cast<TextStyle>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

[[nodiscard]] constexpr bool has_style(TextStyle flags, TextStyle style) {
    return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(style)) != 0;
}

} // namespace tui
