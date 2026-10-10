/**
 * @file Geometry.hpp
 * @brief Types géométriques de base (Point, Size, Rect, Constraints).
 */

#pragma once

#include <algorithm>

namespace tui {

/// Position discrète en cellules terminal (colonne, ligne)
struct Point {
    int x = 0;
    int y = 0;

    constexpr Point() = default;
    constexpr Point(int x_, int y_) : x(x_), y(y_) {}

    constexpr bool operator==(const Point&) const = default;

    constexpr Point operator+(const Point& other) const { return {x + other.x, y + other.y}; }
    constexpr Point operator-(const Point& other) const { return {x - other.x, y - other.y}; }
};

/// Dimensions en cellules terminal
struct Size {
    int width = 0;
    int height = 0;

    constexpr Size() = default;
    constexpr Size(int w, int h) : width(w), height(h) {}

    constexpr bool operator==(const Size&) const = default;

    [[nodiscard]] constexpr bool empty() const { return width <= 0 || height <= 0; }
};

/// Rectangle en coordonnées entières (x,y = coin haut-gauche)
struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    constexpr Rect() = default;
    constexpr Rect(int x_, int y_, int w, int h) : x(x_), y(y_), width(w), height(h) {}
    constexpr Rect(Point origin, Size size) : x(origin.x), y(origin.y), width(size.width), height(size.height) {}

    constexpr bool operator==(const Rect&) const = default;

    [[nodiscard]] constexpr Point origin() const { return {x, y}; }
    [[nodiscard]] constexpr Size size() const { return {width, height}; }
    [[nodiscard]] constexpr int left() const { return x; }
    [[nodiscard]] constexpr int top() const { return y; }
    [[nodiscard]] constexpr int right() const { return x + width; }
    [[nodiscard]] constexpr int bottom() const { return y + height; }
    [[nodiscard]] constexpr bool empty() const { return width <= 0 || height <= 0; }

    [[nodiscard]] constexpr bool contains(Point p) const {
        return p.x >= x && p.x < x + width && p.y >= y && p.y < y + height;
    }

    /// Intersection avec un autre rectangle (rectangle vide si disjoints)
    [[nodiscard]] constexpr Rect intersect(const Rect& other) const {
        int nx = std::max(x, other.x);
        int ny = std::max(y, other.y);
        int nr = std::min(right(), other.right());
        int nb = std::min(bottom(), other.bottom());
        if (nr <= nx || nb <= ny) return {nx, ny, 0, 0};
        return {nx, ny, nr - nx, nb - ny};
    }

    /// Rectangle réduit de `amount` cellules de chaque côté (peut devenir vide)
    [[nodiscard]] constexpr Rect inset(int amount) const {
        return inset(amount, amount, amount, amount);
    }

    [[nodiscard]] constexpr Rect inset(int top_, int right_, int bottom_, int left_) const {
        int nw = width - left_ - right_;
        int nh = height - top_ - bottom_;
        return {x + left_, y + top_, std::max(nw, 0), std::max(nh, 0)};
    }
};

/// Contraintes de dimensionnement passées lors d'une passe measure()
struct Constraints {
    int min_width = 0;
    int max_width = 0;
    int min_height = 0;
    int max_height = 0;

    constexpr Constraints() = default;
    constexpr Constraints(int min_w, int max_w, int min_h, int max_h)
        : min_width(min_w), max_width(max_w), min_height(min_h), max_height(max_h) {}

    [[nodiscard]] static constexpr Constraints tight(Size s) {
        return {s.width, s.width, s.height, s.height};
    }

    [[nodiscard]] static constexpr Constraints loose(Size max_s) {
        return {0, max_s.width, 0, max_s.height};
    }

    [[nodiscard]] constexpr Size clamp(Size s) const {
        return {
            std::clamp(s.width, min_width, max_width),
            std::clamp(s.height, min_height, max_height),
        };
    }
};

} // namespace tui
