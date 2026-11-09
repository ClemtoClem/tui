/* @file io/TomlDocument.cpp */

#include "io/TomlDocument.hpp"

#include "core/Util.hpp"

#include <algorithm>
#include <cctype>

namespace toml {

std::vector<std::string> split_path(std::string_view dotted) {
    std::vector<std::string> r;
    std::string cur;
    for (char c : dotted) {
        if (c == '.') {
            if (!cur.empty()) r.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) r.push_back(cur);
    return r;
}

std::string join_path(const std::vector<std::string>& path) {
    std::string r;
    for (size_t i = 0; i < path.size(); ++i) r += (i ? "." : "") + format_key(path[i]);
    return r;
}

namespace {

bool bare(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-'; }

/// Analyse un chemin de clé à partir de `i` ; s'arrête sur '=' ou ']'.
std::vector<std::string> read_key_path(const std::string& s, size_t& i) {
    std::vector<std::string> path;
    auto ws = [&] { while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i; };
    for (;;) {
        ws();
        std::string k;
        if (i < s.size() && (s[i] == '"' || s[i] == '\'')) {
            char q = s[i++];
            while (i < s.size() && s[i] != q) {
                if (q == '"' && s[i] == '\\' && i + 1 < s.size()) { k += s[i + 1]; i += 2; continue; }
                k += s[i++];
            }
            ++i;
        } else {
            while (i < s.size() && bare(s[i])) k += s[i++];
        }
        path.push_back(k);
        ws();
        if (i < s.size() && s[i] == '.') { ++i; continue; }
        return path;
    }
}

/// État lexical inter-lignes d'une valeur en cours d'analyse.
struct Lex {
    int depth = 0;
    int ml = 0;   // 0 : aucune ; 1 : """ ; 2 : '''
    [[nodiscard]] bool open() const { return depth > 0 || ml != 0; }
};

/// Parcourt `line` à partir de `i` ; renvoie la position du commentaire
/// (std::string::npos si aucun).
size_t scan_chars(const std::string& line, size_t i, Lex& st) {
    while (i < line.size()) {
        char c = line[i];
        if (st.ml == 1) {
            if (line.compare(i, 3, "\"\"\"") == 0) { st.ml = 0; i += 3; continue; }
            if (c == '\\') { i += 2; continue; }
            ++i;
            continue;
        }
        if (st.ml == 2) {
            if (line.compare(i, 3, "'''") == 0) { st.ml = 0; i += 3; continue; }
            ++i;
            continue;
        }
        if (c == '"') {
            if (line.compare(i, 3, "\"\"\"") == 0) { st.ml = 1; i += 3; continue; }
            ++i;
            while (i < line.size() && line[i] != '"') {
                if (line[i] == '\\') ++i;
                ++i;
            }
            ++i;
            continue;
        }
        if (c == '\'') {
            if (line.compare(i, 3, "'''") == 0) { st.ml = 2; i += 3; continue; }
            ++i;
            while (i < line.size() && line[i] != '\'') ++i;
            ++i;
            continue;
        }
        if (c == '[' || c == '{') ++st.depth;
        else if (c == ']' || c == '}') --st.depth;
        else if (c == '#') return i;
        ++i;
    }
    return std::string::npos;
}

bool starts_with(const std::vector<std::string>& v, const std::vector<std::string>& prefix) {
    return v.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), v.begin());
}

std::vector<std::string> concat(const std::vector<std::string>& a, const std::vector<std::string>& b) {
    std::vector<std::string> r = a;
    r.insert(r.end(), b.begin(), b.end());
    return r;
}

bool is_blank(const std::string& l) { return util::trim(l).empty(); }

/// Recolle le commentaire de fin de ligne à sa colonne d'origine si possible.
template <class S>
void append_comment(std::string& line, const S& s) {
    if (s.comment.empty()) return;
    // Largeur affichée (UTF-8) de la ligne.
    size_t w = 0;
    for (unsigned char c : line) w += (c & 0xC0) != 0x80;
    line += w + 1 <= s.comment_col ? std::string(s.comment_col - w, ' ') : std::string(" ");
    line += s.comment;
}

} // namespace

// ============================================================================

Document Document::load(const std::filesystem::path& p) {
    return from_text(util::read_file_or_throw(p), p);
}

Document Document::from_text(std::string text, std::filesystem::path p) {
    Document d;
    d.path_ = std::move(p);
    d.data_ = parse(text, d.path_.string());
    d.text_ = std::move(text);
    d.original_ = d.text_;
    return d;
}

const Value* Document::get(const std::vector<std::string>& table, const std::string& key) const {
    const Value* t = table.empty() ? &data_ : data_.at_path(table);
    return t ? t->find(key) : nullptr;
}

bool Document::has_table(const std::vector<std::string>& table) const {
    const Value* t = table.empty() ? &data_ : data_.at_path(table);
    return t && t->is_table();
}

std::vector<std::string> Document::lines() const {
    auto r = util::split(text_, '\n', true);
    if (!r.empty() && r.back().empty()) r.pop_back();   // fin de fichier
    for (auto& l : r)
        if (!l.empty() && l.back() == '\r') l.pop_back();
    return r;
}

std::vector<Document::Stmt> Document::scan(const std::vector<std::string>& ls) const {
    std::vector<Stmt> out;
    std::vector<std::string> table;
    for (size_t i = 0; i < ls.size(); ++i) {
        const std::string& line = ls[i];
        size_t b = line.find_first_not_of(" \t");
        if (b == std::string::npos || line[b] == '#') continue;
        Stmt st{};
        st.indent = line.substr(0, b);
        st.first = st.last = i;
        if (line[b] == '[') {
            bool array = b + 1 < line.size() && line[b + 1] == '[';
            size_t j = b + (array ? 2 : 1);
            st.kind = array ? Stmt::ArrayHeader : Stmt::Header;
            st.table = read_key_path(line, j);
            table = st.table;
            out.push_back(std::move(st));
            continue;
        }
        size_t j = b;
        st.kind = Stmt::KeyValue;
        st.table = table;
        st.key = read_key_path(line, j);
        if (j < line.size() && line[j] == '=') ++j;
        Lex lex;
        size_t cpos = scan_chars(line, j, lex);
        size_t k = i;
        while (lex.open() && k + 1 < ls.size()) {
            ++k;
            cpos = scan_chars(ls[k], 0, lex);
        }
        st.last = k;
        if (st.first == st.last && cpos != std::string::npos) {
            st.comment = line.substr(cpos);
            st.comment_col = 0;
            for (unsigned char c : line.substr(0, cpos)) st.comment_col += (c & 0xC0) != 0x80;
        }
        out.push_back(std::move(st));
        i = k;
    }
    return out;
}

void Document::commit(const std::vector<std::string>& ls) {
    std::string t;
    for (auto& l : ls) t += l + "\n";
    // Pas plus d'une ligne vide en fin de fichier.
    while (t.size() >= 2 && t[t.size() - 1] == '\n' && t[t.size() - 2] == '\n') t.pop_back();
    Value v = parse(t, path_.string());   // lève si invalide : document inchangé
    text_ = std::move(t);
    data_ = std::move(v);
}

void Document::replace_text(std::string text) {
    Value v = parse(text, path_.string());
    text_ = std::move(text);
    data_ = std::move(v);
}

void Document::set(const std::vector<std::string>& table, const std::string& key, const Value& v) {
    auto ls = lines();
    auto stmts = scan(ls);
    const auto full = concat(table, {key});

    // 1. La clé existe : remplacement de l'instruction (mono ou multi-ligne).
    for (auto& s : stmts) {
        if (s.kind != Stmt::KeyValue || concat(s.table, s.key) != full) continue;
        std::string line = s.indent + join_path(s.key) + " = " + format_value(v);
        append_comment(line, s);
        ls.erase(ls.begin() + static_cast<long>(s.first), ls.begin() + static_cast<long>(s.last) + 1);
        ls.insert(ls.begin() + static_cast<long>(s.first), line);
        commit(ls);
        return;
    }

    // 2. La table est une valeur en ligne (cle = { ... }) : réécriture de la valeur.
    for (auto& s : stmts) {
        if (s.kind != Stmt::KeyValue || concat(s.table, s.key) != table) continue;
        const Value* tv = data_.at_path(table);
        if (!tv || !tv->is_table()) continue;   // pas une table en ligne
        Value nv = *tv;
        nv.set_inline(true);
        nv.set(key, v);
        std::string line = s.indent + join_path(s.key) + " = " + format_value(nv);
        append_comment(line, s);
        ls.erase(ls.begin() + static_cast<long>(s.first), ls.begin() + static_cast<long>(s.last) + 1);
        ls.insert(ls.begin() + static_cast<long>(s.first), line);
        commit(ls);
        return;
    }

    // 3. En-tête [table] présente : insertion après sa dernière clé.
    long header = -1;
    for (auto& s : stmts)
        if (s.kind == Stmt::Header && s.table == table) header = static_cast<long>(s.first);
    if (header >= 0 || table.empty()) {
        long insert_after = header;   // -1 : début de fichier (racine)
        long region_end = static_cast<long>(ls.size());
        for (auto& s : stmts) {
            if ((s.kind == Stmt::Header || s.kind == Stmt::ArrayHeader) && static_cast<long>(s.first) > header) {
                region_end = static_cast<long>(s.first);
                break;
            }
        }
        for (auto& s : stmts)
            if (s.kind == Stmt::KeyValue && static_cast<long>(s.first) > header &&
                static_cast<long>(s.first) < region_end && s.table == table)
                insert_after = static_cast<long>(s.last);
        std::string line = format_key(key) + " = " + format_value(v);
        if (table.empty() && insert_after < 0) {
            // Racine sans clé : avant la première en-tête (après les commentaires de tête).
            long pos = 0;
            while (pos < region_end && !ls.empty() && util::trim(ls[static_cast<size_t>(pos)]).starts_with("#")) ++pos;
            ls.insert(ls.begin() + pos, line);
            if (pos + 1 < static_cast<long>(ls.size()) && !is_blank(ls[static_cast<size_t>(pos + 1)]))
                ls.insert(ls.begin() + pos + 1, "");
        } else {
            ls.insert(ls.begin() + insert_after + 1, line);
        }
        commit(ls);
        return;
    }

    // 4. Table définie par des clés pointées (a.b = ...) dans une table parente.
    for (auto it = stmts.rbegin(); it != stmts.rend(); ++it) {
        auto& s = *it;
        if (s.kind != Stmt::KeyValue) continue;
        auto fp = concat(s.table, s.key);
        if (!starts_with(table, s.table) || !starts_with(fp, table) || fp.size() <= table.size()) continue;
        std::vector<std::string> rel(table.begin() + static_cast<long>(s.table.size()), table.end());
        rel.push_back(key);
        ls.insert(ls.begin() + static_cast<long>(s.last) + 1, s.indent + join_path(rel) + " = " + format_value(v));
        commit(ls);
        return;
    }

    // 5. Nouvelle section en fin de fichier.
    if (!ls.empty() && !is_blank(ls.back())) ls.push_back("");
    ls.push_back("[" + join_path(table) + "]");
    ls.push_back(format_key(key) + " = " + format_value(v));
    commit(ls);
}

bool Document::remove(const std::vector<std::string>& table, const std::string& key) {
    auto ls = lines();
    auto stmts = scan(ls);
    const auto full = concat(table, {key});
    for (auto& s : stmts) {
        if (s.kind != Stmt::KeyValue) continue;
        auto fp = concat(s.table, s.key);
        if (fp == full) {
            ls.erase(ls.begin() + static_cast<long>(s.first), ls.begin() + static_cast<long>(s.last) + 1);
            commit(ls);
            return true;
        }
        if (fp == table) {   // table en ligne
            const Value* tv = data_.at_path(table);
            if (!tv || !tv->is_table() || !tv->find(key)) return false;
            Value nv = *tv;
            nv.set_inline(true);
            nv.erase(key);
            std::string line = s.indent + join_path(s.key) + " = " + format_value(nv);
            append_comment(line, s);
            ls.erase(ls.begin() + static_cast<long>(s.first), ls.begin() + static_cast<long>(s.last) + 1);
            ls.insert(ls.begin() + static_cast<long>(s.first), line);
            commit(ls);
            return true;
        }
    }
    return false;
}

void Document::ensure_table(const std::vector<std::string>& table) {
    if (table.empty() || has_table(table)) return;
    auto ls = lines();
    if (!ls.empty() && !is_blank(ls.back())) ls.push_back("");
    ls.push_back("[" + join_path(table) + "]");
    commit(ls);
}

bool Document::remove_table(const std::vector<std::string>& table) {
    auto ls = lines();
    auto stmts = scan(ls);
    std::vector<char> drop(ls.size(), 0);
    bool any = false;
    for (size_t i = 0; i < stmts.size(); ++i) {
        auto& s = stmts[i];
        bool header = s.kind == Stmt::Header || s.kind == Stmt::ArrayHeader;
        if (header && starts_with(s.table, table)) {
            size_t end = ls.size();
            for (size_t j = i + 1; j < stmts.size(); ++j) {
                if (stmts[j].kind != Stmt::KeyValue) { end = stmts[j].first; break; }
            }
            // Les commentaires qui précèdent l'en-tête suivante lui appartiennent.
            while (end > s.first + 1 && (is_blank(ls[end - 1]) || util::trim(ls[end - 1]).starts_with("#"))) --end;
            for (size_t k = s.first; k < end; ++k) drop[k] = 1;
            any = true;
        } else if (s.kind == Stmt::KeyValue && starts_with(concat(s.table, s.key), table)) {
            for (size_t k = s.first; k <= s.last; ++k) drop[k] = 1;
            any = true;
        }
    }
    if (!any) return false;
    std::vector<std::string> kept;
    for (size_t i = 0; i < ls.size(); ++i) {
        if (drop[i]) continue;
        if (is_blank(ls[i]) && !kept.empty() && is_blank(kept.back())) continue;
        kept.push_back(ls[i]);
    }
    while (!kept.empty() && is_blank(kept.front())) kept.erase(kept.begin());
    commit(kept);
    return true;
}

bool Document::rename_table(const std::vector<std::string>& old_path, const std::vector<std::string>& new_path) {
    auto ls = lines();
    auto stmts = scan(ls);
    bool any = false;
    for (auto& s : stmts) {
        bool header = s.kind == Stmt::Header || s.kind == Stmt::ArrayHeader;
        if (!header || !starts_with(s.table, old_path)) continue;
        std::vector<std::string> np = new_path;
        np.insert(np.end(), s.table.begin() + static_cast<long>(old_path.size()), s.table.end());
        std::string& line = ls[s.first];
        size_t close = line.find(']');
        std::string tail = close == std::string::npos ? "]" : line.substr(close);
        line = s.indent + (s.kind == Stmt::ArrayHeader ? "[[" : "[") + join_path(np) + tail;
        any = true;
    }
    if (any) commit(ls);
    return any;
}

void Document::save(const std::filesystem::path& p) {
    auto target = p.empty() ? path_ : p;
    util::write_file(target, text_);
    original_ = text_;
    if (!p.empty()) path_ = p;
}

} // namespace toml
