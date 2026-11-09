/**
 * @file SyntaxHighlight.hpp
 * @brief Coloration syntaxique légère, multi-langage, pilotée par table.
 *
 * Un seul moteur de tokenisation générique (tokenize_line) paramétré par
 * une LanguageSpec par langage (délimiteurs de chaîne, commentaires,
 * mots-clés...) plutôt que N analyseurs syntaxiques écrits à la main :
 * suffisant pour une coloration lisible (mots-clés/chaînes/commentaires/
 * nombres/variables), pas pour un vrai parseur par langage. Pas de
 * connaissance de la grammaire (pas de "unclosed string spanning
 * plusieurs lignes" sauf pour les commentaires bloc, qui sont le seul
 * état threadé ligne à ligne via `in_block_comment`).
 */

#pragma once

#include "Color.hpp"
#include "Text.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace tui {

enum class TokenRole {
    Plain,
    Keyword,
    Type,
    String,
    Comment,
    Number,
    Variable,
    Preprocessor,
};

struct Token {
    int start_col = 0;
    int length = 0;
    TokenRole role = TokenRole::Plain;
};

struct LanguageSpec {
    std::string line_comment;             // ex: "//" ou "#" ; vide = aucun
    std::string block_comment_start;      // ex: "/*" ; vide = aucun
    std::string block_comment_end;        // ex: "*/"
    std::vector<char32_t> string_delims;  // ex: {'"', '\''}
    bool escape_in_strings = true;
    std::unordered_set<std::string> keywords;
    std::unordered_set<std::string> types;
    char32_t variable_sigil = 0;          // ex: '$' (shell/make) ; 0 = aucun
    bool hash_is_preprocessor = false;    // C/C++/Java : #include, #define... (role Preprocessor, pas Comment)
};

namespace detail {

[[nodiscard]] inline bool is_ident_start(char32_t c) {
    return (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z') || c == U'_';
}
[[nodiscard]] inline bool is_ident_char(char32_t c) {
    return is_ident_start(c) || (c >= U'0' && c <= U'9');
}
[[nodiscard]] inline bool is_digit(char32_t c) { return c >= U'0' && c <= U'9'; }

[[nodiscard]] inline bool matches_at(const std::u32string& line, size_t pos, const std::string& needle) {
    if (needle.empty()) return false;
    auto decoded = TextHelper::decode_utf8(needle);
    if (pos + decoded.size() > line.size()) return false;
    return line.compare(pos, decoded.size(), decoded) == 0;
}

} // namespace detail

/// Retourne la table de langages connus (clé = identifiant court passé à
/// tokenize_line/detect_language, ex "cpp", "sh", "json"...).
[[nodiscard]] inline const std::unordered_map<std::string, LanguageSpec>& language_specs() {
    static const std::unordered_map<std::string, LanguageSpec> table = [] {
        std::unordered_map<std::string, LanguageSpec> t;

        LanguageSpec c_like;
        c_like.line_comment = "//";
        c_like.block_comment_start = "/*";
        c_like.block_comment_end = "*/";
        c_like.string_delims = {U'"', U'\''};
        c_like.hash_is_preprocessor = true;
        c_like.keywords = {
            "if", "else", "for", "while", "do", "switch", "case", "default", "break", "continue",
            "return", "goto", "struct", "class", "enum", "union", "typedef", "namespace", "using",
            "public", "private", "protected", "virtual", "override", "final", "static", "const",
            "constexpr", "consteval", "mutable", "volatile", "inline", "friend", "template", "typename",
            "new", "delete", "this", "sizeof", "throw", "try", "catch", "noexcept", "extern", "auto",
            "true", "false", "nullptr", "NULL", "void", "operator", "explicit", "export", "import",
            "module", "concept", "requires", "co_await", "co_return", "co_yield",
            // Java
            "package", "interface", "implements", "extends", "abstract", "synchronized", "instanceof",
            "throws", "final", "native", "transient", "assert",
        };
        c_like.types = {
            "int", "long", "short", "char", "float", "double", "bool", "signed", "unsigned", "size_t",
            "uint8_t", "uint16_t", "uint32_t", "uint64_t", "int8_t", "int16_t", "int32_t", "int64_t",
            "String", "Integer", "Boolean", "Double", "Float", "Object", "Long", "Short", "Byte",
            "std", "wchar_t", "char32_t", "char16_t", "char8_t",
        };
        t["c"] = c_like;
        t["cpp"] = c_like;
        t["java"] = c_like;
        t["js"] = [c_like] {
            LanguageSpec s = c_like;
            s.hash_is_preprocessor = false;
            s.string_delims = {U'"', U'\'', U'`'};
            s.keywords = {
                "if", "else", "for", "while", "do", "switch", "case", "default", "break", "continue",
                "return", "function", "var", "let", "const", "class", "extends", "new", "delete", "this",
                "typeof", "instanceof", "in", "of", "try", "catch", "finally", "throw", "async", "await",
                "yield", "import", "export", "from", "as", "static", "get", "set", "true", "false", "null",
                "undefined", "void", "interface", "type", "enum", "implements", "public", "private",
                "protected", "readonly", "namespace", "declare",
            };
            s.types = {"string", "number", "boolean", "any", "unknown", "never", "object", "symbol"};
            return s;
        }();
        t["css"] = [] {
            LanguageSpec s;
            s.block_comment_start = "/*";
            s.block_comment_end = "*/";
            s.string_delims = {U'"', U'\''};
            return s;
        }();
        t["html"] = [] {
            LanguageSpec s;
            s.block_comment_start = "<!--";
            s.block_comment_end = "-->";
            s.string_delims = {U'"', U'\''};
            return s;
        }();
        t["sh"] = [] {
            LanguageSpec s;
            s.line_comment = "#";
            s.string_delims = {U'"', U'\''};
            s.variable_sigil = U'$';
            s.keywords = {
                "if", "then", "else", "elif", "fi", "for", "while", "until", "do", "done", "case", "esac",
                "function", "in", "return", "exit", "break", "continue", "local", "export", "readonly",
                "shift", "trap", "set", "unset", "echo", "true", "false",
            };
            return s;
        }();
        t["json"] = [] {
            LanguageSpec s;
            s.string_delims = {U'"'};
            s.keywords = {"true", "false", "null"};
            return s;
        }();
        t["yaml"] = [] {
            LanguageSpec s;
            s.line_comment = "#";
            s.string_delims = {U'"', U'\''};
            s.keywords = {"true", "false", "null", "yes", "no", "on", "off"};
            return s;
        }();
        t["toml"] = [] {
            LanguageSpec s;
            s.line_comment = "#";
            s.string_delims = {U'"', U'\''};
            s.keywords = {"true", "false"};
            return s;
        }();
        t["make"] = [] {
            LanguageSpec s;
            s.line_comment = "#";
            s.string_delims = {U'"', U'\''};
            s.variable_sigil = U'$';
            s.keywords = {"ifeq", "ifneq", "ifdef", "ifndef", "else", "endif", "define", "endef",
                           "include", "export", "unexport", "override", "PHONY"};
            return s;
        }();
        return t;
    }();
    return table;
}

/// Devine l'identifiant de langage (clé de language_specs()) depuis un nom
/// de fichier ; chaîne vide si non reconnu (= pas de coloration).
[[nodiscard]] inline std::string detect_language(const std::string& filename) {
    auto dot = filename.find_last_of('.');
    std::string base = filename.substr(filename.find_last_of('/') == std::string::npos ? 0 : filename.find_last_of('/') + 1);
    if (base == "Makefile" || base == "makefile" || base == "GNUmakefile") return "make";

    if (dot == std::string::npos) return "";
    std::string ext = filename.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    static const std::unordered_map<std::string, std::string> by_ext = {
        {"c", "c"}, {"h", "c"},
        {"cpp", "cpp"}, {"cc", "cpp"}, {"cxx", "cpp"}, {"hpp", "cpp"}, {"hh", "cpp"}, {"hxx", "cpp"},
        {"java", "java"},
        {"js", "js"}, {"mjs", "js"}, {"cjs", "js"}, {"jsx", "js"}, {"ts", "js"}, {"tsx", "js"},
        {"css", "css"}, {"scss", "css"},
        {"html", "html"}, {"htm", "html"},
        {"sh", "sh"}, {"bash", "sh"}, {"zsh", "sh"},
        {"json", "json"},
        {"yaml", "yaml"}, {"yml", "yaml"},
        {"toml", "toml"},
        {"mk", "make"},
    };
    auto it = by_ext.find(ext);
    return it == by_ext.end() ? "" : it->second;
}

/// Tokenise une ligne selon `spec`. `in_block_comment` est à la fois
/// entrée (la ligne démarre-t-elle déjà dans un commentaire bloc ouvert
/// par une ligne précédente ?) et sortie (met-elle fin au commentaire, ou
/// reste-t-il ouvert pour la ligne suivante ?) - c'est tout l'état
/// threadé entre lignes, le reste de la tokenisation est sans mémoire.
[[nodiscard]] inline std::vector<Token> tokenize_line(const std::u32string& line, const LanguageSpec& spec,
                                                        bool& in_block_comment) {
    std::vector<Token> tokens;
    size_t n = line.size();
    size_t i = 0;

    if (in_block_comment) {
        size_t end = std::u32string::npos;
        if (!spec.block_comment_end.empty()) {
            for (size_t p = 0; p <= n; ++p) {
                if (detail::matches_at(line, p, spec.block_comment_end)) { end = p; break; }
            }
        }
        if (end == std::u32string::npos) {
            tokens.push_back({0, static_cast<int>(n), TokenRole::Comment});
            return tokens; // toute la ligne, commentaire toujours ouvert
        }
        size_t close_len = TextHelper::decode_utf8(spec.block_comment_end).size();
        tokens.push_back({0, static_cast<int>(end + close_len), TokenRole::Comment});
        i = end + close_len;
        in_block_comment = false;
    }

    bool at_line_start = (i == 0);
    while (i < n) {
        // Espaces : pas de token dédié, simplement sautés (Plain implicite).
        if (line[i] == U' ' || line[i] == U'\t') { ++i; at_line_start = false; continue; }

        if (!spec.line_comment.empty() && detail::matches_at(line, i, spec.line_comment)) {
            tokens.push_back({static_cast<int>(i), static_cast<int>(n - i), TokenRole::Comment});
            break;
        }
        if (spec.hash_is_preprocessor && at_line_start && line[i] == U'#') {
            tokens.push_back({static_cast<int>(i), static_cast<int>(n - i), TokenRole::Preprocessor});
            break;
        }
        if (!spec.block_comment_start.empty() && detail::matches_at(line, i, spec.block_comment_start)) {
            size_t end = std::u32string::npos;
            for (size_t p = i; p <= n; ++p) {
                if (p > i && detail::matches_at(line, p, spec.block_comment_end)) { end = p; break; }
            }
            if (end == std::u32string::npos) {
                tokens.push_back({static_cast<int>(i), static_cast<int>(n - i), TokenRole::Comment});
                in_block_comment = true;
                break;
            }
            size_t close_len = TextHelper::decode_utf8(spec.block_comment_end).size();
            tokens.push_back({static_cast<int>(i), static_cast<int>(end + close_len - i), TokenRole::Comment});
            i = end + close_len;
            at_line_start = false;
            continue;
        }
        if (spec.variable_sigil != 0 && line[i] == spec.variable_sigil) {
            size_t start = i;
            ++i;
            if (i < n && (line[i] == U'(' || line[i] == U'{')) {
                char32_t close = (line[i] == U'(') ? U')' : U'}';
                ++i;
                while (i < n && line[i] != close) ++i;
                if (i < n) ++i; // inclut la fermeture
            } else {
                while (i < n && detail::is_ident_char(line[i])) ++i;
            }
            tokens.push_back({static_cast<int>(start), static_cast<int>(i - start), TokenRole::Variable});
            at_line_start = false;
            continue;
        }
        if (std::find(spec.string_delims.begin(), spec.string_delims.end(), line[i]) != spec.string_delims.end()) {
            char32_t delim = line[i];
            size_t start = i;
            ++i;
            while (i < n && line[i] != delim) {
                if (spec.escape_in_strings && line[i] == U'\\' && i + 1 < n) i += 2;
                else ++i;
            }
            if (i < n) ++i; // inclut le délimiteur fermant
            tokens.push_back({static_cast<int>(start), static_cast<int>(i - start), TokenRole::String});
            at_line_start = false;
            continue;
        }
        if (detail::is_digit(line[i]) || (line[i] == U'.' && i + 1 < n && detail::is_digit(line[i + 1]))) {
            size_t start = i;
            while (i < n && (detail::is_digit(line[i]) || line[i] == U'.' || line[i] == U'x' || line[i] == U'X' ||
                              line[i] == U'_' || (line[i] >= U'a' && line[i] <= U'f') ||
                              (line[i] >= U'A' && line[i] <= U'F'))) {
                ++i;
            }
            tokens.push_back({static_cast<int>(start), static_cast<int>(i - start), TokenRole::Number});
            at_line_start = false;
            continue;
        }
        if (detail::is_ident_start(line[i])) {
            size_t start = i;
            while (i < n && detail::is_ident_char(line[i])) ++i;
            std::string word = TextHelper::encode_utf8(line.substr(start, i - start));
            TokenRole role = spec.keywords.count(word) ? TokenRole::Keyword
                            : spec.types.count(word)    ? TokenRole::Type
                                                         : TokenRole::Plain;
            if (role != TokenRole::Plain) tokens.push_back({static_cast<int>(start), static_cast<int>(i - start), role});
            at_line_start = false;
            continue;
        }
        ++i;
        at_line_start = false;
    }
    return tokens;
}

/// Palette par rôle de token ; un thème raisonnable par défaut, mais
/// substituable (ex: pour coller à Theme::planor()).
class SyntaxTheme {
public:
    SyntaxTheme() {
        colors_[static_cast<size_t>(TokenRole::Keyword)] = Color{198, 120, 221};
        colors_[static_cast<size_t>(TokenRole::Type)] = Color{224, 180, 90};
        colors_[static_cast<size_t>(TokenRole::String)] = Color{110, 200, 140};
        colors_[static_cast<size_t>(TokenRole::Comment)] = Color{128, 128, 140};
        colors_[static_cast<size_t>(TokenRole::Number)] = Color{209, 154, 102};
        colors_[static_cast<size_t>(TokenRole::Variable)] = Color{86, 182, 194};
        colors_[static_cast<size_t>(TokenRole::Preprocessor)] = Color{198, 120, 221};
        colors_[static_cast<size_t>(TokenRole::Plain)] = Color::Default();
    }

    void set_color(TokenRole role, Color color) { colors_[static_cast<size_t>(role)] = color; }
    [[nodiscard]] Color color(TokenRole role) const { return colors_[static_cast<size_t>(role)]; }

private:
    std::array<Color, 8> colors_{};
};

} // namespace tui
