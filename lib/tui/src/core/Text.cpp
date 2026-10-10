#include <tui/core/Text.hpp>

#include <algorithm>
#include <array>

namespace tui {

namespace {

constexpr char32_t kReplacementChar = 0xFFFD;

/// Plage de codepoints [lo, hi] inclusive associée à une largeur (0 ou 2).
/// Toute plage absente de la table est considérée de largeur 1.
struct WidthRange {
    char32_t lo;
    char32_t hi;
    int width;
};

// Table condensée : marques combinantes / zéro-largeur (0) puis CJK et
// assimilés en pleine largeur (2). Non exhaustive vis-à-vis d'Unicode
// (les scripts rares ne sont pas couverts) mais couvre les cas usuels
// d'un terminal (CJK, ponctuation pleine largeur, emoji basiques).
//
// DOIT rester trié par `lo` croissant : codepoint_width() fait une
// recherche binaire (std::upper_bound) dessus, dont le comportement est
// indéfini sur une plage non triée.
constexpr std::array<WidthRange, 26> kWidthTable{{
    {0x00AD, 0x00AD, 0}, // Soft hyphen
    {0x0300, 0x036F, 0}, // Combining Diacritical Marks
    {0x1100, 0x115F, 2}, // Hangul Jamo
    {0x1AB0, 0x1AFF, 0}, // Combining Diacritical Marks Extended
    {0x1DC0, 0x1DFF, 0}, // Combining Diacritical Marks Supplement
    {0x200B, 0x200F, 0}, // Zero width space/marks, LRM/RLM
    {0x2028, 0x202F, 0}, // Line/paragraph separators, embedding marks
    {0x2060, 0x206F, 0}, // Word joiner and invisible operators
    {0x20D0, 0x20FF, 0}, // Combining Diacritical Marks for Symbols
    {0x2E80, 0x303E, 2}, // CJK Radicals, Kangxi, CJK Symbols and Punctuation
    {0x3041, 0x33FF, 2}, // Hiragana .. CJK Compatibility
    {0x3400, 0x4DBF, 2}, // CJK Unified Ideographs Extension A
    {0x4E00, 0x9FFF, 2}, // CJK Unified Ideographs
    {0xA960, 0xA97F, 2}, // Hangul Jamo Extended-A
    {0xAC00, 0xD7A3, 2}, // Hangul Syllables
    {0xF900, 0xFAFF, 2}, // CJK Compatibility Ideographs
    {0xFE00, 0xFE0F, 0}, // Variation Selectors
    {0xFE20, 0xFE2F, 0}, // Combining Half Marks
    {0xFEFF, 0xFEFF, 0}, // BOM / zero width no-break space
    {0xFF01, 0xFF60, 2}, // Fullwidth forms
    {0xFFE0, 0xFFE6, 2}, // Fullwidth signs
    {0x1F300, 0x1F64F, 2}, // Misc symbols and pictographs, emoticons
    {0x1F900, 0x1F9FF, 2}, // Supplemental symbols and pictographs
    {0x20000, 0x2FFFD, 2}, // CJK Unified Ideographs Extension B..
    {0x30000, 0x3FFFD, 2}, // CJK Unified Ideographs Extension G..
    {0xE0100, 0xE01EF, 0}, // Variation Selectors Supplement
}};

} // namespace

std::u32string TextHelper::decode_utf8(std::string_view s) {
    std::u32string out;
    out.reserve(s.size());

    size_t i = 0;
    while (i < s.size()) {
        auto byte = [&](size_t idx) { return static_cast<unsigned char>(s[idx]); };
        unsigned char c = byte(i);

        auto continuation_ok = [&](size_t idx) {
            return idx < s.size() && (byte(idx) & 0xC0) == 0x80;
        };

        if ((c & 0x80) == 0x00) {
            out.push_back(c);
            i += 1;
        } else if ((c & 0xE0) == 0xC0 && continuation_ok(i + 1)) {
            char32_t cp = (static_cast<char32_t>(c & 0x1F) << 6) |
                          (byte(i + 1) & 0x3F);
            out.push_back(cp < 0x80 ? kReplacementChar : cp); // rejette l'overlong
            i += 2;
        } else if ((c & 0xF0) == 0xE0 && continuation_ok(i + 1) && continuation_ok(i + 2)) {
            char32_t cp = (static_cast<char32_t>(c & 0x0F) << 12) |
                          (static_cast<char32_t>(byte(i + 1) & 0x3F) << 6) |
                          (byte(i + 2) & 0x3F);
            out.push_back(cp < 0x800 ? kReplacementChar : cp);
            i += 3;
        } else if ((c & 0xF8) == 0xF0 && continuation_ok(i + 1) && continuation_ok(i + 2) && continuation_ok(i + 3)) {
            char32_t cp = (static_cast<char32_t>(c & 0x07) << 18) |
                          (static_cast<char32_t>(byte(i + 1) & 0x3F) << 12) |
                          (static_cast<char32_t>(byte(i + 2) & 0x3F) << 6) |
                          (byte(i + 3) & 0x3F);
            out.push_back(cp < 0x10000 || cp > 0x10FFFF ? kReplacementChar : cp);
            i += 4;
        } else {
            out.push_back(kReplacementChar);
            i += 1;
        }
    }
    return out;
}

std::string TextHelper::encode_utf8(std::u32string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char32_t cp : s) {
        if (cp <= 0x7F) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

int TextHelper::codepoint_width(char32_t cp) {
    if (cp == 0 || cp < 0x20 || cp == 0x7F || (cp >= 0x80 && cp < 0xA0)) return 0;

    auto it = std::upper_bound(
        kWidthTable.begin(), kWidthTable.end(), cp,
        [](char32_t value, const WidthRange& range) { return value < range.lo; });
    if (it != kWidthTable.begin()) {
        auto prev = it - 1;
        if (cp >= prev->lo && cp <= prev->hi) return prev->width;
    }
    return 1;
}

int TextHelper::display_width(std::u32string_view s) {
    int total = 0;
    for (char32_t cp : s) total += codepoint_width(cp);
    return total;
}

std::u32string TextHelper::truncate_to_width(
    std::u32string_view s, int max_width, std::u32string_view ellipsis) {
    if (max_width <= 0) return U"";
    if (display_width(s) <= max_width) return std::u32string(s);

    int ellipsis_width = display_width(ellipsis);
    int budget = max_width - ellipsis_width;
    if (budget < 0) budget = max_width; // l'ellipse ne tient même pas seule : on l'ignore

    std::u32string out;
    int used = 0;
    for (char32_t cp : s) {
        int w = codepoint_width(cp);
        if (used + w > budget) break;
        out.push_back(cp);
        used += w;
    }
    if (ellipsis_width <= max_width) out.append(ellipsis);
    return out;
}

std::vector<std::u32string> TextHelper::wrap_to_width(std::u32string_view s, int width) {
    std::vector<std::u32string> lines;
    if (width <= 0) {
        lines.emplace_back(s);
        return lines;
    }

    std::u32string current;
    int current_width = 0;

    auto flush_line = [&] {
        lines.push_back(current);
        current.clear();
        current_width = 0;
    };

    size_t i = 0;
    while (i < s.size()) {
        size_t word_start = i;
        while (i < s.size() && s[i] != U' ') ++i;
        std::u32string_view word = s.substr(word_start, i - word_start);
        size_t space_start = i;
        while (i < s.size() && s[i] == U' ') ++i;
        int spaces = static_cast<int>(i - space_start);

        int word_width = display_width(word);

        if (word_width > width) {
            // Mot plus large que la largeur dispo : coupe de force.
            if (!current.empty()) flush_line();
            size_t pos = 0;
            while (pos < word.size()) {
                std::u32string chunk;
                int chunk_width = 0;
                while (pos < word.size()) {
                    int w = codepoint_width(word[pos]);
                    if (chunk_width + w > width) break;
                    chunk.push_back(word[pos]);
                    chunk_width += w;
                    ++pos;
                }
                if (chunk.empty()) { chunk.push_back(word[pos]); ++pos; } // caractère isolé trop large
                lines.push_back(chunk);
            }
        } else {
            int needed = current_width == 0 ? word_width : current_width + 1 + word_width;
            if (needed > width) {
                flush_line();
                current = std::u32string(word);
                current_width = word_width;
            } else {
                if (!current.empty()) {
                    current.push_back(U' ');
                    ++current_width;
                }
                current.append(word);
                current_width += word_width;
            }
        }

        if (spaces > 0 && i < s.size()) {
            // espaces internes consommés comme séparateur normal (déjà comptés ci-dessus)
        }
    }
    if (!current.empty() || lines.empty()) flush_line();

    return lines;
}

} // namespace tui
