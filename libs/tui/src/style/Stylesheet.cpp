/* @file style/Stylesheet.cpp — analyse TCSS, sélecteurs et cascade. */

#include "tui/style/Stylesheet.hpp"

#include "tui/Tui.hpp"
#include "tui/style/ColorUtil.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>

namespace tui {

// ============================================================================
// ComputedStyle
// ============================================================================

void ComputedStyle::merge(const ComputedStyle& o) {
    if (o.color) {
        color = o.color;
        color_auto.reset();
    }
    if (o.color_auto) {
        color_auto = o.color_auto;
        color.reset();
    }
    if (o.background) background = o.background;
    if (o.text_style) text_style = o.text_style;
    if (o.border) border = o.border;
    if (o.border_color) border_color = o.border_color;
    if (o.border_title_color) border_title_color = o.border_title_color;
    if (o.border_title_style) border_title_style = o.border_title_style;
    if (o.padding) padding = o.padding;
    if (o.width) width = o.width;
    if (o.height) height = o.height;
    if (o.min_width) min_width = o.min_width;
    if (o.max_width) max_width = o.max_width;
    if (o.min_height) min_height = o.min_height;
    if (o.max_height) max_height = o.max_height;
    if (o.display) display = o.display;
    for (auto& [k, v] : o.extra) extra[k] = v;
}

namespace {

std::string lower(std::string_view s) {
    std::string r(s);
    for (auto& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

std::string trim(std::string_view s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

std::vector<std::string> words(std::string_view s) {
    std::vector<std::string> out;
    std::string cur;
    int depth = 0;
    for (char c : s) {
        if (c == '(') ++depth;
        if (c == ')') --depth;
        if (std::isspace(static_cast<unsigned char>(c)) && depth == 0) {
            if (!cur.empty()) out.push_back(std::move(cur)), cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(std::move(cur));
    return out;
}

bool is_ident_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_'; }

std::optional<int> to_int(std::string_view s) {
    if (s.empty()) return std::nullopt;
    int sign = 1;
    size_t i = 0;
    if (s[0] == '-') sign = -1, i = 1;
    if (i >= s.size()) return std::nullopt;
    int v = 0;
    for (; i < s.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) return std::nullopt;
        v = v * 10 + (s[i] - '0');
    }
    return sign * v;
}

std::optional<Color> ansi_color(const std::string& s) {
    static const std::map<std::string, Color> ansi = {
        {"ansi_default", Color::Default()},   {"ansi_black", {0, 0, 0}},          {"ansi_red", {205, 49, 49}},
        {"ansi_green", {13, 188, 121}},       {"ansi_yellow", {229, 229, 16}},    {"ansi_blue", {36, 114, 200}},
        {"ansi_magenta", {188, 63, 188}},     {"ansi_cyan", {17, 168, 205}},      {"ansi_white", {229, 229, 229}},
        {"ansi_bright_black", {102, 102, 102}}, {"ansi_bright_red", {241, 76, 76}}, {"ansi_bright_green", {35, 209, 139}},
        {"ansi_bright_yellow", {245, 245, 67}}, {"ansi_bright_blue", {59, 142, 234}},
        {"ansi_bright_magenta", {214, 112, 214}}, {"ansi_bright_cyan", {41, 184, 219}},
        {"ansi_bright_white", {255, 255, 255}},
    };
    auto it = ansi.find(s);
    if (it == ansi.end()) return std::nullopt;
    return it->second;
}

std::optional<TextStyle> parse_text_style(std::string_view value, std::string* err) {
    TextStyle st = TextStyle::None;
    auto ws = words(lower(value));
    for (size_t i = 0; i < ws.size(); ++i) {
        const auto& w = ws[i];
        if (w == "not") {   // « not underline » : ignoré (pas de retrait de style)
            ++i;
            continue;
        }
        if (w == "none") continue;
        if (w == "bold" || w == "b") st = st | TextStyle::Bold;
        else if (w == "italic" || w == "i") st = st | TextStyle::Italic;
        else if (w == "underline" || w == "u") st = st | TextStyle::Underline;
        else if (w == "reverse" || w == "r") st = st | TextStyle::Reverse;
        else if (w == "dim" || w == "d") st = st | TextStyle::Dim;
        else if (w == "strike" || w == "s" || w == "strikethrough") st = st | TextStyle::Strikethrough;
        else if (w == "blink") st = st | TextStyle::Blink;
        else {
            if (err) *err = "style de texte inconnu « " + w + " »";
            return std::nullopt;
        }
    }
    return st;
}

std::optional<BorderStyle> parse_border_kind(const std::string& k) {
    static const std::map<std::string, BorderStyle> kinds = {
        {"none", BorderStyle::None},     {"hidden", BorderStyle::None},   {"solid", BorderStyle::Square},
        {"square", BorderStyle::Square}, {"round", BorderStyle::Rounded}, {"rounded", BorderStyle::Rounded},
        {"heavy", BorderStyle::Heavy},   {"double", BorderStyle::Double}, {"ascii", BorderStyle::Ascii},
        {"dashed", BorderStyle::Dashed}, {"blank", BorderStyle::Blank},   {"thick", BorderStyle::Thick},
        {"tall", BorderStyle::Square},   {"wide", BorderStyle::Square},   {"panel", BorderStyle::Square},
        {"outer", BorderStyle::Square},  {"inner", BorderStyle::Square},  {"hkey", BorderStyle::Square},
        {"vkey", BorderStyle::Square},
    };
    auto it = kinds.find(k);
    if (it == kinds.end()) return std::nullopt;
    return it->second;
}

/// Registre des types CSS (nom → test dynamic_cast).
struct TypeRegistry {
    std::mutex m;
    std::vector<std::pair<std::string, std::function<bool(const Widget&)>>> entries;
    bool builtins = false;
};

TypeRegistry& registry() {
    static TypeRegistry r;
    return r;
}

template <class T> void reg(TypeRegistry& r, const char* name) {
    r.entries.emplace_back(name, [](const Widget& w) { return dynamic_cast<const T*>(&w) != nullptr; });
}

void ensure_builtins(TypeRegistry& r) {
    if (r.builtins) return;
    r.builtins = true;
    reg<Container>(r, "Container");
    reg<LinearLayout>(r, "LinearLayout");
    reg<Vertical>(r, "Vertical");
    reg<Horizontal>(r, "Horizontal");
    reg<Grid>(r, "Grid");
    reg<Stack>(r, "Stack");
    reg<Border>(r, "Border");
    reg<ScrollableContainer>(r, "ScrollableContainer");
    reg<Tabs>(r, "Tabs");
    reg<Input>(r, "Input");
    reg<Button>(r, "Button");
    reg<Label>(r, "Label");
    reg<Label>(r, "Static");
    reg<ListView>(r, "ListView");
    reg<TreeView>(r, "TreeView");
    reg<TableView>(r, "TableView");
    reg<TableView>(r, "DataTable");
    reg<TextArea>(r, "TextArea");
    reg<Dialog>(r, "Dialog");
    reg<Checkbox>(r, "Checkbox");
    reg<ToggleSwitch>(r, "Switch");
    reg<ProgressBar>(r, "ProgressBar");
}

} // namespace

std::optional<Color> ComputedStyle::extra_color(const std::string& name) const {
    auto it = extra.find(name);
    if (it == extra.end()) return std::nullopt;
    auto ws = words(it->second);
    if (ws.empty()) return std::nullopt;
    if (auto a = ansi_color(lower(ws[0]))) return a;
    return color::parse(ws[0]);
}

std::optional<TextStyle> ComputedStyle::extra_text_style(const std::string& name) const {
    auto it = extra.find(name);
    if (it == extra.end()) return std::nullopt;
    return parse_text_style(it->second, nullptr);
}

namespace css {

std::string ParseError::to_string() const {
    return origin + ":" + std::to_string(line) + ":" + std::to_string(column) + ": " + message;
}

std::string Selector::to_string() const {
    std::string out;
    for (size_t i = 0; i < steps.size(); ++i) {
        const auto& s = steps[i].simple;
        if (i > 0) out += steps[i].combinator == '>' ? " > " : " ";
        if (s.universal) out += "*";
        out += s.type;
        if (!s.id.empty()) out += "#" + s.id;
        for (auto& c : s.classes) out += "." + c;
        for (auto& p : s.pseudos) out += ":" + p;
    }
    return out;
}

void register_type(std::string name, std::function<bool(const Widget&)> test) {
    auto& r = registry();
    std::lock_guard lock(r.m);
    ensure_builtins(r);
    r.entries.emplace_back(std::move(name), std::move(test));
}

bool matches_type(const Widget& w, std::string_view name) {
    if (name == "Widget") return true;
    if (name == "Screen") return w.parent() == nullptr;
    if (w.type_name() == name) return true;
    auto& r = registry();
    std::lock_guard lock(r.m);
    ensure_builtins(r);
    for (auto& [n, test] : r.entries)
        if (n == name && test(w)) return true;
    return false;
}

// ============================================================================
// Sélecteurs
// ============================================================================

namespace {

const std::set<std::string>& known_pseudos() {
    static const std::set<std::string> p = {"focus",  "hover", "disabled",    "enabled",   "focus-within",
                                            "root",   "dark",  "light",       "first-child", "last-child",
                                            "blur",   "even",  "odd"};
    return p;
}

bool match_simple(const SimpleSelector& s, const Widget& w, const MatchContext& ctx) {
    if (!s.type.empty() && !matches_type(w, s.type)) return false;
    if (!s.id.empty() && w.id() != s.id) return false;
    for (auto& c : s.classes)
        if (!w.has_class(c)) return false;
    for (auto& p : s.pseudos) {
        if (p == "focus") {
            if (!w.has_focus()) return false;
        } else if (p == "blur") {
            if (w.has_focus()) return false;
        } else if (p == "hover") {
            if (!w.hovered()) return false;
        } else if (p == "disabled") {
            if (!w.disabled()) return false;
        } else if (p == "enabled") {
            if (w.disabled()) return false;
        } else if (p == "focus-within") {
            if (!ctx.focused || !ctx.focused->is_descendant_of(&w)) return false;
        } else if (p == "root") {
            if (w.parent()) return false;
        } else if (p == "dark") {
            if (!ctx.dark) return false;
        } else if (p == "light") {
            if (ctx.dark) return false;
        } else if (p == "first-child" || p == "last-child" || p == "even" || p == "odd") {
            if (!w.parent()) return false;
            auto sib = w.parent()->children();
            auto it = std::find_if(sib.begin(), sib.end(), [&](auto& c) { return c.get() == &w; });
            if (it == sib.end()) return false;
            long idx = it - sib.begin();
            if (p == "first-child" && idx != 0) return false;
            if (p == "last-child" && idx != static_cast<long>(sib.size()) - 1) return false;
            if (p == "even" && idx % 2 != 1) return false;   // 1-indexé comme en CSS
            if (p == "odd" && idx % 2 != 0) return false;
        }
    }
    return true;
}

bool match_from(const Selector& s, long step, const Widget* w, const MatchContext& ctx) {
    if (!w || !match_simple(s.steps[static_cast<size_t>(step)].simple, *w, ctx)) return false;
    if (step == 0) return true;
    char comb = s.steps[static_cast<size_t>(step)].combinator;
    if (comb == '>') return match_from(s, step - 1, w->parent(), ctx);
    for (const Widget* a = w->parent(); a; a = a->parent())
        if (match_from(s, step - 1, a, ctx)) return true;
    return false;
}

} // namespace

bool Stylesheet::matches(const Selector& s, const Widget& w, const MatchContext& ctx) {
    if (s.steps.empty()) return false;
    return match_from(s, static_cast<long>(s.steps.size()) - 1, &w, ctx);
}

std::optional<Selector> Stylesheet::parse_selector(std::string_view text, std::string* error) {
    Selector sel;
    size_t i = 0, n = text.size();
    char comb = ' ';
    auto fail = [&](std::string msg) -> std::optional<Selector> {
        if (error) *error = std::move(msg);
        return std::nullopt;
    };
    auto ident = [&]() {
        size_t b = i;
        while (i < n && is_ident_char(text[i])) ++i;
        return std::string(text.substr(b, i - b));
    };
    while (i < n) {
        while (i < n && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
        if (i >= n) break;
        if (text[i] == '>') {
            if (sel.steps.empty()) return fail("combinateur « > » sans sélecteur à gauche");
            comb = '>';
            ++i;
            continue;
        }
        SimpleSelector simple;
        bool any = false;
        while (i < n && !std::isspace(static_cast<unsigned char>(text[i])) && text[i] != '>') {
            char c = text[i];
            if (c == '*') {
                simple.universal = true;
                ++i;
            } else if (c == '#') {
                ++i;
                simple.id = ident();
                if (simple.id.empty()) return fail("identifiant vide après « # »");
                ++sel.ids;
            } else if (c == '.') {
                ++i;
                auto cl = ident();
                if (cl.empty()) return fail("classe vide après « . »");
                simple.classes.push_back(cl);
                ++sel.classes;
            } else if (c == ':') {
                ++i;
                auto p = lower(ident());
                if (!known_pseudos().contains(p)) return fail("pseudo-classe inconnue « :" + p + " »");
                simple.pseudos.push_back(p);
                ++sel.classes;
            } else if (is_ident_char(c) && !any) {
                simple.type = ident();
                ++sel.types;
            } else {
                return fail(std::string("caractère inattendu « ") + c + " » dans le sélecteur");
            }
            any = true;
        }
        if (!any) return fail("sélecteur vide");
        sel.steps.push_back({std::move(simple), sel.steps.empty() ? ' ' : comb});
        comb = ' ';
    }
    if (sel.steps.empty()) return fail("sélecteur vide");
    if (comb == '>') return fail("combinateur « > » sans sélecteur à droite");
    return sel;
}

// ============================================================================
// Analyse des sources
// ============================================================================

namespace {

struct Parser {
    std::string_view src;
    std::string origin;
    size_t pos = 0;
    int line = 1, col = 1;
    std::vector<ParseError>& errors;

    void error(int l, int c, std::string msg) { errors.push_back({origin, l, c, std::move(msg)}); }

    char peek() const { return pos < src.size() ? src[pos] : '\0'; }
    void advance() {
        if (pos >= src.size()) return;
        if (src[pos] == '\n') ++line, col = 1;
        else ++col;
        ++pos;
    }
    void skip_ws_comments() {
        while (pos < src.size()) {
            if (std::isspace(static_cast<unsigned char>(src[pos]))) {
                advance();
            } else if (src.compare(pos, 2, "/*") == 0) {
                int l = line, c = col;
                advance(), advance();
                while (pos < src.size() && src.compare(pos, 2, "*/") != 0) advance();
                if (pos >= src.size()) {
                    error(l, c, "commentaire non terminé");
                    return;
                }
                advance(), advance();
            } else if (src.compare(pos, 2, "//") == 0) {
                while (pos < src.size() && src[pos] != '\n') advance();
            } else {
                return;
            }
        }
    }
    /// Lit jusqu'à ';', '{' ou '}' (exclus), hors chaînes et parenthèses ; commentaires retirés.
    std::string chunk() {
        std::string out;
        int depth = 0;
        while (pos < src.size()) {
            char c = src[pos];
            if (depth == 0 && (c == ';' || c == '{' || c == '}')) break;
            if (src.compare(pos, 2, "/*") == 0) {
                advance(), advance();
                while (pos < src.size() && src.compare(pos, 2, "*/") != 0) advance();
                advance(), advance();
                out += ' ';
                continue;
            }
            if (c == '"' || c == '\'') {
                char q = c;
                out += c;
                advance();
                while (pos < src.size() && src[pos] != q && src[pos] != '\n') out += src[pos], advance();
                if (pos < src.size() && src[pos] == q) out += q, advance();
                continue;
            }
            if (c == '(') ++depth;
            if (c == ')') --depth;
            out += c;
            advance();
        }
        return out;
    }
};

std::string combine_selector(const std::string& parent, const std::string& child) {
    if (parent.empty()) return child;
    if (child.find('&') == std::string::npos) return parent + " " + child;
    std::string out;
    for (char c : child) {
        if (c == '&') out += parent;
        else out += c;
    }
    return out;
}

std::vector<std::string> split_commas(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ',') out.push_back(trim(cur)), cur.clear();
        else cur += c;
    }
    out.push_back(trim(cur));
    return out;
}

} // namespace

bool Stylesheet::add_source(std::string_view text, std::string origin, int tier) {
    size_t before = parse_errors_.size() + value_errors_.size();
    Parser p{text, origin, 0, 1, 1, parse_errors_};

    // Analyse récursive d'un bloc ; `parents` = textes des sélecteurs englobants.
    std::function<void(const std::vector<std::string>&, Rule*, int)> block =
        [&](const std::vector<std::string>& parents, Rule* rule, int depth) {
            while (true) {
                p.skip_ws_comments();
                if (p.pos >= text.size()) {
                    if (depth > 0) p.error(p.line, p.col, "accolade « } » manquante en fin de fichier");
                    return;
                }
                if (p.peek() == '}') {
                    if (depth == 0) p.error(p.line, p.col, "accolade « } » sans bloc ouvert");
                    p.advance();
                    if (depth > 0) return;
                    continue;
                }
                int l = p.line, c = p.col;
                std::string piece = trim(p.chunk());
                char stop = p.peek();
                if (stop == '{') {
                    p.advance();
                    std::vector<std::string> texts;
                    std::vector<Selector> sels;
                    bool ok = true;
                    for (auto& part : split_commas(piece)) {
                        for (auto& par : parents.empty() ? std::vector<std::string>{""} : parents) {
                            std::string full = combine_selector(par, part);
                            std::string err;
                            auto s = parse_selector(full, &err);
                            if (!s) {
                                p.error(l, c, err + " : « " + full + " »");
                                ok = false;
                                continue;
                            }
                            texts.push_back(full);
                            sels.push_back(std::move(*s));
                        }
                    }
                    if (!ok || sels.empty()) {
                        // Ignore le bloc fautif en entier.
                        Rule dummy;
                        block({"*"}, &dummy, depth + 1);
                        continue;
                    }
                    rules_.push_back(Rule{std::move(sels), {}, tier, next_order_++, origin, {}});
                    size_t idx = rules_.size() - 1;
                    // Les déclarations s'accumulent dans une règle locale : rules_
                    // peut être réalloué par les règles imbriquées.
                    Rule local;
                    block(texts, &local, depth + 1);
                    rules_[idx].declarations = std::move(local.declarations);
                    continue;
                }
                if (stop == ';') p.advance();
                if (piece.empty()) continue;
                if (piece[0] == '$') {
                    auto colon = piece.find(':');
                    if (colon == std::string::npos) {
                        p.error(l, c, "déclaration de variable sans « : »");
                        continue;
                    }
                    std::string name = trim(std::string_view(piece).substr(1, colon - 1));
                    css_vars_[name] = trim(std::string_view(piece).substr(colon + 1));
                    var_origins_.emplace_back(origin, name);
                    continue;
                }
                if (!rule) {
                    p.error(l, c, "déclaration hors d'un bloc : « " + piece + " »");
                    continue;
                }
                auto colon = piece.find(':');
                if (colon == std::string::npos) {
                    p.error(l, c, "déclaration sans « : » : « " + piece + " »");
                    continue;
                }
                std::string prop = lower(trim(std::string_view(piece).substr(0, colon)));
                std::string value = trim(std::string_view(piece).substr(colon + 1));
                if (value.ends_with("!important")) value = trim(value.substr(0, value.size() - 10));
                rule->declarations.push_back({prop, value, l});
            }
        };
    block({}, nullptr, 0);
    recompile();
    return parse_errors_.size() + value_errors_.size() == before;
}

bool Stylesheet::add_file(const std::filesystem::path& path, int tier) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        parse_errors_.push_back({path.string(), 0, 0, "fichier illisible"});
        errors_ = parse_errors_;
        errors_.insert(errors_.end(), value_errors_.begin(), value_errors_.end());
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    return add_source(ss.str(), path.string(), tier);
}

void Stylesheet::clear() {
    rules_.clear();
    errors_.clear();
    parse_errors_.clear();
    value_errors_.clear();
    css_vars_.clear();
    var_origins_.clear();
    next_order_ = 0;
    ++generation_;
    inline_cache_.clear();
}

void Stylesheet::remove_origin(const std::string& origin) {
    std::erase_if(rules_, [&](const Rule& r) { return r.origin == origin; });
    std::erase_if(parse_errors_, [&](const ParseError& e) { return e.origin == origin; });
    for (auto& [o, name] : var_origins_)
        if (o == origin) css_vars_.erase(name);
    std::erase_if(var_origins_, [&](auto& p) { return p.first == origin; });
    recompile();
}

void Stylesheet::set_theme_variables(std::map<std::string, std::string> vars) {
    theme_vars_ = std::move(vars);
    recompile();
}

std::map<std::string, std::string> Stylesheet::variables() const {
    auto all = theme_vars_;
    for (auto& [k, v] : css_vars_) all[k] = v;
    return all;
}

std::string Stylesheet::resolve(std::string_view value) const {
    std::string cur(value);
    for (int pass = 0; pass < 12 && cur.find('$') != std::string::npos; ++pass) {
        std::string out;
        for (size_t i = 0; i < cur.size();) {
            if (cur[i] != '$') {
                out += cur[i++];
                continue;
            }
            size_t j = i + 1;
            while (j < cur.size() && is_ident_char(cur[j])) ++j;
            std::string name = cur.substr(i + 1, j - i - 1);
            const std::string* v = nullptr;
            if (auto it = css_vars_.find(name); it != css_vars_.end()) v = &it->second;
            else if (auto it2 = theme_vars_.find(name); it2 != theme_vars_.end()) v = &it2->second;
            if (v) out += *v;
            else out += cur.substr(i, j - i);   // laissée telle quelle : signalée à la compilation
            i = j;
        }
        if (out == cur) break;
        cur = std::move(out);
    }
    return cur;
}

// ============================================================================
// Compilation des déclarations
// ============================================================================

void Stylesheet::compile_declaration(ComputedStyle& out, const Declaration& d, const std::string& origin,
                                     std::vector<ParseError>* errors) const {
    std::string value = resolve(d.value);
    auto fail = [&](std::string msg) {
        if (errors) errors->push_back({origin, d.line, 1, d.property + " : " + msg});
    };
    if (auto dollar = value.find('$'); dollar != std::string::npos) {
        size_t j = dollar + 1;
        while (j < value.size() && is_ident_char(value[j])) ++j;
        fail("variable inconnue « " + value.substr(dollar, j - dollar) + " »");
        return;
    }
    auto ws = words(value);

    // Couleur : « <couleur> [N%] » ; N% = opacité, mélangée au fond du thème.
    auto parse_color_words = [&](const std::vector<std::string>& w, size_t from) -> std::optional<Color> {
        if (from >= w.size()) return std::nullopt;
        std::string first = lower(w[from]);
        std::optional<Color> c = ansi_color(first);
        if (!c) c = color::parse(first);
        if (!c) return std::nullopt;
        if (from + 1 < w.size() && w[from + 1].ends_with('%')) {
            auto pct = to_int(std::string_view(w[from + 1]).substr(0, w[from + 1].size() - 1));
            if (!pct) return std::nullopt;
            Color base = Color::Default();
            if (auto it = theme_vars_.find("background"); it != theme_vars_.end())
                base = color::parse(it->second).value_or(Color::Default());
            if (!c->is_default && !base.is_default) c = color::blend(base, *c, *pct / 100.0);
        }
        return c;
    };
    auto color_value = [&]() -> std::optional<Color> {
        auto c = parse_color_words(ws, 0);
        if (!c) fail("couleur invalide « " + value + " »");
        return c;
    };

    const std::string& p = d.property;
    if (p == "color") {
        if (!ws.empty() && lower(ws[0]) == "auto") {
            double a = 1.0;
            if (ws.size() > 1 && ws[1].ends_with('%'))
                a = to_int(std::string_view(ws[1]).substr(0, ws[1].size() - 1)).value_or(100) / 100.0;
            out.color_auto = a;
            out.color.reset();
        } else if (auto c = color_value()) {
            out.color = c;
            out.color_auto.reset();
        }
    } else if (p == "background") {
        if (auto c = color_value()) out.background = c;
    } else if (p == "text-style") {
        std::string err;
        if (auto st = parse_text_style(value, &err)) out.text_style = st;
        else fail(err);
    } else if (p == "border" || p == "outline") {
        if (ws.empty()) return fail("valeur vide");
        auto kind = parse_border_kind(lower(ws[0]));
        if (!kind) return fail("type de bordure inconnu « " + ws[0] + " »");
        out.border = kind;
        if (ws.size() > 1) {
            if (auto c = parse_color_words(ws, 1)) out.border_color = c;
            else fail("couleur de bordure invalide « " + value + " »");
        }
    } else if (p == "border-color") {
        if (auto c = color_value()) out.border_color = c;
    } else if (p == "border-title-color") {
        if (auto c = color_value()) out.border_title_color = c;
    } else if (p == "border-title-style") {
        std::string err;
        if (auto st = parse_text_style(value, &err)) out.border_title_style = st;
        else fail(err);
    } else if (p == "padding") {
        std::vector<int> v;
        for (auto& w : ws) {
            auto n = to_int(w);
            if (!n || *n < 0) return fail("marge invalide « " + value + " »");
            v.push_back(*n);
        }
        if (v.size() == 1) out.padding = Spacing{v[0], v[0], v[0], v[0]};
        else if (v.size() == 2) out.padding = Spacing{v[0], v[1], v[0], v[1]};
        else if (v.size() == 4) out.padding = Spacing{v[0], v[1], v[2], v[3]};
        else fail("padding attend 1, 2 ou 4 valeurs");
    } else if (p == "width" || p == "height") {
        if (ws.size() != 1) return fail("dimension invalide « " + value + " »");
        std::string v = lower(ws[0]);
        Dimension dim;
        if (v == "auto" || v.ends_with("fr")) {
            dim.unit = Dimension::Unit::Auto;
        } else if (v.ends_with('%') || v.ends_with("vw") || v.ends_with("vh")) {
            size_t cut = v.ends_with('%') ? 1 : 2;
            auto n = to_int(std::string_view(v).substr(0, v.size() - cut));
            if (!n) return fail("dimension invalide « " + value + " »");
            dim = {Dimension::Unit::Percent, *n};
        } else if (auto n = to_int(v)) {
            dim = {Dimension::Unit::Cells, std::max(0, *n)};
        } else {
            return fail("dimension invalide « " + value + " »");
        }
        (p == "width" ? out.width : out.height) = dim;
    } else if (p == "min-width" || p == "max-width" || p == "min-height" || p == "max-height") {
        auto n = ws.size() == 1 ? to_int(ws[0]) : std::nullopt;
        if (!n) return fail("nombre de cellules attendu");
        if (p == "min-width") out.min_width = *n;
        else if (p == "max-width") out.max_width = *n;
        else if (p == "min-height") out.min_height = *n;
        else out.max_height = *n;
    } else if (p == "display") {
        std::string v = lower(value);
        if (v == "none") out.display = false;
        else if (v == "block") out.display = true;
        else fail("display attend « block » ou « none »");
    } else {
        out.extra[p] = value;
    }
}

void Stylesheet::compile_rule(Rule& r) {
    r.compiled = ComputedStyle{};
    for (auto& d : r.declarations) compile_declaration(r.compiled, d, r.origin, &value_errors_);
}

void Stylesheet::recompile() {
    // Les erreurs de valeur dépendent des variables : on les régénère.
    value_errors_.clear();
    for (auto& r : rules_) compile_rule(r);
    errors_ = parse_errors_;
    errors_.insert(errors_.end(), value_errors_.begin(), value_errors_.end());
    inline_cache_.clear();
    ++generation_;
}

ComputedStyle Stylesheet::compile_inline(std::string_view declarations, std::vector<ParseError>* errors) const {
    ComputedStyle out;
    size_t start = 0;
    while (start <= declarations.size()) {
        size_t end = declarations.find(';', start);
        std::string piece = trim(declarations.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
        if (!piece.empty()) {
            auto colon = piece.find(':');
            if (colon == std::string::npos) {
                if (errors) errors->push_back({"<inline>", 0, 0, "déclaration sans « : » : « " + piece + " »"});
            } else {
                Declaration d{lower(trim(std::string_view(piece).substr(0, colon))),
                              trim(std::string_view(piece).substr(colon + 1)), 0};
                compile_declaration(out, d, "<inline>", errors);
            }
        }
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return out;
}

// ============================================================================
// Cascade
// ============================================================================

ComputedStyle Stylesheet::compute(const Widget& w, const MatchContext& ctx) const {
    struct Hit {
        const Rule* rule;
        int ids, classes, types;
    };
    std::vector<Hit> hits;
    for (const auto& r : rules_) {
        const Selector* best = nullptr;
        for (const auto& s : r.selectors) {
            if (!matches(s, w, ctx)) continue;
            if (!best || std::tie(s.ids, s.classes, s.types) > std::tie(best->ids, best->classes, best->types)) best = &s;
        }
        if (best) hits.push_back({&r, best->ids, best->classes, best->types});
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
        return std::tie(a.rule->tier, a.ids, a.classes, a.types, a.rule->order) <
               std::tie(b.rule->tier, b.ids, b.classes, b.types, b.rule->order);
    });
    ComputedStyle out;
    for (auto& h : hits) out.merge(h.rule->compiled);
    if (!w.inline_style().empty()) {
        auto it = inline_cache_.find(w.inline_style());
        if (it == inline_cache_.end()) it = inline_cache_.emplace(w.inline_style(), compile_inline(w.inline_style())).first;
        out.merge(it->second);
    }
    return out;
}

void Stylesheet::apply(Widget& root, const MatchContext& ctx) const {
    root.set_computed_style(compute(root, ctx));
    for (auto& child : root.children())
        if (child) apply(*child, ctx);
}

} // namespace css
} // namespace tui
