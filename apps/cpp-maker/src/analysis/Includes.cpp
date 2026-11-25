/* @file analysis/Includes.cpp */

#include "analysis/Includes.hpp"

#include "build/Planner.hpp"
#include "core/Util.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace analysis {

std::vector<std::string> scan_includes(const fs::path& file) {
    std::vector<std::string> out;
    std::string content;
    if (!util::read_file(file, content)) return out;
    size_t i = 0;
    while ((i = content.find("#include", i)) != std::string::npos) {
        size_t j = i + 8;
        while (j < content.size() && (content[j] == ' ' || content[j] == '\t')) ++j;
        if (j >= content.size()) break;
        char open = content[j];
        if (open != '"' && open != '<') {
            i += 8;
            continue;
        }
        char close = open == '"' ? '"' : '>';
        auto end = content.find(close, j + 1);
        if (end == std::string::npos || end > content.find('\n', j)) {
            i = j + 1;
            continue;
        }
        out.push_back(content.substr(j + 1, end - j - 1));
        i = end + 1;
    }
    return out;
}

namespace {

bool is_ident_start(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
bool is_ident_char(char c) { return is_ident_start(c) || (c >= '0' && c <= '9'); }
bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }

/// Nom de module C++ : identifiant (« . » identifiant)* (s'arrête avant « : »,
/// utilisé pour les partitions « import x:part; »).
std::string read_module_name(std::string_view s, size_t& i) {
    size_t start = i;
    while (i < s.size()) {
        if (is_ident_char(s[i])) { ++i; continue; }
        if (s[i] == '.' && i + 1 < s.size() && is_ident_start(s[i + 1])) { i += 2; continue; }
        break;
    }
    std::string name(s.substr(start, i - start));
    while (!name.empty() && name.back() == '.') name.pop_back();
    return name;
}

struct ModuleScan {
    std::vector<std::string> imports;   ///< modules importés
    std::vector<std::string> decls;     ///< modules déclarés
};

/// Parcours lexical léger : ignore commentaires et littéraux, repère les mots
/// « import », « export » et « module » et en extrait les noms de modules.
ModuleScan scan_module_statements(const fs::path& file) {
    ModuleScan r;
    std::string c;
    if (!util::read_file(file, c)) return r;
    size_t i = 0, n = c.size();
    while (i < n) {
        char ch = c[i];
        if (is_space(ch)) { ++i; continue; }
        if (ch == '/' && i + 1 < n && c[i + 1] == '/') {           // commentaire ligne
            i += 2;
            while (i < n && c[i] != '\n') ++i;
            continue;
        }
        if (ch == '/' && i + 1 < n && c[i + 1] == '*') {           // commentaire bloc
            i += 2;
            while (i + 1 < n && !(c[i] == '*' && c[i + 1] == '/')) ++i;
            i += 2;
            continue;
        }
        if (ch == '"' || ch == '\'') {                             // littéral
            char q = ch;
            ++i;
            while (i < n) {
                if (c[i] == '\\') { i += 2; continue; }
                if (c[i] == q) { ++i; break; }
                ++i;
            }
            continue;
        }
        if (!is_ident_start(ch)) { ++i; continue; }

        size_t start = i;
        while (i < n && is_ident_char(c[i])) ++i;
        std::string_view word(c.data() + start, i - start);

        if (word == "import") {
            size_t k = i;
            while (k < n && is_space(c[k])) ++k;
            if (k < n && c[k] != '<' && c[k] != '"') {
                std::string name = read_module_name(c, k);
                if (!name.empty()) util::push_unique(r.imports, name);
            }
            continue;
        }
        if (word == "export") {
            size_t k = i;
            while (k < n && is_space(c[k])) ++k;
            size_t k2 = k;
            while (k2 < n && is_ident_char(c[k2])) ++k2;
            std::string_view second(c.data() + k, k2 - k);
            if (second == "import" || second == "module") {
                size_t m = k2;
                while (m < n && is_space(c[m])) ++m;
                // « export module; » / « export import <…>; » : pas de nom.
                if (m < n && c[m] != ';' && c[m] != '<' && c[m] != '"') {
                    std::string name = read_module_name(c, m);
                    if (!name.empty()) {
                        if (second == "import") util::push_unique(r.imports, name);
                        else util::push_unique(r.decls, name);
                    }
                }
                i = k2;   // ne pas retraiter « import » / « module »
            }
            continue;
        }
        if (word == "module") {
            size_t k = i;
            while (k < n && is_space(c[k])) ++k;
            if (k < n && c[k] != ';' && c[k] != '<' && c[k] != '"') {
                std::string name = read_module_name(c, k);
                if (!name.empty()) util::push_unique(r.decls, name);
            }
            continue;
        }
    }
    return r;
}

bool is_header(const fs::path& p) {
    static const std::set<std::string> ext = {".h", ".hpp", ".hh", ".hxx", ".h++", ".ipp", ".tpp", ".inl"};
    return ext.count(p.extension().string()) > 0;
}

/// Fichier principal d'un module : include/<nom>/<nom>.<ext>.
bool is_main_header(const fs::path& rel, const std::string& name) {
    return rel.parent_path().string() == name && rel.stem().string() == name;
}

/// Ajoute « key → id » en conservant l'ordre et sans doublon.
void add_key(std::map<std::string, std::vector<std::string>>& index, const std::string& key,
             const std::string& id) {
    auto& v = index[key];
    if (std::find(v.begin(), v.end(), id) == v.end()) v.push_back(id);
}

/// Fichiers à analyser d'une cible : sources (.cpp/.c…), interfaces de modules
/// (.cppm/.ixx/.mpp) et en-têtes publics + privés.
std::vector<fs::path> target_files(const model::Workspace& ws, const model::TargetConfig& t) {
    std::vector<fs::path> files;
    for (auto& s : build::discover_sources(ws, t, t.source_dirs)) files.push_back(s.path);
    std::error_code ec;
    for (auto& d : t.source_dirs) {
        fs::path dir = util::under(ws.abs(t), d);
        if (!fs::is_directory(dir, ec)) continue;
        for (auto& e : fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
            if (ec) break;
            if (!e.is_regular_file(ec)) continue;
            auto ext = e.path().extension().string();
            if (ext == ".cppm" || ext == ".ixx" || ext == ".mpp") files.push_back(e.path());
        }
    }
    std::vector<std::string> hdr_dirs = t.public_includes;
    hdr_dirs.insert(hdr_dirs.end(), t.private_includes.begin(), t.private_includes.end());
    for (auto& d : hdr_dirs) {
        fs::path dir = util::under(ws.abs(t), d);
        if (!fs::is_directory(dir, ec)) continue;
        for (auto& e : fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
            if (ec) break;
            if (e.is_regular_file(ec) && is_header(e.path())) files.push_back(e.path());
        }
    }
    std::sort(files.begin(), files.end());
    files.erase(std::unique(files.begin(), files.end()), files.end());
    return files;
}

} // namespace

std::vector<std::string> scan_imports(const fs::path& file) { return scan_module_statements(file).imports; }
std::vector<std::string> scan_module_decls(const fs::path& file) { return scan_module_statements(file).decls; }

std::vector<MissingDep> detect_missing_deps(const model::Workspace& ws) {
    // --- Index des en-têtes publics : clé → modules.
    //   clés : chemin complet (name/name.hpp), chemin qualifié (layer/name/name.hpp),
    //   raccourci du fichier principal (name) et raccourci qualifié (layer/name).
    std::map<std::string, std::vector<std::string>> header_index;
    // --- Index des noms de modules C++ déclarés (« export module x.y; »).
    std::map<std::string, std::vector<std::string>> module_index;
    std::error_code ec;
    for (auto& t : ws.targets) {
        if (!t.is_module()) continue;
        for (auto& inc : t.public_includes) {
            fs::path root = util::under(ws.abs(t), inc);
            if (!fs::is_directory(root, ec)) continue;
            for (auto& e : fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec)) {
                if (ec) break;
                if (!e.is_regular_file(ec) || !is_header(e.path())) continue;
                std::string rel = e.path().lexically_relative(root).generic_string();
                add_key(header_index, rel, t.id);
                add_key(header_index, t.layer + "/" + rel, t.id);
                if (is_main_header(fs::path(rel), t.name)) {
                    add_key(header_index, t.name, t.id);
                    add_key(header_index, t.layer + "/" + t.name, t.id);
                }
            }
        }
        for (auto& f : target_files(ws, t))
            for (auto& name : scan_module_decls(f)) add_key(module_index, name, t.id);
    }

    std::vector<MissingDep> out;
    std::set<std::string> seen;
    for (auto& t : ws.targets) {
        if (t.from_git) continue;
        std::set<std::string> reachable;
        try {
            for (auto& d : ws.closure(t.id)) reachable.insert(d);
        } catch (const util::Error&) {
            continue;   // cycle : signalé par la validation
        }

        // Signale une référence (en-tête ou module) non couverte par les
        // dépendances déclarées. `cands` est la liste des modules candidats
        // (plusieurs = ambiguïté).
        auto report = [&](const std::string& ref, const fs::path& file, bool via_import,
                          std::vector<std::string> cands) {
            if (cands.empty()) return;
            std::sort(cands.begin(), cands.end());
            cands.erase(std::unique(cands.begin(), cands.end()), cands.end());
            // Préférence au même layer (comme resolve_module_ref) : un candidat
            // du layer de la cible lève l'ambiguïté (auto-inclusion comprise).
            if (cands.size() > 1) {
                std::vector<std::string> same;
                for (auto& id : cands)
                    if (auto* m = ws.find(id); m && m->layer == t.layer) same.push_back(id);
                if (same.size() == 1) cands = std::move(same);
            }
            if (cands.size() == 1) {
                if (cands[0] == t.id || reachable.count(cands[0])) return;
                std::string key = t.id + "\x1f" + cands[0];
                if (!seen.insert(key).second) return;
                out.push_back({t.id, cands[0], {}, ref, file, via_import});
                return;
            }
            // Plusieurs candidats : ambiguïté à lever en précisant le layer.
            bool any = false;
            for (auto& id : cands)
                if (id != t.id && !reachable.count(id)) { any = true; break; }
            if (!any) return;
            std::string key = t.id + "\x1f" + ref + (via_import ? "\x1fimp" : "");
            if (!seen.insert(key).second) return;
            out.push_back({t.id, "", cands, ref, file, via_import});
        };

        for (auto& f : target_files(ws, t)) {
            for (auto& inc : scan_includes(f)) {
                auto it = header_index.find(inc);
                std::vector<std::string> cands = it == header_index.end() ? std::vector<std::string>{} : it->second;
                report(inc, f, false, std::move(cands));
            }
            for (auto& name : scan_imports(f)) {
                auto it = module_index.find(name);
                std::vector<std::string> cands = it == module_index.end()
                                                     ? ws.resolve_module_ref_noexcept(name, t.layer)
                                                     : it->second;
                report(name, f, true, std::move(cands));
            }
        }
    }
    return out;
}

} // namespace analysis
