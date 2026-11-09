/**
 * @file style/ColorUtil.hpp
 * @brief Opérations sur les couleurs pour les feuilles de style : analyse
 * (« #rgb », « #rrggbb », « rgb(r, g, b) », noms CSS usuels), mélange,
 * éclaircissement en HSL et couleur de texte contrastée.
 */

#pragma once

#include "../core/Color.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

namespace tui::color {

/// Mélange linéaire : t = 0 → a, t = 1 → b.
[[nodiscard]] inline Color blend(Color a, Color b, double t) {
    if (a.is_default) return b;
    if (b.is_default) return a;
    t = std::clamp(t, 0.0, 1.0);
    auto mix = [t](uint8_t x, uint8_t y) {
        return static_cast<uint8_t>(std::lround(x + (static_cast<double>(y) - x) * t));
    };
    return {mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b)};
}

/// Luminance relative (0 = noir, 1 = blanc).
[[nodiscard]] inline double luminance(Color c) {
    if (c.is_default) return 0.0;
    return (0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b) / 255.0;
}

/// Éclaircit (delta > 0) ou assombrit (delta < 0) en luminosité HSL.
[[nodiscard]] inline Color lighten(Color c, double delta) {
    if (c.is_default || delta == 0.0) return c;
    double r = c.r / 255.0, g = c.g / 255.0, b = c.b / 255.0;
    double mx = std::max({r, g, b}), mn = std::min({r, g, b});
    double h = 0, s = 0, l = (mx + mn) / 2;
    if (mx != mn) {
        double d = mx - mn;
        s = l > 0.5 ? d / (2 - mx - mn) : d / (mx + mn);
        if (mx == r) h = (g - b) / d + (g < b ? 6 : 0);
        else if (mx == g) h = (b - r) / d + 2;
        else h = (r - g) / d + 4;
        h /= 6;
    }
    l = std::clamp(l + delta, 0.0, 1.0);
    auto hue = [](double p, double q, double t) {
        if (t < 0) t += 1;
        if (t > 1) t -= 1;
        if (t < 1.0 / 6) return p + (q - p) * 6 * t;
        if (t < 0.5) return q;
        if (t < 2.0 / 3) return p + (q - p) * (2.0 / 3 - t) * 6;
        return p;
    };
    double nr, ng, nb;
    if (s == 0) nr = ng = nb = l;
    else {
        double q = l < 0.5 ? l * (1 + s) : l + s - l * s, p = 2 * l - q;
        nr = hue(p, q, h + 1.0 / 3);
        ng = hue(p, q, h);
        nb = hue(p, q, h - 1.0 / 3);
    }
    auto to8 = [](double v) { return static_cast<uint8_t>(std::lround(std::clamp(v, 0.0, 1.0) * 255)); };
    return {to8(nr), to8(ng), to8(nb)};
}

/// Texte lisible sur `background` (presque blanc ou presque noir).
[[nodiscard]] inline Color contrast_text(Color background) {
    return luminance(background) > 0.5 ? Color{12, 12, 12} : Color{240, 240, 240};
}

[[nodiscard]] inline std::string to_hex(Color c) {
    if (c.is_default) return "default";
    char buf[8];
    std::snprintf(buf, sizeof buf, "#%02X%02X%02X", c.r, c.g, c.b);
    return buf;
}

/// Analyse une couleur littérale ; nullopt si le texte n'en est pas une.
/// « default », « auto », « transparent » → Color::Default().
[[nodiscard]] inline std::optional<Color> parse(std::string_view text) {
    std::string s;
    for (char ch : text) s += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    if (s.empty()) return std::nullopt;
    auto hexval = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
        return -1;
    };
    if (s[0] == '#') {
        std::string h = s.substr(1);
        if (h.size() == 3 || h.size() == 4) {
            int r = hexval(h[0]), g = hexval(h[1]), b = hexval(h[2]);
            if (r < 0 || g < 0 || b < 0) return std::nullopt;
            return Color{static_cast<uint8_t>(r * 17), static_cast<uint8_t>(g * 17), static_cast<uint8_t>(b * 17)};
        }
        if (h.size() == 6 || h.size() == 8) {
            int v[6];
            for (int i = 0; i < 6; ++i)
                if ((v[i] = hexval(h[static_cast<size_t>(i)])) < 0) return std::nullopt;
            return Color{static_cast<uint8_t>(v[0] * 16 + v[1]), static_cast<uint8_t>(v[2] * 16 + v[3]),
                         static_cast<uint8_t>(v[4] * 16 + v[5])};
        }
        return std::nullopt;
    }
    if (s.starts_with("rgb(") && s.back() == ')') {
        int r = 0, g = 0, b = 0;
        if (std::sscanf(s.c_str(), "rgb(%d,%d,%d)", &r, &g, &b) == 3 ||
            std::sscanf(s.c_str(), "rgb(%d, %d, %d)", &r, &g, &b) == 3)
            return Color{static_cast<uint8_t>(std::clamp(r, 0, 255)), static_cast<uint8_t>(std::clamp(g, 0, 255)),
                         static_cast<uint8_t>(std::clamp(b, 0, 255))};
        return std::nullopt;
    }
    struct Named { const char* name; Color c; };
    static constexpr Named names[] = {
        {"default", Color::Default()}, {"auto", Color::Default()}, {"transparent", Color::Default()},
        {"black", {0, 0, 0}}, {"white", {255, 255, 255}}, {"red", {220, 50, 47}}, {"green", {0, 170, 0}},
        {"blue", {38, 139, 210}}, {"yellow", {230, 200, 0}}, {"cyan", {42, 161, 152}}, {"magenta", {211, 54, 130}},
        {"gray", {128, 128, 128}}, {"grey", {128, 128, 128}}, {"orange", {255, 165, 0}}, {"purple", {128, 0, 128}},
        {"pink", {255, 182, 193}}, {"brown", {165, 42, 42}}, {"navy", {0, 0, 128}}, {"teal", {0, 128, 128}},
        {"lime", {0, 255, 0}}, {"silver", {192, 192, 192}}, {"maroon", {128, 0, 0}}, {"olive", {128, 128, 0}},
    };
    for (auto& n : names)
        if (s == n.name) return n.c;
    return std::nullopt;
}

} // namespace tui::color
