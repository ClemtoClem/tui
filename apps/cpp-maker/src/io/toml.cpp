/* @file io/toml.cpp */

#include "io/toml.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <format>
#include <fstream>
#include <limits>
#include <sstream>

namespace toml {

// ============================================================================
// ParseError
// ============================================================================

namespace {
std::string compose(const std::string& msg, const std::string& file, size_t line, size_t col) {
    std::string r;
    if (!file.empty()) r += file;
    if (line) r += std::format("{}{}:{}", r.empty() ? "" : ":", line, col);
    if (!r.empty()) r += ": ";
    return r + msg;
}
} // namespace

ParseError::ParseError(const std::string& msg, std::string file, size_t line, size_t column)
    : std::runtime_error(compose(msg, file, line, column)), msg_(msg), file_(std::move(file)),
      line_(line), column_(column) {}

std::string_view type_name(Type t) {
    switch (t) {
    case Type::None: return "vide";
    case Type::String: return "chaîne";
    case Type::Integer: return "entier";
    case Type::Float: return "flottant";
    case Type::Boolean: return "booléen";
    case Type::Array: return "tableau";
    case Type::Table: return "table";
    }
    return "?";
}

// ============================================================================
// Value
// ============================================================================

Value::Value(const std::vector<std::string>& v) : type_(Type::Array) {
    for (auto& s : v) arr_.emplace_back(s);
}

Value Value::table() {
    Value v;
    v.type_ = Type::Table;
    return v;
}

Value Value::array() {
    Value v;
    v.type_ = Type::Array;
    return v;
}

std::vector<std::string> Value::as_strings(bool* ok) const {
    std::vector<std::string> r;
    if (ok) *ok = true;
    if (type_ == Type::String) return {str_};
    if (type_ != Type::Array) {
        if (ok) *ok = false;
        return r;
    }
    for (auto& v : arr_) {
        if (!v.is_string()) {
            if (ok) *ok = false;
            return {};
        }
        r.push_back(v.str_);
    }
    return r;
}

const Value* Value::find(std::string_view key) const {
    if (type_ != Type::Table) return nullptr;
    for (size_t i = 0; i < keys_.size(); ++i)
        if (keys_[i] == key) return &vals_[i];
    return nullptr;
}

Value* Value::find(std::string_view key) {
    if (type_ != Type::Table) return nullptr;
    for (size_t i = 0; i < keys_.size(); ++i)
        if (keys_[i] == key) return &vals_[i];
    return nullptr;
}

Value& Value::operator[](std::string_view key) {
    if (type_ == Type::None) type_ = Type::Table;
    if (auto* v = find(key)) return *v;
    keys_.emplace_back(key);
    vals_.emplace_back();
    return vals_.back();
}

void Value::set(std::string_view key, Value v) { (*this)[key] = std::move(v); }

bool Value::erase(std::string_view key) {
    for (size_t i = 0; i < keys_.size(); ++i) {
        if (keys_[i] == key) {
            keys_.erase(keys_.begin() + static_cast<long>(i));
            vals_.erase(vals_.begin() + static_cast<long>(i));
            return true;
        }
    }
    return false;
}

const Value* Value::at_path(const std::vector<std::string>& path) const {
    const Value* cur = this;
    for (auto& k : path) {
        cur = cur->find(k);
        if (!cur) return nullptr;
    }
    return cur;
}

bool Value::operator==(const Value& o) const {
    if (type_ != o.type_) {
        if ((type_ == Type::Integer || type_ == Type::Float) &&
            (o.type_ == Type::Integer || o.type_ == Type::Float))
            return as_float() == o.as_float();
        return false;
    }
    switch (type_) {
    case Type::None: return true;
    case Type::String: return str_ == o.str_;
    case Type::Integer: return int_ == o.int_;
    case Type::Float: return float_ == o.float_;
    case Type::Boolean: return bool_ == o.bool_;
    case Type::Array: return arr_ == o.arr_;
    case Type::Table: {
        if (keys_.size() != o.keys_.size()) return false;
        for (size_t i = 0; i < keys_.size(); ++i) {
            auto* ov = o.find(keys_[i]);
            if (!ov || !(*ov == vals_[i])) return false;
        }
        return true;
    }
    }
    return false;
}

// ============================================================================
// Parseur
// ============================================================================

namespace {

bool is_bare_key_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-';
}

void append_utf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) out += static_cast<char>(cp);
    else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

class Parser {
public:
    Parser(std::string_view text, std::string file) : s_(text), file_(std::move(file)) {}

    Value run() {
        Value root = Value::table();
        Value* current = &root;
        std::vector<std::vector<std::string>> defined;   // tables explicitement déclarées

        for (;;) {
            skip_blank_lines();
            if (eof()) break;
            if (peek() == '[') {
                bool array = peek(1) == '[';
                pos_ += array ? 2 : 1;
                skip_ws();
                auto path = parse_key_path();
                skip_ws();
                if (array) {
                    if (peek() != ']' || peek(1) != ']') fail("']]' attendu");
                    pos_ += 2;
                } else {
                    if (peek() != ']') fail("']' attendu");
                    ++pos_;
                }
                expect_end_of_line();
                current = array ? open_array_table(root, path) : open_table(root, path, defined);
                continue;
            }
            auto key = parse_key_path();
            skip_ws();
            if (peek() != '=') fail("'=' attendu après la clé '" + key.back() + "'");
            ++pos_;
            skip_ws();
            Value v = parse_value();
            assign(*current, key, std::move(v));
            expect_end_of_line();
        }
        return root;
    }

private:
    std::string_view s_;
    std::string file_;
    size_t pos_ = 0;

    // --- Position
    [[nodiscard]] bool eof() const { return pos_ >= s_.size(); }
    [[nodiscard]] char peek(size_t ahead = 0) const {
        return pos_ + ahead < s_.size() ? s_[pos_ + ahead] : '\0';
    }

    [[noreturn]] void fail(const std::string& msg) const {
        size_t line = 1, col = 1;
        for (size_t i = 0; i < pos_ && i < s_.size(); ++i) {
            if (s_[i] == '\n') { ++line; col = 1; }
            else ++col;
        }
        throw ParseError(msg, file_, line, col);
    }

    void skip_ws() {
        while (!eof() && (peek() == ' ' || peek() == '\t')) ++pos_;
    }
    void skip_comment() {
        if (peek() == '#')
            while (!eof() && peek() != '\n') ++pos_;
    }
    bool skip_newline() {
        if (peek() == '\n') { ++pos_; return true; }
        if (peek() == '\r' && peek(1) == '\n') { pos_ += 2; return true; }
        return false;
    }
    void skip_blank_lines() {
        for (;;) {
            skip_ws();
            skip_comment();
            if (!skip_newline()) return;
        }
    }
    /// Espaces, commentaires et sauts de ligne (dans les tableaux).
    void skip_ws_nl_comments() {
        for (;;) {
            skip_ws();
            skip_comment();
            if (!skip_newline()) return;
        }
    }
    void expect_end_of_line() {
        skip_ws();
        skip_comment();
        if (eof()) return;
        if (!skip_newline()) fail(std::format("fin de ligne attendue, '{}' trouvé", peek()));
    }

    // --- Clés
    std::string parse_simple_key() {
        char c = peek();
        if (c == '"') {
            if (peek(1) == '"' && peek(2) == '"') fail("clé multi-ligne interdite");
            return parse_basic_string();
        }
        if (c == '\'') return parse_literal_string();
        size_t start = pos_;
        while (!eof() && is_bare_key_char(peek())) ++pos_;
        if (pos_ == start) fail("clé attendue");
        return std::string(s_.substr(start, pos_ - start));
    }

    std::vector<std::string> parse_key_path() {
        std::vector<std::string> path;
        for (;;) {
            path.push_back(parse_simple_key());
            skip_ws();
            if (peek() != '.') break;
            ++pos_;
            skip_ws();
        }
        return path;
    }

    // --- Tables
    static std::string join_path(const std::vector<std::string>& p) {
        std::string r;
        for (size_t i = 0; i < p.size(); ++i) r += (i ? "." : "") + p[i];
        return r;
    }

    /// Descend dans une table (le dernier élément d'un tableau de tables).
    Value* descend(Value& t, const std::string& k, const std::vector<std::string>& path) {
        Value* v = t.find(k);
        if (!v) {
            Value n = Value::table();
            n.set_implicit(true);
            t.set(k, std::move(n));
            return t.find(k);
        }
        if (v->is_table()) {
            if (v->is_inline()) fail("la table en ligne '" + join_path(path) + "' ne peut pas être étendue");
            return v;
        }
        if (v->is_array() && v->is_table_array() && !v->items().empty()) return &v->items().back();
        fail("'" + join_path(path) + "' n'est pas une table");
    }

    Value* open_table(Value& root, const std::vector<std::string>& path,
                      std::vector<std::vector<std::string>>& defined) {
        Value* cur = &root;
        for (size_t i = 0; i + 1 < path.size(); ++i) cur = descend(*cur, path[i], path);
        Value* t = cur->find(path.back());
        if (!t) {
            cur->set(path.back(), Value::table());
            t = cur->find(path.back());
        } else if (!t->is_table()) {
            fail("'" + join_path(path) + "' est déjà défini et n'est pas une table");
        } else if (t->is_inline()) {
            fail("la table en ligne '" + join_path(path) + "' ne peut pas être redéfinie");
        } else if (!t->is_implicit() || std::find(defined.begin(), defined.end(), path) != defined.end()) {
            fail("table [" + join_path(path) + "] définie deux fois");
        }
        t->set_implicit(false);
        defined.push_back(path);
        return t;
    }

    Value* open_array_table(Value& root, const std::vector<std::string>& path) {
        Value* cur = &root;
        for (size_t i = 0; i + 1 < path.size(); ++i) cur = descend(*cur, path[i], path);
        Value* a = cur->find(path.back());
        if (!a) {
            Value arr = Value::array();
            arr.set_table_array(true);
            cur->set(path.back(), std::move(arr));
            a = cur->find(path.back());
        } else if (!a->is_array() || !a->is_table_array()) {
            fail("'" + join_path(path) + "' n'est pas un tableau de tables");
        }
        a->push_back(Value::table());
        return &a->items().back();
    }

    void assign(Value& table, const std::vector<std::string>& key, Value v) {
        Value* cur = &table;
        for (size_t i = 0; i + 1 < key.size(); ++i) {
            Value* n = cur->find(key[i]);
            if (!n) {
                cur->set(key[i], Value::table());
                n = cur->find(key[i]);
            } else if (!n->is_table() || n->is_inline()) {
                fail("'" + join_path(key) + "' : '" + key[i] + "' n'est pas une table");
            }
            cur = n;
        }
        if (cur->find(key.back())) fail("clé '" + join_path(key) + "' définie deux fois");
        cur->set(key.back(), std::move(v));
    }

    // --- Valeurs
    Value parse_value() {
        char c = peek();
        if (c == '"') {
            if (peek(1) == '"' && peek(2) == '"') return Value(parse_ml_basic_string());
            return Value(parse_basic_string());
        }
        if (c == '\'') {
            if (peek(1) == '\'' && peek(2) == '\'') return Value(parse_ml_literal_string());
            return Value(parse_literal_string());
        }
        if (c == '[') return parse_array();
        if (c == '{') return parse_inline_table();
        if (s_.substr(pos_).starts_with("true") && !is_bare_key_char(peek(4))) {
            pos_ += 4;
            return Value(true);
        }
        if (s_.substr(pos_).starts_with("false") && !is_bare_key_char(peek(5))) {
            pos_ += 5;
            return Value(false);
        }
        if (eof() || c == '\n' || c == '\r' || c == '#') fail("valeur manquante");
        return parse_number_or_date();
    }

    uint32_t parse_hex(int n) {
        uint32_t v = 0;
        for (int i = 0; i < n; ++i) {
            char c = peek();
            if (!std::isxdigit(static_cast<unsigned char>(c))) fail("séquence \\u invalide");
            v = v * 16 + static_cast<uint32_t>(std::isdigit(static_cast<unsigned char>(c)) ? c - '0'
                                                : (std::tolower(static_cast<unsigned char>(c)) - 'a' + 10));
            ++pos_;
        }
        return v;
    }

    void parse_escape(std::string& out) {
        ++pos_;   // '\'
        char c = peek();
        ++pos_;
        switch (c) {
        case 'b': out += '\b'; break;
        case 't': out += '\t'; break;
        case 'n': out += '\n'; break;
        case 'f': out += '\f'; break;
        case 'r': out += '\r'; break;
        case 'e': out += '\033'; break;
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case 'u': append_utf8(out, parse_hex(4)); break;
        case 'U': append_utf8(out, parse_hex(8)); break;
        default: --pos_; fail(std::format("séquence d'échappement invalide '\\{}'", c));
        }
    }

    std::string parse_basic_string() {
        ++pos_;
        std::string out;
        for (;;) {
            if (eof() || peek() == '\n') fail("chaîne non terminée");
            char c = peek();
            if (c == '"') { ++pos_; return out; }
            if (c == '\\') { parse_escape(out); continue; }
            out += c;
            ++pos_;
        }
    }

    std::string parse_literal_string() {
        ++pos_;
        size_t start = pos_;
        while (!eof() && peek() != '\'' && peek() != '\n') ++pos_;
        if (peek() != '\'') fail("chaîne littérale non terminée");
        std::string r(s_.substr(start, pos_ - start));
        ++pos_;
        return r;
    }

    std::string parse_ml_basic_string() {
        pos_ += 3;
        skip_newline();   // saut de ligne immédiat ignoré
        std::string out;
        for (;;) {
            if (eof()) fail("chaîne multi-ligne non terminée");
            if (peek() == '"' && peek(1) == '"' && peek(2) == '"') {
                pos_ += 3;
                // Jusqu'à deux guillemets supplémentaires font partie du contenu.
                while (peek() == '"' && out.size() < std::numeric_limits<size_t>::max()) {
                    out += '"';
                    ++pos_;
                }
                return out;
            }
            if (peek() == '\\') {
                // Barre oblique en fin de ligne : supprime blancs et sauts de ligne.
                size_t save = pos_;
                ++pos_;
                skip_ws();
                if (peek() == '\n' || peek() == '\r') {
                    while (!eof() && (peek() == ' ' || peek() == '\t' || peek() == '\n' || peek() == '\r')) ++pos_;
                    continue;
                }
                pos_ = save;
                parse_escape(out);
                continue;
            }
            out += peek();
            ++pos_;
        }
    }

    std::string parse_ml_literal_string() {
        pos_ += 3;
        skip_newline();
        size_t start = pos_;
        for (;;) {
            if (eof()) fail("chaîne littérale multi-ligne non terminée");
            if (peek() == '\'' && peek(1) == '\'' && peek(2) == '\'') {
                size_t end = pos_;
                pos_ += 3;
                while (peek() == '\'') { ++pos_; ++end; }
                return std::string(s_.substr(start, end - start));
            }
            ++pos_;
        }
    }

    Value parse_array() {
        ++pos_;
        Value arr = Value::array();
        for (;;) {
            skip_ws_nl_comments();
            if (peek() == ']') { ++pos_; return arr; }
            if (eof()) fail("tableau non terminé");
            arr.push_back(parse_value());
            skip_ws_nl_comments();
            if (peek() == ',') { ++pos_; continue; }
            if (peek() == ']') { ++pos_; return arr; }
            fail("',' ou ']' attendu dans le tableau");
        }
    }

    Value parse_inline_table() {
        ++pos_;
        Value t = Value::table();
        t.set_inline(true);
        skip_ws_nl_comments();
        if (peek() == '}') { ++pos_; return t; }
        for (;;) {
            skip_ws_nl_comments();
            auto key = parse_key_path();
            skip_ws();
            if (peek() != '=') fail("'=' attendu dans la table en ligne");
            ++pos_;
            skip_ws();
            assign(t, key, parse_value());
            skip_ws_nl_comments();
            if (peek() == ',') { ++pos_; continue; }
            if (peek() == '}') { ++pos_; return t; }
            fail("',' ou '}' attendu dans la table en ligne");
        }
    }

    Value parse_number_or_date() {
        size_t start = pos_;
        auto token_char = [](char c) {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '+' || c == '-' ||
                   c == '.' || c == ':';
        };
        while (!eof() && token_char(peek())) ++pos_;
        // Date-heure avec espace : « 1979-05-27 07:32:00 »
        if (pos_ - start == 10 && peek() == ' ' && std::isdigit(static_cast<unsigned char>(peek(1))) &&
            s_[start + 4] == '-') {
            ++pos_;
            while (!eof() && token_char(peek())) ++pos_;
        }
        std::string tok(s_.substr(start, pos_ - start));
        if (tok.empty()) fail(std::format("valeur inattendue '{}'", peek()));

        // Dates/heures : conservées comme chaînes.
        if (tok.size() >= 8 && ((tok[4] == '-' && std::isdigit(static_cast<unsigned char>(tok[0]))) ||
                                (tok.size() >= 5 && tok[2] == ':')))
            return Value(tok);

        std::string clean;
        for (size_t i = 0; i < tok.size(); ++i) {
            if (tok[i] == '_') {
                if (i == 0 || i + 1 >= tok.size() || !std::isalnum(static_cast<unsigned char>(tok[i - 1])) ||
                    !std::isalnum(static_cast<unsigned char>(tok[i + 1])))
                    fail("'_' mal placé dans le nombre '" + tok + "'");
                continue;
            }
            clean += tok[i];
        }
        std::string body = clean;
        bool neg = false;
        if (!body.empty() && (body[0] == '+' || body[0] == '-')) {
            neg = body[0] == '-';
            body = body.substr(1);
        }
        if (body == "inf") return Value(neg ? -std::numeric_limits<double>::infinity()
                                            : std::numeric_limits<double>::infinity());
        if (body == "nan") return Value(std::numeric_limits<double>::quiet_NaN());

        int base = 10;
        if (body.starts_with("0x")) base = 16;
        else if (body.starts_with("0o")) base = 8;
        else if (body.starts_with("0b")) base = 2;
        if (base != 10) {
            if (clean[0] == '+' || clean[0] == '-') fail("signe interdit devant un entier non décimal");
            errno = 0;
            char* end = nullptr;
            long long v = std::strtoll(body.c_str() + 2, &end, base);
            if (errno || !end || *end || body.size() == 2) fail("entier invalide '" + tok + "'");
            return Value(static_cast<int64_t>(v));
        }
        bool is_float = body.find_first_of(".eE") != std::string::npos;
        if (body.empty() || !std::isdigit(static_cast<unsigned char>(body[0])))
            fail("valeur inattendue '" + tok + "'");
        errno = 0;
        char* end = nullptr;
        if (is_float) {
            double d = std::strtod(clean.c_str(), &end);
            if (errno || !end || *end) fail("flottant invalide '" + tok + "'");
            return Value(d);
        }
        if (body.size() > 1 && body[0] == '0') fail("zéro initial interdit dans '" + tok + "'");
        long long v = std::strtoll(clean.c_str(), &end, 10);
        if (errno || !end || *end) fail("entier invalide '" + tok + "'");
        return Value(static_cast<int64_t>(v));
    }
};

} // namespace

Value parse(std::string_view text, const std::string& file) {
    return Parser(text, file).run();
}

Value parse_file(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw ParseError("fichier introuvable ou illisible", p.string());
    std::ostringstream ss;
    ss << f.rdbuf();
    return parse(ss.str(), p.string());
}

// ============================================================================
// Sérialisation
// ============================================================================

std::string quote(std::string_view s) {
    std::string r = "\"";
    for (unsigned char c : s) {
        switch (c) {
        case '"': r += "\\\""; break;
        case '\\': r += "\\\\"; break;
        case '\n': r += "\\n"; break;
        case '\t': r += "\\t"; break;
        case '\r': r += "\\r"; break;
        case '\b': r += "\\b"; break;
        case '\f': r += "\\f"; break;
        default:
            if (c < 0x20 || c == 0x7f) r += std::format("\\u{:04X}", c);
            else r += static_cast<char>(c);
        }
    }
    return r + "\"";
}

std::string format_key(std::string_view key) {
    if (!key.empty() && std::all_of(key.begin(), key.end(), is_bare_key_char)) return std::string(key);
    return quote(key);
}

namespace {
std::string format_float(double d) {
    if (std::isnan(d)) return "nan";
    if (std::isinf(d)) return d < 0 ? "-inf" : "inf";
    std::string s = std::format("{}", d);
    if (s.find_first_of(".e") == std::string::npos) s += ".0";
    return s;
}
} // namespace

std::string format_value(const Value& v) {
    switch (v.type()) {
    case Type::None: return "\"\"";
    case Type::String: return quote(v.as_string());
    case Type::Integer: return std::to_string(v.as_integer());
    case Type::Float: return format_float(v.as_float());
    case Type::Boolean: return v.as_bool() ? "true" : "false";
    case Type::Array: {
        std::string r = "[";
        for (size_t i = 0; i < v.items().size(); ++i) {
            if (i) r += ", ";
            r += format_value(v.items()[i]);
        }
        return r + "]";
    }
    case Type::Table: {
        if (v.size() == 0) return "{}";
        std::string r = "{ ";
        for (size_t i = 0; i < v.size(); ++i) {
            if (i) r += ", ";
            r += format_key(v.keys()[i]) + " = " + format_value(v.value_at(i));
        }
        return r + " }";
    }
    }
    return "\"\"";
}

namespace {

bool is_section_value(const Value& v) {
    if (v.is_table()) return !v.is_inline();
    if (v.is_array() && v.is_table_array()) return true;
    return false;
}

void serialize_table(const Value& t, const std::string& prefix, std::string& out, bool emit_header) {
    bool has_kv = false;
    for (size_t i = 0; i < t.size(); ++i)
        if (!is_section_value(t.value_at(i))) has_kv = true;
    bool has_sub = false;
    for (size_t i = 0; i < t.size(); ++i)
        if (is_section_value(t.value_at(i))) has_sub = true;

    if (emit_header && (has_kv || !has_sub)) {
        if (!out.empty()) out += "\n";
        out += "[" + prefix + "]\n";
    }
    for (size_t i = 0; i < t.size(); ++i) {
        auto& v = t.value_at(i);
        if (is_section_value(v)) continue;
        out += format_key(t.keys()[i]) + " = " + format_value(v) + "\n";
    }
    for (size_t i = 0; i < t.size(); ++i) {
        auto& v = t.value_at(i);
        std::string path = (prefix.empty() ? "" : prefix + ".") + format_key(t.keys()[i]);
        if (v.is_table() && !v.is_inline()) {
            serialize_table(v, path, out, true);
        } else if (v.is_array() && v.is_table_array()) {
            for (auto& item : v.items()) {
                if (!out.empty()) out += "\n";
                out += "[[" + path + "]]\n";
                std::string body;
                serialize_table(item, path, body, false);
                out += body;
            }
        }
    }
}

} // namespace

std::string serialize(const Value& root) {
    std::string out;
    serialize_table(root, "", out, false);
    return out;
}

// ============================================================================
// Saisie utilisateur
// ============================================================================

namespace {
std::string trim_copy(std::string_view s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string_view::npos) return {};
    size_t e = s.find_last_not_of(" \t\r\n");
    return std::string(s.substr(b, e - b + 1));
}

std::optional<Value> parse_literal(std::string_view text) {
    try {
        Value t = parse("v = " + std::string(text));
        if (auto* v = t.find("v")) return *v;
    } catch (const ParseError&) {}
    return std::nullopt;
}
} // namespace

std::optional<Value> value_from_text(std::string_view raw, Type expected, std::string* error) {
    std::string text = trim_copy(raw);
    auto err = [&](const std::string& m) -> std::optional<Value> {
        if (error) *error = m;
        return std::nullopt;
    };
    switch (expected) {
    case Type::String:
        if (text.size() >= 2 && (text.front() == '"' || text.front() == '\'') && text.back() == text.front())
            if (auto v = parse_literal(text); v && v->is_string()) return v;
        return Value(std::string(raw == text ? text : trim_copy(raw)));
    case Type::Integer: {
        auto v = parse_literal(text);
        if (v && v->is_integer()) return v;
        return err("entier attendu : '" + text + "'");
    }
    case Type::Float: {
        auto v = parse_literal(text);
        if (v && (v->is_float() || v->is_integer())) return Value(v->as_float());
        return err("nombre attendu : '" + text + "'");
    }
    case Type::Boolean: {
        std::string l;
        for (char c : text) l += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (l == "true" || l == "oui" || l == "yes" || l == "on" || l == "1" || l == "vrai") return Value(true);
        if (l == "false" || l == "non" || l == "no" || l == "off" || l == "0" || l == "faux") return Value(false);
        return err("booléen attendu (true/false) : '" + text + "'");
    }
    case Type::Array: {
        if (text.starts_with("[")) {
            auto v = parse_literal(text);
            if (v && v->is_array()) return v;
            return err("tableau TOML invalide : '" + text + "'");
        }
        Value arr = Value::array();
        size_t start = 0;
        while (start <= text.size()) {
            size_t end = text.find(',', start);
            if (end == std::string::npos) end = text.size();
            auto item = trim_copy(std::string_view(text).substr(start, end - start));
            if (!item.empty()) arr.push_back(Value(item));
            start = end + 1;
        }
        return arr;
    }
    case Type::Table: {
        auto v = parse_literal(text.empty() ? "{}" : text);
        if (v && v->is_table()) return v;
        return err("table en ligne attendue ({ cle = \"valeur\" }) : '" + text + "'");
    }
    case Type::None: {
        if (auto v = parse_literal(text)) return v;
        return Value(text);
    }
    }
    return err("type inconnu");
}

std::string value_to_text(const Value& v) {
    if (v.is_string()) return v.as_string();
    if (v.is_none()) return {};
    if (v.is_array()) {
        bool ok = false;
        auto strs = v.as_strings(&ok);
        if (ok) {
            bool plain = std::all_of(strs.begin(), strs.end(), [](const std::string& s) {
                return s.find(',') == std::string::npos && trim_copy(s) == s && !s.empty();
            });
            if (plain) {
                std::string r;
                for (size_t i = 0; i < strs.size(); ++i) r += (i ? ", " : "") + strs[i];
                return r;
            }
        }
    }
    return format_value(v);
}

} // namespace toml
