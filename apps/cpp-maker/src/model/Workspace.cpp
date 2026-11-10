/* @file model/Workspace.cpp */

#include "model/Workspace.hpp"

#include "core/Term.hpp"
#include "core/Util.hpp"
#include "core/Version.hpp"
#include "io/toml.hpp"
#include "io/TomlDocument.hpp"
#include "model/GitDeps.hpp"
#include "model/Schema.hpp"
#include "toolchain/Sanitize.hpp"

#include <algorithm>
#include <functional>
#include <set>

namespace model {

// ============================================================================
// Lecture typée d'un fichier TOML
// ============================================================================

namespace {

class Reader {
public:
    Reader(const toml::Value& root, std::string file) : root_(root), file_(std::move(file)) {}

    [[nodiscard]] const toml::Value& root() const { return root_; }
    [[nodiscard]] const std::string& file() const { return file_; }

    /// Table à `path` ; nullptr si absente ; erreur si ce n'est pas une table.
    [[nodiscard]] const toml::Value* table(const std::vector<std::string>& path) const {
        const toml::Value* v = root_.at_path(path);
        if (!v) return nullptr;
        if (!v->is_table()) util::fail("{}: [{}] doit être une table", file_, toml::join_path(path));
        return v;
    }

    void str(const toml::Value* t, const std::string& key, std::string& out) const {
        if (!t) return;
        if (auto* v = t->find(key)) {
            if (!v->is_string()) bad(t, key, "une chaîne");
            out = v->as_string();
        }
    }
    void list(const toml::Value* t, const std::string& key, std::vector<std::string>& out) const {
        if (!t) return;
        if (auto* v = t->find(key)) {
            bool ok = false;
            auto r = v->as_strings(&ok);
            if (!ok) bad(t, key, "un tableau de chaînes");
            out = std::move(r);
        }
    }
    bool has(const toml::Value* t, const std::string& key) const { return t && t->find(key); }
    void integer(const toml::Value* t, const std::string& key, int& out) const {
        if (!t) return;
        if (auto* v = t->find(key)) {
            if (!v->is_integer()) bad(t, key, "un entier");
            out = static_cast<int>(v->as_integer());
        }
    }
    void boolean(const toml::Value* t, const std::string& key, bool& out) const {
        if (!t) return;
        if (auto* v = t->find(key)) {
            if (!v->is_bool()) bad(t, key, "un booléen");
            out = v->as_bool();
        }
    }
    void map(const toml::Value* t, const std::string& key, std::vector<std::pair<std::string, std::string>>& out) const {
        if (!t) return;
        if (auto* v = t->find(key)) {
            if (!v->is_table()) bad(t, key, "une table");
            out.clear();
            for (size_t i = 0; i < v->size(); ++i) {
                if (!v->value_at(i).is_string()) bad(v, v->keys()[i], "une chaîne");
                out.emplace_back(v->keys()[i], v->value_at(i).as_string());
            }
        }
    }

private:
    const toml::Value& root_;
    std::string file_;

    [[noreturn]] void bad(const toml::Value*, const std::string& key, std::string_view expected) const {
        util::fail("{}: '{}' doit être {}", file_, key, expected);
    }
};

void read_flags(const Reader& r, const toml::Value* t, Flags& f) {
    r.list(t, "defines", f.defines);
    r.list(t, "public_defines", f.public_defines);
    r.list(t, "cflags", f.cflags);
    r.list(t, "cxxflags", f.cxxflags);
    r.list(t, "ldflags", f.ldflags);
    r.list(t, "warnings", f.warnings);
}

void read_platforms(const Reader& r, const toml::Value* t, std::map<std::string, PlatformOverride>& out) {
    if (!t) return;
    for (size_t i = 0; i < t->size(); ++i) {
        auto& v = t->value_at(i);
        if (!v.is_table()) util::fail("{}: platform.{} doit être une table", r.file(), t->keys()[i]);
        PlatformOverride o;
        r.list(&v, "sources", o.sources);
        r.list(&v, "defines", o.defines);
        r.list(&v, "system", o.system_libs);
        r.list(&v, "cflags", o.cflags);
        r.list(&v, "cxxflags", o.cxxflags);
        r.list(&v, "ldflags", o.ldflags);
        out[t->keys()[i]] = std::move(o);
    }
}

void read_deps(const Reader& r, const toml::Value* t, Dependencies& d) {
    r.list(t, "modules", d.modules);
    r.list(t, "pkg_config", d.pkg_config);
    r.list(t, "system", d.system);
    r.list(t, "frameworks", d.frameworks);
    for (auto& s : d.system)
        if (s.starts_with("-l")) s = s.substr(2);
    if (!t) return;
    if (auto* g = t->find("git")) {
        if (!g->is_table()) util::fail("{}: dependencies.git doit être une table", r.file());
        for (size_t i = 0; i < g->size(); ++i) {
            auto& v = g->value_at(i);
            if (!v.is_table()) util::fail("{}: dependencies.git.{} doit être une table", r.file(), g->keys()[i]);
            GitDependency gd;
            gd.alias = g->keys()[i];
            r.str(&v, "url", gd.url);
            r.str(&v, "version", gd.version);
            r.str(&v, "module", gd.module);
            r.str(&v, "subdir", gd.subdir);
            if (gd.url.empty()) util::fail("{}: dependencies.git.{} : 'url' est obligatoire", r.file(), gd.alias);
            d.git.push_back(std::move(gd));
        }
    }
}

void read_review(const Reader& r, ReviewConfig& out) {
    const toml::Value* t = r.table({"review"});
    if (!t) return;
    r.str(t, "status", out.status);
    r.str(t, "reviewer", out.reviewer);
    r.str(t, "date", out.date);
    r.str(t, "notes", out.notes);
}

void read_rules(const Reader& r, std::vector<PreCommitRule>& out) {
    const toml::Value* t = r.table({"git", "pre_commit"});
    if (!t) return;
    for (size_t i = 0; i < t->size(); ++i) {
        auto& v = t->value_at(i);
        if (!v.is_table()) util::fail("{}: git.pre_commit.{} doit être une table", r.file(), t->keys()[i]);
        PreCommitRule rule;
        rule.name = t->keys()[i];
        rule.file = r.file();
        r.str(&v, "description", rule.description);
        r.str(&v, "run", rule.run);
        r.str(&v, "command", rule.command);
        r.list(&v, "when", rule.when);
        r.map(&v, "env", rule.env);
        r.str(&v, "on_failure", rule.on_failure);
        r.boolean(&v, "enabled", rule.enabled);
        out.push_back(std::move(rule));
    }
}

std::vector<ModeConfig> parse_modes(const Reader& r) {
    std::vector<ModeConfig> out;
    const toml::Value* mt = r.table({"build", "modes"});
    auto builtins = builtin_modes();
    if (mt) {
        // Un mode est traité après le mode dont il hérite.
        std::vector<std::string> pending(mt->keys().begin(), mt->keys().end()), order;
        while (!pending.empty()) {
            bool progress = false;
            for (auto it = pending.begin(); it != pending.end();) {
                const toml::Value* t = mt->find(*it);
                std::string base;
                if (t && t->is_table()) r.str(t, "base", base);
                bool waits = !base.empty() && base != *it &&
                             std::find(pending.begin(), pending.end(), base) != pending.end();
                if (waits) { ++it; continue; }
                order.push_back(*it);
                it = pending.erase(it);
                progress = true;
            }
            if (!progress) util::fail("{}: build.modes : héritage circulaire entre {}", r.file(), util::join(pending, ", "));
        }
        for (auto& name : order) {
            const toml::Value* t = mt->find(name);
            if (!t || !t->is_table()) util::fail("{}: build.modes.{} doit être une table", r.file(), name);
            ModeConfig m;
            m.name = name;
            r.str(t, "base", m.base);
            if (!m.base.empty()) {
                const ModeConfig* b = nullptr;
                for (auto& x : out) if (x.name == m.base) b = &x;
                if (!b) for (auto& x : builtins) if (x.name == m.base) b = &x;
                if (!b) util::fail("{}: build.modes.{} : base inconnue '{}'", r.file(), name, m.base);
                m.flags = b->flags;
                m.link_flags = b->link_flags;
                m.defines = b->defines;
                m.sanitizer = b->sanitizer;
                m.runner = b->runner;
                m.runner_args = b->runner_args;
            } else if (auto it = std::find_if(builtins.begin(), builtins.end(), [&](auto& b) { return b.name == name; });
                       it != builtins.end()) {
                // Redéfinition partielle d'un mode prédéfini : on part de lui.
                m.flags = it->flags;
                m.link_flags = it->link_flags;
                m.defines = it->defines;
                m.sanitizer = it->sanitizer;
                m.runner = it->runner;
                m.runner_args = it->runner_args;
            }
            std::string old_san = m.sanitizer;
            r.str(t, "sanitizer", m.sanitizer);
            if (m.sanitizer != old_san) {
                std::erase_if(m.flags, [](auto& f) { return f.starts_with("-fsanitize="); });
                std::erase_if(m.link_flags, [](auto& f) { return f.starts_with("-fsanitize="); });
            }
            r.list(t, "flags", m.flags);
            r.list(t, "link_flags", m.link_flags);
            r.list(t, "defines", m.defines);
            r.str(t, "runner", m.runner);
            r.list(t, "runner_args", m.runner_args);
            normalize_mode(m);
            out.push_back(std::move(m));
        }
    }
    for (auto& b : builtins)
        if (std::none_of(out.begin(), out.end(), [&](auto& m) { return m.name == b.name; })) out.push_back(b);
    return out;
}

bool is_within(const fs::path& p, const fs::path& dir) {
    auto rel = p.lexically_relative(dir);
    return !rel.empty() && !rel.native().starts_with("..");
}

fs::path canonical_or(const fs::path& p) {
    std::error_code ec;
    auto c = fs::weakly_canonical(p, ec);
    return ec ? p.lexically_normal() : c;
}

} // namespace

// ============================================================================
// Chargement
// ============================================================================

std::optional<fs::path> Workspace::find_root(const fs::path& from) {
    std::error_code ec;
    fs::path p = canonical_or(fs::absolute(from, ec));
    for (;;) {
        if (fs::is_regular_file(p / kProjectFile, ec)) return p;
        if (!p.has_parent_path() || p.parent_path() == p) return std::nullopt;
        p = p.parent_path();
    }
}

Workspace Workspace::load(const fs::path& from, const LoadOptions& o) {
    auto root = find_root(from);
    if (!root)
        util::fail("aucun {} trouvé dans {} ni ses parents (cpp-maker new <nom> pour créer un projet)",
                   kProjectFile, canonical_or(fs::absolute(from)).string());
    return load_root(*root, o);
}

Workspace Workspace::load_root(const fs::path& root, const LoadOptions& o) {
    Workspace w;
    w.root = canonical_or(fs::absolute(root));
    w.project.file = w.root / kProjectFile;
    w.load_project_file();
    w.discover();
    if (o.resolve_git) {
        w.import_git(o.update_git);
        w.git_imported_ = true;
    }
    w.resolve_dependencies();
    for (auto& x : w.warnings) x = util::replace_all(x, w.root.string() + "/", "");
    if (o.validate) {
        auto errors = w.validate();
        if (!errors.empty()) {
            std::string msg = errors.size() == 1 ? errors[0]
                              : std::format("{} erreur(s) de configuration :\n  - {}", errors.size(),
                                            util::join(errors, "\n  - "));
            throw util::Error(msg);
        }
    }
    return w;
}

void Workspace::load_project_file() {
    auto& p = project;
    toml::Value root;
    root = toml::parse_file(p.file);
    Reader r(root, p.file.string());
    util::push_unique(warnings, schema::lint(schema::FileKind::Project, root, p.file.string()));

    p.name = this->root.filename().string();
    if (auto* t = r.table({"project"})) {
        r.str(t, "name", p.name);
        r.str(t, "version", p.version);
        r.str(t, "description", p.description);
        r.str(t, "language", p.language);
        r.str(t, "cxx_standard", p.cxx_standard);
        r.str(t, "c_standard", p.c_standard);
        r.str(t, "minimum_cpp_maker_version", p.min_version);
    }
    if (auto* t = r.table({"paths"})) {
        // Chaque chemin s'écrit "dossier" ou ["dossier", …].
        r.list(t, "layers", p.layers_dirs);
        if (p.layers_dirs.empty()) p.layers_dirs = {"libs"};
        r.list(t, "applications", p.apps_dirs);
        if (p.apps_dirs.empty()) p.apps_dirs = {"apps"};
        // build et dist désignent UN dossier de sortie : tableau d'un seul élément accepté.
        for (auto [key, out] : {std::pair<const char*, std::string*>{"build", &p.build_dir}, {"dist", &p.dist_dir}, {"out", &p.out_dir}}) {
            std::vector<std::string> v;
            r.list(t, key, v);
            if (v.size() > 1)
                util::fail("{}: [paths].{} : un seul dossier attendu (reçu {} : {})", r.file(), key, v.size(), util::join(v, ", "));
            if (v.size() == 1) *out = v[0];
        }
    }
    r.list(r.table({"layers"}), "order", p.layer_order);
    read_deps(r, r.table({"dependencies"}), p.deps);
    if (auto* t = r.table({"build"})) {
        r.str(t, "default_mode", p.default_mode);
        r.str(t, "default_toolchain", p.default_toolchain);
        r.integer(t, "jobs", p.jobs);
        r.list(t, "warnings", p.warnings);
        r.boolean(t, "static_pic", p.static_pic);
        r.boolean(t, "require_approval", p.require_approval);
        r.boolean(t, "ccache", p.ccache);
        r.list(t, "defines", p.defines);
        r.list(t, "cflags", p.cflags);
        r.list(t, "cxxflags", p.cxxflags);
        r.list(t, "ldflags", p.ldflags);
    }
    p.modes = parse_modes(r);
    read_rules(r, p.pre_commit);

    if (auto* t = r.table({"toolchains"})) {
        for (size_t i = 0; i < t->size(); ++i) {
            const std::string& n = t->keys()[i];
            auto& v = t->value_at(i);
            if (!v.is_table()) util::fail("{}: toolchains.{} doit être une table", r.file(), n);
            ToolchainConfig c;
            c.name = n;
            c.from_config = true;
            r.str(&v, "prefix", c.prefix);
            r.str(&v, "cc", c.cc);
            r.str(&v, "cxx", c.cxx);
            r.str(&v, "ar", c.ar);
            r.str(&v, "strip", c.strip);
            r.str(&v, "pkg_config", c.pkg_config);
            r.str(&v, "family", c.family);
            r.str(&v, "os", c.os);
            r.str(&v, "target", c.target);
            r.str(&v, "sysroot", c.sysroot);
            r.list(&v, "cflags", c.cflags);
            r.list(&v, "cxxflags", c.cxxflags);
            r.list(&v, "ldflags", c.ldflags);
            if (r.has(&v, "runner")) {
                r.list(&v, "runner", c.runner);
                c.runner_set = true;
            }
            p.toolchains[n] = std::move(c);
        }
    }
    if (auto* t = r.table({"tests"})) {
        r.integer(t, "timeout", p.test_timeout);
        r.str(t, "framework", p.test_framework);
        r.map(t, "env", p.test_env);
    }
    if (auto* t = r.table({"package"})) {
        auto& k = p.package;
        r.list(t, "formats", k.formats);
        r.boolean(t, "strip", k.strip);
        r.str(t, "maintainer", k.maintainer);
        r.str(t, "description", k.description);
        r.str(t, "homepage", k.homepage);
        r.str(t, "license", k.license);
        r.list(t, "deb_depends", k.deb_depends);
        r.str(t, "deb_prefix", k.deb_prefix);
        r.list(t, "extra_files", k.extra_files);
        r.boolean(t, "checksums", k.checksums);
    }
    if (p.package.description.empty()) p.package.description = p.description;
    if (auto* t = r.table({"deploy"})) {
        for (size_t i = 0; i < t->size(); ++i) {
            auto& v = t->value_at(i);
            const std::string& n = t->keys()[i];
            if (!v.is_table()) util::fail("{}: deploy.{} doit être une table", r.file(), n);
            DeployConfig d;
            d.name = n;
            r.str(&v, "method", d.method);
            r.str(&v, "host", d.host);
            r.str(&v, "user", d.user);
            r.str(&v, "path", d.path);
            r.integer(&v, "port", d.port);
            r.str(&v, "toolchain", d.toolchain);
            r.str(&v, "mode", d.mode);
            r.str(&v, "artifact", d.artifact);
            r.list(&v, "targets", d.targets);
            r.list(&v, "ssh_options", d.ssh_options);
            r.list(&v, "before", d.before);
            r.list(&v, "after", d.after);
            r.list(&v, "after_local", d.after_local);
            r.str(&v, "script", d.script);
            r.boolean(&v, "delete", d.delete_extra);
            p.deploys[n] = std::move(d);
        }
    }
    if (auto* t = r.table({"generate"})) {
        for (size_t i = 0; i < t->size(); ++i) {
            auto& v = t->value_at(i);
            if (!v.is_table()) util::fail("{}: generate.{} doit être une table", r.file(), t->keys()[i]);
            GenerateStep g;
            g.name = t->keys()[i];
            r.str(&v, "command", g.command);
            r.list(&v, "inputs", g.inputs);
            r.list(&v, "outputs", g.outputs);
            r.list(&v, "run_before", g.run_before);
            r.boolean(&v, "always", g.always);
            p.generate.push_back(std::move(g));
        }
    }
    if (!p.min_version.empty() && util::compare_versions(cppmaker::kVersion, p.min_version) < 0)
        util::fail("{}: ce projet requiert cpp-maker {} (version installée : {})", r.file(), p.min_version,
                   cppmaker::kVersion);
}

namespace {

LayerConfig read_layer(const fs::path& root, const fs::path& dir) {
    LayerConfig L;
    L.name = dir.filename().string();
    L.dir = dir.lexically_relative(root);
    L.file = dir / kLayerFile;
    std::error_code ec;
    L.has_file = fs::is_regular_file(L.file, ec);
    if (!L.has_file) return L;
    auto tbl = toml::parse_file(L.file);
    Reader r(tbl, L.file.string());
    std::string declared;
    if (auto* t = r.table({"layer"})) {
        r.str(t, "name", declared);
        r.str(t, "version", L.version);
        r.str(t, "description", L.description);
        r.list(t, "modules", L.modules);
    }
    if (!declared.empty() && declared != L.name)
        util::fail("{}: name = \"{}\" ne correspond pas au dossier '{}' (cpp-maker rename layer pour renommer)",
                   L.file.string(), declared, L.name);
    read_deps(r, r.table({"dependencies"}), L.deps);
    read_flags(r, r.table({"flags"}), L.flags);
    r.list(r.table({"forbid"}), "pkg_config", L.forbid_pkg_config);
    read_platforms(r, r.table({"platform"}), L.platforms);
    read_rules(r, L.pre_commit);
    return L;
}

TargetConfig read_target(const fs::path& root, const fs::path& dir, Kind kind, const std::string& layer,
                         const ProjectConfig& p, std::vector<std::string>& warnings) {
    TargetConfig t;
    t.kind = kind;
    t.name = dir.filename().string();
    t.layer = layer;
    t.dir = dir.lexically_relative(root);
    t.file = dir / (kind == Kind::Module ? kModuleFile : kApplicationFile);
    t.id = kind == Kind::Module ? layer + "." + t.name : t.name;
    t.type = kind == Kind::Module ? "static" : "executable";
    t.language = p.language;
    if (kind == Kind::Application) t.public_includes.clear();

    auto tbl = toml::parse_file(t.file);
    Reader r(tbl, t.file.string());
    util::push_unique(warnings, schema::lint(kind == Kind::Module ? schema::FileKind::Module
                                                                  : schema::FileKind::Application,
                                             tbl, t.file.string()));
    const char* sec = kind == Kind::Module ? "module" : "application";
    std::string declared;
    if (auto* s = r.table({sec})) {
        r.str(s, "name", declared);
        r.str(s, "version", t.version);
        r.str(s, "description", t.description);
        if (kind == Kind::Module) r.str(s, "type", t.type);
        r.str(s, "language", t.language);
        r.str(s, "cxx_standard", t.cxx_standard);
        r.str(s, "c_standard", t.c_standard);
        r.str(s, "artifact", t.artifact);
        r.boolean(s, "install", t.install);
        if (kind == Kind::Application) r.str(s, "main", t.main);
    }
    if (!declared.empty() && declared != t.name)
        util::fail("{}: name = \"{}\" ne correspond pas au dossier '{}' (cpp-maker rename {} pour renommer)",
                   t.file.string(), declared, t.name, kind == Kind::Module ? "module" : "app");
    if (auto* s = r.table({"sources"})) {
        r.list(s, "dirs", t.source_dirs);
        r.list(s, "files", t.source_files);
        r.list(s, "exclude", t.exclude);
    }
    if (auto* s = r.table({"include"})) {
        if (kind == Kind::Module) r.list(s, "public", t.public_includes);
        if (r.has(s, "private")) {
            r.list(s, "private", t.private_includes);
            t.private_includes_set = true;
        }
    }
    if (!t.private_includes_set) {
        t.private_includes = t.source_dirs;
        if (kind == Kind::Application) t.private_includes.insert(t.private_includes.begin(), "include");
    }
    read_deps(r, r.table({"dependencies"}), t.deps);
    read_flags(r, r.table({"flags"}), t.flags);
    if (auto* s = r.table({"tests"})) {
        r.list(s, "dirs", t.test_dirs);
        r.str(s, "framework", t.test_framework);
        r.integer(s, "timeout", t.test_timeout);
    }
    read_platforms(r, r.table({"platform"}), t.platforms);
    read_rules(r, t.pre_commit);
    read_review(r, t.review);
    if (auto* s = r.table({"run"})) {
        r.list(s, "args", t.run_args);
        r.str(s, "cwd", t.run_cwd);
        r.map(s, "env", t.run_env);
    }
    return t;
}

std::vector<fs::path> sorted_subdirs(const fs::path& dir) {
    std::vector<fs::path> r;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return r;
    for (auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_directory(ec)) continue;
        auto n = e.path().filename().string();
        if (n.starts_with(".")) continue;
        r.push_back(e.path());
    }
    std::sort(r.begin(), r.end());
    return r;
}

} // namespace

void Workspace::discover() {
    std::error_code ec;
    // --- Layers
    std::vector<LayerConfig> found;
    for (auto& d : [&] {
             std::vector<fs::path> all;
             for (auto& base : layers_paths())
                 for (auto& sub : sorted_subdirs(base)) all.push_back(sub);
             return all;
         }()) {
        bool is_layer = fs::is_regular_file(d / kLayerFile, ec);
        if (!is_layer)
            for (auto& sub : sorted_subdirs(d))
                if (fs::is_regular_file(sub / kModuleFile, ec)) { is_layer = true; break; }
        if (!is_layer) continue;
        found.push_back(read_layer(root, d));
        if (found.back().has_file) {
            auto tbl = toml::parse_file(found.back().file);
            util::push_unique(warnings, schema::lint(schema::FileKind::Layer, tbl, found.back().file.string()));
        }
    }
    for (auto& name : project.layer_order) {
        auto it = std::find_if(found.begin(), found.end(), [&](auto& L) { return L.name == name; });
        if (it == found.end()) continue;   // signalé par validate()
        layers.push_back(std::move(*it));
        found.erase(it);
    }
    for (auto& L : found) layers.push_back(std::move(L));

    // --- Modules
    for (auto& L : layers) {
        fs::path ldir = root / L.dir;
        std::vector<fs::path> dirs;
        if (!L.modules.empty()) {
            for (auto& m : L.modules) {
                fs::path d = ldir / m;
                if (!fs::is_regular_file(d / kModuleFile, ec))
                    util::fail("{}: module '{}' listé mais {} introuvable", L.file.string(), m,
                               (d / kModuleFile).string());
                dirs.push_back(d);
            }
        } else {
            for (auto& d : sorted_subdirs(ldir))
                if (fs::is_regular_file(d / kModuleFile, ec)) dirs.push_back(d);
        }
        for (auto& d : dirs) {
            targets.push_back(read_target(root, d, Kind::Module, L.name, project, warnings));
            L.discovered.push_back(targets.back().name);
        }
    }
    // --- Applications
    for (auto& base : apps_paths())
        for (auto& d : sorted_subdirs(base))
            if (fs::is_regular_file(d / kApplicationFile, ec))
                targets.push_back(read_target(root, d, Kind::Application, "", project, warnings));
}

void Workspace::import_git(bool update) {
    bool any = std::any_of(targets.begin(), targets.end(), [](auto& t) { return !t.deps.git.empty(); });
    if (!any) return;
    std::map<std::string, std::string> lock = update ? std::map<std::string, std::string>{}
                                                     : gitdeps::read_lock(root / kLockFile);
    std::map<std::string, std::string> url_alias;
    // Les cibles ajoutées pendant la boucle ne déclarent pas de git (déjà résolu).
    size_t n = targets.size();
    for (size_t ti = 0; ti < n; ++ti) {
        for (auto& gd : targets[ti].deps.git) {
            if (auto it = url_alias.find(gd.url); it != url_alias.end()) {
                if (it->second != gd.alias)
                    util::fail("dépendance git '{}' déclarée sous deux alias ('{}' et '{}')", gd.url, it->second, gd.alias);
            } else {
                url_alias[gd.url] = gd.alias;
                auto res = gitdeps::resolve(gd.url, gd.version, lock.count(gd.url) ? lock[gd.url] : "");
                if (!res.ok)
                    util::fail("{}: dépendance git '{}' ({}) : {}", targets[ti].file.string(), gd.alias, gd.url, res.error);
                bool changed = !lock.count(gd.url) || lock[gd.url] != res.commit;
                lock[gd.url] = res.commit;
                fs::path dep_root = res.path / gd.subdir;
                Workspace dep = load_root(dep_root, {.resolve_git = true, .update_git = update, .validate = true});
                if (find_layer(gd.alias) || std::any_of(targets.begin(), targets.end(), [&](auto& t) { return t.layer == gd.alias; }))
                    util::fail("dépendance git '{}' : l'alias est déjà un nom de layer", gd.alias);
                // Identifiants importés : <alias>.<module> (ou <alias>.<layer>-<module> en cas d'homonymie).
                std::map<std::string, std::string> ids;
                std::map<std::string, int> count;
                for (auto& m : dep.targets) if (m.is_module()) ++count[m.name];
                for (auto& m : dep.targets)
                    if (m.is_module()) ids[m.id] = gd.alias + "." + (count[m.name] > 1 ? m.layer + "-" + m.name : m.name);
                LayerConfig L;
                L.name = gd.alias;
                L.dir = dep_root;
                for (auto& m : dep.targets) {
                    if (!m.is_module()) continue;
                    TargetConfig c = m;
                    c.id = ids[m.id];
                    c.name = c.id.substr(gd.alias.size() + 1);
                    if (c.artifact.empty()) c.artifact = gd.alias + "_" + m.name;
                    c.layer = gd.alias;
                    c.dir = dep_root / m.dir;   // absolu
                    c.from_git = true;
                    c.install = false;
                    c.deps.git.clear();
                    for (auto& d : c.dep_ids)
                        if (auto j = ids.find(d); j != ids.end()) d = j->second;
                    // Les cxx/c standards du projet distant s'appliquent à ses modules.
                    if (c.cxx_standard.empty()) c.cxx_standard = dep.project.cxx_standard;
                    if (c.c_standard.empty()) c.c_standard = dep.project.c_standard;
                    L.discovered.push_back(c.name);
                    targets.push_back(std::move(c));
                }
                layers.push_back(std::move(L));
                if (changed) term::info("dépendance git {} @ {} ({})", gd.url, gd.version, res.commit.substr(0, 10));
            }
            auto& owner = targets[ti];
            std::string ref = gd.module.empty() ? gd.alias + ".*" : gd.alias + "." + gd.module;
            util::push_unique(owner.deps.modules, ref);
        }
    }
    git_lock = lock;
    gitdeps::write_lock(root / kLockFile, lock);
}

void Workspace::resolve_dependencies() {
    std::vector<std::string> errors;
    // Chargement sans import git : les références « <alias>.… » sont ignorées.
    std::set<std::string> git_aliases;
    if (!git_imported_)
        for (auto& t : targets)
            for (auto& g : t.deps.git) git_aliases.insert(g.alias);
    for (auto& t : targets) {
        if (t.from_git) continue;
        t.dep_ids.clear();
        auto add_refs = [&](const std::vector<std::string>& refs, const std::string& origin) {
            for (auto& ref : refs) {
                if (git_aliases.count(ref.substr(0, ref.find('.')))) continue;
                try {
                    for (auto& id : resolve_module_ref(ref, t.layer))
                        if (id != t.id) util::push_unique(t.dep_ids, id);
                } catch (const util::Error& e) {
                    errors.push_back(std::format("{}: {}", origin, e.what()));
                }
            }
        };
        add_refs(t.deps.modules, t.file.string());
        t.all_pkg_config = t.deps.pkg_config;
        t.all_system_libs = t.deps.system;
        if (t.is_module()) {
            if (auto* L = find_layer(t.layer)) {
                add_refs(L->deps.modules, L->file.string());
                util::push_unique(t.all_pkg_config, L->deps.pkg_config);
                util::push_unique(t.all_system_libs, L->deps.system);
            }
        }
        add_refs(project.deps.modules, project.file.string());
        util::push_unique(t.all_pkg_config, project.deps.pkg_config);
        util::push_unique(t.all_system_libs, project.deps.system);
    }
    // Conservées pour validate() (le chargement peut être demandé sans validation).
    unresolved_ = std::move(errors);
}

// ============================================================================
// Recherche et résolution
// ============================================================================

const TargetConfig* Workspace::find(const std::string& id) const {
    for (auto& t : targets)
        if (t.id == id) return &t;
    return nullptr;
}

TargetConfig* Workspace::find(const std::string& id) {
    for (auto& t : targets)
        if (t.id == id) return &t;
    return nullptr;
}

const LayerConfig* Workspace::find_layer(const std::string& name) const {
    for (auto& L : layers)
        if (L.name == name) return &L;
    return nullptr;
}

std::vector<const TargetConfig*> Workspace::modules_of(const std::string& layer) const {
    std::vector<const TargetConfig*> r;
    for (auto& t : targets)
        if (t.is_module() && t.layer == layer) r.push_back(&t);
    return r;
}

std::vector<const TargetConfig*> Workspace::applications() const {
    std::vector<const TargetConfig*> r;
    for (auto& t : targets)
        if (t.is_app()) r.push_back(&t);
    return r;
}

std::vector<const TargetConfig*> Workspace::modules() const {
    std::vector<const TargetConfig*> r;
    for (auto& t : targets)
        if (t.is_module()) r.push_back(&t);
    return r;
}

std::vector<std::string> Workspace::ids() const {
    std::vector<std::string> r;
    for (auto& t : targets) r.push_back(t.id);
    return r;
}

std::vector<std::string> Workspace::resolve_module_ref(const std::string& ref, const std::string& context_layer) const {
    if (ref.ends_with(".*")) {
        std::string layer = ref.substr(0, ref.size() - 2);
        if (!find_layer(layer)) {
            std::vector<std::string> names;
            for (auto& L : layers) names.push_back(L.name);
            auto s = util::suggest(layer, names);
            util::fail("layer inconnu '{}' dans '{}'{}", layer, ref, s.empty() ? "" : " (vouliez-vous dire '" + s + "' ?)");
        }
        std::vector<std::string> r;
        for (auto* m : modules_of(layer)) r.push_back(m->id);
        return r;
    }
    if (auto* t = find(ref); t && t->is_module()) return {ref};
    if (auto* t = find(ref); t && t->is_app())
        util::fail("'{}' est une application : seules les bibliothèques (modules) peuvent être des dépendances", ref);

    std::vector<const TargetConfig*> same;
    for (auto& t : targets)
        if (t.is_module() && t.name == ref) same.push_back(&t);
    if (same.size() > 1)
        for (auto* t : same)
            if (t->layer == context_layer) return {t->id};
    if (same.size() == 1) return {same[0]->id};
    if (same.size() > 1) {
        std::vector<std::string> ids;
        for (auto* t : same) ids.push_back(t->id);
        util::fail("référence ambiguë '{}' : {}", ref, util::join(ids, ", "));
    }
    if (find_layer(ref)) {
        std::vector<std::string> r;
        for (auto* m : modules_of(ref)) r.push_back(m->id);
        return r;
    }
    std::vector<std::string> names;
    for (auto& t : targets)
        if (t.is_module()) { names.push_back(t.id); names.push_back(t.name); }
    auto s = util::suggest(ref, names);
    util::fail("module inconnu '{}'{}", ref, s.empty() ? "" : " (vouliez-vous dire '" + s + "' ?)");
}

std::vector<std::string> Workspace::resolve_selector(const std::string& sel) const {
    if (sel == "all" || sel == "*" || sel == "tout") return ids();
    if (auto* t = find(sel); t && t->is_app()) return {sel};
    try {
        return resolve_module_ref(sel);
    } catch (const util::Error&) {
        std::vector<std::string> names = ids();
        for (auto& L : layers) names.push_back(L.name);
        for (auto& t : targets) names.push_back(t.name);
        auto s = util::suggest(sel, names);
        util::fail("cible inconnue '{}'{} (cpp-maker list)", sel, s.empty() ? "" : " — vouliez-vous dire '" + s + "' ?");
    }
}

std::vector<std::string> Workspace::resolve_module_ref_noexcept(const std::string& ref, const std::string& ctx) const {
    try {
        return resolve_module_ref(ref, ctx);
    } catch (const util::Error&) {
        return {};
    }
}

std::vector<std::string> Workspace::resolve_selector_noexcept(const std::string& sel) const {
    try {
        return resolve_selector(sel);
    } catch (const util::Error&) {
        return {};
    }
}

std::vector<std::string> Workspace::targets_at(const fs::path& cwd) const {
    fs::path c = canonical_or(fs::absolute(cwd));
    for (auto& t : targets)
        if (!t.from_git && (c == canonical_or(root / t.dir) || is_within(c, canonical_or(root / t.dir)))) return {t.id};
    for (auto& L : layers) {
        fs::path d = canonical_or(root / L.dir);
        if (c == d || is_within(c, d)) {
            std::vector<std::string> r;
            for (auto* m : modules_of(L.name)) r.push_back(m->id);
            return r;
        }
    }
    return {};
}

std::vector<std::string> Workspace::select(const std::vector<std::string>& selectors, const fs::path& cwd) const {
    if (selectors.empty()) {
        auto here = targets_at(cwd);
        if (!here.empty()) return here;
        std::vector<std::string> r;
        for (auto& t : targets)
            if (!t.from_git) r.push_back(t.id);
        return r;
    }
    std::vector<std::string> r;
    for (auto& s : selectors) util::push_unique(r, resolve_selector(s));
    return r;
}

std::vector<std::string> Workspace::topo_order(const std::vector<std::string>& roots) const {
    std::vector<std::string> order, stack;
    std::map<std::string, int> state;
    std::function<void(const std::string&)> visit = [&](const std::string& n) {
        const TargetConfig* t = find(n);
        if (!t) util::fail("cible inconnue : '{}'", n);
        if (state[n] == 2) return;
        if (state[n] == 1) {
            auto it = std::find(stack.begin(), stack.end(), n);
            std::vector<std::string> cyc(it, stack.end());
            cyc.push_back(n);
            util::fail("dépendance circulaire : {}", util::join(cyc, " → "));
        }
        state[n] = 1;
        stack.push_back(n);
        for (auto& d : t->dep_ids) visit(d);
        stack.pop_back();
        state[n] = 2;
        order.push_back(n);
    };
    for (auto& r : roots) visit(r);
    return order;
}

std::vector<std::string> Workspace::closure(const std::string& id) const {
    auto o = topo_order({id});
    o.pop_back();
    return o;
}

std::vector<std::string> Workspace::dependents(const std::string& id) const {
    std::vector<std::string> r;
    for (auto& t : targets)
        if (util::contains(t.dep_ids, id)) r.push_back(t.id);
    return r;
}

int Workspace::layer_rank(const std::string& layer) const {
    for (size_t i = 0; i < project.layer_order.size(); ++i)
        if (project.layer_order[i] == layer) return static_cast<int>(i);
    return -1;
}

// ============================================================================
// Validation
// ============================================================================

std::vector<std::string> Workspace::validate() const {
    std::vector<std::string> e = unresolved_;
    const std::string pf = project.file.string();
    auto& p = project;
    auto E = [&](std::string m) { e.push_back(std::move(m)); };

    if (!util::is_safe_name(p.name)) E(std::format("{}: project.name invalide : '{}'", pf, p.name));
    if (!util::contains(cxx_standards(), p.cxx_standard))
        E(std::format("{}: project.cxx_standard invalide : '{}'", pf, p.cxx_standard));
    if (!util::contains(c_standards(), p.c_standard))
        E(std::format("{}: project.c_standard invalide : '{}'", pf, p.c_standard));
    if (p.language != "c" && p.language != "c++") E(std::format("{}: project.language doit valoir c ou c++", pf));
    std::vector<std::string> dirs = {p.build_dir, p.dist_dir, p.out_dir};
    dirs.insert(dirs.end(), p.layers_dirs.begin(), p.layers_dirs.end());
    dirs.insert(dirs.end(), p.apps_dirs.begin(), p.apps_dirs.end());
    for (auto& d : dirs)
        if (d.empty() || fs::path(d).is_absolute() || d.starts_with(".."))
            E(std::format("{}: [paths] chemin invalide '{}' (relatif à la racine attendu)", pf, d));
    if (p.jobs < 0) E(std::format("{}: build.jobs doit être ≥ 0", pf));
    if (!p.find_mode(p.default_mode)) E(std::format("{}: build.default_mode : mode inconnu '{}'", pf, p.default_mode));
    if (p.test_timeout < 0) E(std::format("{}: tests.timeout doit être ≥ 0", pf));
    if (p.test_framework != "builtin" && p.test_framework != "gtest" && p.test_framework != "none")
        E(std::format("{}: tests.framework invalide '{}' (builtin|gtest|none)", pf, p.test_framework));

    for (auto& m : p.modes) {
        if (!util::is_safe_name(m.name)) E(std::format("{}: nom de mode invalide '{}'", pf, m.name));
        for (auto& s : util::split_list(m.sanitizer))
            if (!sanitize::find(sanitize::normalize(s)))
                E(std::format("{}: mode '{}' : sanitizer inconnu '{}'", pf, m.name, s));
        if (auto err = sanitize::check_compatible(sanitize::from_mode(m)); !err.empty())
            E(std::format("{}: mode '{}' : {}", pf, m.name, err));
        if (!m.runner.empty() && m.runner != "valgrind")
            E(std::format("{}: mode '{}' : runner '{}' non supporté (valgrind)", pf, m.name, m.runner));
    }
    static const std::set<std::string> oses = {"linux", "windows", "macos", "android", "freebsd", "baremetal"};
    for (auto& [n, t] : p.toolchains) {
        if (!util::is_safe_name(n)) E(std::format("{}: nom de toolchain invalide '{}'", pf, n));
        if (t.family != "auto" && t.family != "gcc" && t.family != "clang")
            E(std::format("{}: toolchains.{}.family invalide '{}' (auto|gcc|clang)", pf, n, t.family));
        if (!t.os.empty() && !oses.count(t.os)) E(std::format("{}: toolchains.{}.os invalide '{}'", pf, n, t.os));
    }

    // --- Layers
    std::set<std::string> seen;
    for (auto& l : p.layer_order) {
        if (!seen.insert(l).second) E(std::format("{}: [layers].order : doublon '{}'", pf, l));
        if (!find_layer(l)) E(std::format("{}: [layers].order : layer '{}' introuvable dans {}", pf, l, util::join(p.layers_dirs, ", ")));
    }
    std::map<std::string, std::string> layer_dirs_seen;
    for (auto& L : layers) {
        if (auto [it, ok] = layer_dirs_seen.emplace(L.name, L.dir.string()); !ok)
            E(std::format("{}: layer '{}' présent dans deux dossiers ({} et {})", pf, L.name, it->second, L.dir.string()));
        if (!util::is_safe_name(L.name) || L.name.find('.') != std::string::npos)
            E(std::format("{}: nom de layer invalide '{}' (lettres, chiffres, _ et -)", L.file.string(), L.name));
        bool git = std::any_of(targets.begin(), targets.end(), [&](auto& t) { return t.layer == L.name && t.from_git; });
        if (!p.layer_order.empty() && layer_rank(L.name) < 0 && !git)
            E(std::format("{}: layer '{}' absent de [layers].order", pf, L.name));
    }

    // --- Cibles
    std::set<std::string> ids_seen;
    std::map<std::string, std::string> lib_artifacts, exe_artifacts;
    static const std::set<std::string> plat = {"linux", "windows", "macos", "android", "freebsd", "baremetal", "unix"};
    for (auto& t : targets) {
        std::string f = t.file.string();
        if (!util::is_safe_name(t.name) || t.name.find('.') != std::string::npos)
            E(std::format("{}: nom invalide '{}' (lettres, chiffres, _ et -)", f, t.name));
        if (!ids_seen.insert(t.id).second) E(std::format("{}: identifiant '{}' défini deux fois", f, t.id));
        if (t.is_module() && t.type != "static" && t.type != "shared" && t.type != "header-only")
            E(std::format("{}: module.type invalide '{}' (static|shared|header-only)", f, t.type));
        if (t.language != "c" && t.language != "c++") E(std::format("{}: language doit valoir c ou c++", f));
        if (!t.cxx_standard.empty() && !util::contains(cxx_standards(), t.cxx_standard))
            E(std::format("{}: cxx_standard invalide '{}'", f, t.cxx_standard));
        if (!t.c_standard.empty() && !util::contains(c_standards(), t.c_standard))
            E(std::format("{}: c_standard invalide '{}'", f, t.c_standard));
        if (!t.test_framework.empty() && t.test_framework != "builtin" && t.test_framework != "gtest" &&
            t.test_framework != "none")
            E(std::format("{}: tests.framework invalide '{}'", f, t.test_framework));
        static const std::set<std::string> review_status = {"none", "requested", "approved", "rejected"};
        if (!review_status.count(t.review.status))
            E(std::format("{}: review.status invalide '{}' (none|requested|approved|rejected)", f, t.review.status));
        for (auto& [os, o] : t.platforms) {
            (void)o;
            if (!plat.count(os)) E(std::format("{}: plate-forme inconnue [platform.{}]", f, os));
        }
        if (t.is_header_only() && !t.from_git) {
            static const std::set<std::string> src_ext = {".c", ".cpp", ".cc", ".cxx", ".c++", ".S", ".s"};
            for (auto& d : t.source_dirs) {
                std::error_code ec;
                fs::path dir = util::under(root / t.dir, d);
                if (!fs::is_directory(dir, ec)) continue;
                for (auto& en : fs::recursive_directory_iterator(dir, ec))
                    if (en.is_regular_file(ec) && src_ext.count(en.path().extension().string())) {
                        E(std::format("{}: module header-only mais {} contient des sources ({}) — type static ou shared ?", f,
                                      util::display(dir, root), en.path().filename().string()));
                        break;
                    }
            }
        }
        if (!t.is_header_only()) {
            auto& table = t.is_app() ? exe_artifacts : lib_artifacts;
            if (auto [it, ok] = table.emplace(t.artifact_base(), t.id); !ok)
                E(std::format("{}: l'artefact '{}' est déjà produit par '{}' — définissez artifact = \"...\"",
                              f, t.artifact_base(), it->second));
        }
    }

    // --- Graphe : cycles, couches, paquets interdits
    try {
        (void)topo_order(ids());
    } catch (const util::Error& err) {
        E(std::format("{}: {}", pf, err.what()));
    }
    for (auto& t : targets) {
        if (!t.is_module() || t.from_git) continue;
        int r = layer_rank(t.layer);
        for (auto& d : t.dep_ids) {
            auto* dm = find(d);
            if (!dm || r < 0) continue;
            int dr = layer_rank(dm->layer);
            if (dr > r)
                E(std::format("{}: '{}' (couche {}) ne peut pas dépendre de '{}' (couche {}, supérieure) — "
                              "inversez la dépendance ou déplacez le code", t.file.string(), t.id, t.layer, d, dm->layer));
        }
        auto* L = find_layer(t.layer);
        if (!L || L->forbid_pkg_config.empty()) continue;
        std::vector<const TargetConfig*> stack{&t};
        std::set<std::string> visited{t.id};
        while (!stack.empty()) {
            auto* cur = stack.back();
            stack.pop_back();
            for (auto& pkg : cur->all_pkg_config)
                if (util::contains(L->forbid_pkg_config, pkg))
                    E(std::format("{}: le paquet '{}' est interdit dans le layer {} ('{}' en dépend{})",
                                  t.file.string(), pkg, t.layer, t.id, cur == &t ? "" : " via '" + cur->id + "'"));
            for (auto& d : cur->dep_ids)
                if (auto* dm = find(d); dm && visited.insert(d).second) stack.push_back(dm);
        }
    }

    // --- Livraison
    for (auto& [n, d] : p.deploys) {
        static const std::set<std::string> methods = {"rsync", "scp", "copy", "script"};
        static const std::set<std::string> arts = {"stage", "tar.gz", "tar.xz", "zip", "deb"};
        if (!methods.count(d.method)) E(std::format("{}: deploy.{}.method invalide '{}' (rsync|scp|copy|script)", pf, n, d.method));
        if (d.method == "script" && d.script.empty()) E(std::format("{}: deploy.{}.script requis (méthode script)", pf, n));
        if ((d.method == "rsync" || d.method == "scp") && d.host.empty())
            E(std::format("{}: deploy.{}.host requis pour {}", pf, n, d.method));
        if (d.method != "script" && d.path.empty()) E(std::format("{}: deploy.{}.path requis", pf, n));
        if (!arts.count(d.artifact)) E(std::format("{}: deploy.{}.artifact invalide '{}'", pf, n, d.artifact));
        for (auto& s : d.targets) {
            try {
                (void)resolve_selector(s);
            } catch (const util::Error& err) {
                E(std::format("{}: deploy.{}.targets : {}", pf, n, err.what()));
            }
        }
    }
    for (auto& f : p.package.formats) {
        static const std::set<std::string> fmts = {"tar.gz", "tar.xz", "zip", "deb"};
        if (!fmts.count(f)) E(std::format("{}: package.formats : format inconnu '{}' (tar.gz|tar.xz|zip|deb)", pf, f));
    }
    static const std::set<std::string> cmds = {"all", "build", "run", "test", "check", "package", "install", "deploy"};
    for (auto& g : p.generate) {
        if (g.command.empty()) E(std::format("{}: generate.{}.command requis", pf, g.name));
        for (auto& c : g.run_before)
            if (!cmds.count(c)) E(std::format("{}: generate.{}.run_before : commande inconnue '{}'", pf, g.name, c));
    }
    // --- Règles pre-commit
    auto check_rules = [&](const std::vector<PreCommitRule>& rules) {
        for (auto& rule : rules) {
            std::string where = std::format("{}: git.pre_commit.{}", rule.file.string(), rule.name);
            if (!util::is_safe_name(rule.name)) E(where + " : nom de règle invalide");
            if (rule.run.empty() == rule.command.empty())
                E(where + " : définir soit 'run' (arguments de cpp-maker), soit 'command' (commande shell)");
            if (rule.on_failure != "block" && rule.on_failure != "warn")
                E(std::format("{} : on_failure invalide '{}' (block|warn)", where, rule.on_failure));
        }
    };
    check_rules(p.pre_commit);
    for (auto& L : layers) check_rules(L.pre_commit);
    for (auto& t : targets)
        if (!t.from_git) check_rules(t.pre_commit);

    std::vector<std::string> unique;
    const std::string prefix = root.string() + "/";
    for (auto& x : e) util::push_unique(unique, util::replace_all(x, prefix, ""));
    return unique;
}

} // namespace model
