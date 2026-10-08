#pragma once

// Conversions colonne <-> lettre (base 26 bijective, comme Excel : A,
// B, ... Z, AA, AB, ...) et parsing/formatage de references de cellule
// ("A1") et de plages ("A1:B3"). Grille extensible : pas de limite
// dure sur col/row au-dela d'un garde-fou de securite contre une
// repetition clavier incontrolee.

#include <algorithm>
#include <cctype>
#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace sheetapp {

inline constexpr int kMaxIndex = 1'000'000;

struct CellPos {
    int col = 0;
    int row = 0;
    auto operator<=>(const CellPos&) const = default;
};

struct CellRange {
    CellPos start;
    CellPos end;
};

inline std::string column_label(int col) {
    std::string out;
    long n = col;
    do {
        out.push_back(static_cast<char>('A' + (n % 26)));
        n = n / 26 - 1;
    } while (n >= 0);
    std::reverse(out.begin(), out.end());
    return out;
}

inline std::optional<int> column_index(std::string_view label) {
    if (label.empty()) return std::nullopt;
    long value = 0;
    for (char c : label) {
        char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (upper < 'A' || upper > 'Z') return std::nullopt;
        value = value * 26 + (upper - 'A' + 1);
        if (value > kMaxIndex) return std::nullopt;
    }
    return static_cast<int>(value - 1);
}

inline std::string cell_ref(CellPos p) { return column_label(p.col) + std::to_string(p.row + 1); }

inline std::optional<CellPos> parse_cell_ref(std::string_view s) {
    size_t i = 0;
    while (i < s.size() && std::isalpha(static_cast<unsigned char>(s[i]))) ++i;
    if (i == 0 || i == s.size()) return std::nullopt;
    auto col = column_index(s.substr(0, i));
    if (!col) return std::nullopt;

    size_t digits_start = i;
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
    if (i != s.size() || i == digits_start) return std::nullopt;

    long row_1based = 0;
    for (size_t j = digits_start; j < s.size(); ++j) {
        row_1based = row_1based * 10 + (s[j] - '0');
        if (row_1based > kMaxIndex) return std::nullopt;
    }
    if (row_1based < 1) return std::nullopt;
    return CellPos{*col, static_cast<int>(row_1based - 1)};
}

inline std::optional<CellRange> parse_range(std::string_view s) {
    auto colon = s.find(':');
    if (colon == std::string_view::npos) return std::nullopt;
    auto a = parse_cell_ref(s.substr(0, colon));
    auto b = parse_cell_ref(s.substr(colon + 1));
    if (!a || !b) return std::nullopt;
    CellRange r;
    r.start = CellPos{std::min(a->col, b->col), std::min(a->row, b->row)};
    r.end = CellPos{std::max(a->col, b->col), std::max(a->row, b->row)};
    return r;
}

struct CellPosHash {
    size_t operator()(const CellPos& p) const noexcept {
        return (static_cast<size_t>(static_cast<uint32_t>(p.col)) << 32) ^
               static_cast<size_t>(static_cast<uint32_t>(p.row));
    }
};

} // namespace sheetapp
