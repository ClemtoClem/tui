/* @file model/Scaffold.cpp */

#include "model/Scaffold.hpp"

#include "core/Util.hpp"
#include "core/Version.hpp"
#include "io/toml.hpp"

#include <format>
#include <regex>
#include <set>

namespace scaffold {

namespace {

std::string list(const std::vector<std::string>& v) { return toml::format_value(toml::Value(v)); }
std::string q(const std::string& s) { return toml::quote(s); }

void check_name(const std::string& kind, const std::string& name) {
    if (!util::is_safe_name(name) || name.find('.') != std::string::npos || name.find('+') != std::string::npos)
        util::fail("nom de {} invalide '{}' (lettres, chiffres, _ et -)", kind, name);
}

// --- Sources d'exemple ---------------------------------------------------------

struct Files {
    std::vector<std::pair<fs::path, std::string>> list;
    void add(fs::path p, std::string c) { list.emplace_back(std::move(p), std::move(c)); }
};

/// Espace de noms C++ d'un module : son nom, sauf s'il masquerait une fonction
/// de la bibliothèque C visible depuis l'espace global (namespace log ↔ ::log
/// de <cmath>) — il devient alors <layer>_<nom>.
std::string cxx_namespace(const std::string& name, const std::string& layer) {
    static const std::set<std::string> reserved = {
        "abs", "acos", "asin", "atan", "ceil", "cos", "cosh", "div", "exp", "fabs", "floor", "fmod", "frexp",
        "labs", "ldexp", "log", "log10", "log2", "modf", "pow", "round", "sin", "sinh", "sqrt", "tan", "tanh",
        "trunc", "fmax", "fmin", "hypot", "cbrt", "erf", "gamma", "lgamma", "nan", "rint", "time", "clock",
        "signal", "raise", "exit", "abort", "random", "rand", "index", "select", "read", "write", "open", "close",
        "socket", "connect", "bind", "listen", "accept", "send", "recv", "remove", "rename", "link", "unlink",
        "sync", "wait", "kill", "pipe", "dup", "fork", "sleep", "alarm", "pause", "stat", "access", "error",
        "printf", "puts", "putc", "getc", "free", "malloc", "calloc", "realloc", "system", "atoi", "atof", "qsort"};
    std::string id = util::identifier(name);
    if (!reserved.count(id)) return id;
    return util::identifier((layer.empty() ? std::string("lib") : layer) + "_" + name);
}

Files module_sources(const std::string& name, const std::string& layer, const std::string& type, const std::string& lang) {
    Files f;
    const std::string id = util::identifier(name);
    const bool c = lang == "c";
    const bool header_only = type == "header-only";
    if (c) {
        std::string guard = util::upper(id) + "_H";
        std::string decl = header_only
            ? std::format("static inline int {0}_add(int a, int b) {{ return a + b; }}\n\n"
                          "static inline const char* {0}_name(void) {{ return \"{1}\"; }}\n", id, name)
            : std::format("/** Exemple : renvoie a + b. */\nint {0}_add(int a, int b);\n\n"
                          "/** Nom du module. */\nconst char* {0}_name(void);\n", id);
        f.add(fs::path("include") / name / (name + ".h"),
              std::format("#ifndef {0}\n#define {0}\n\n#ifdef __cplusplus\nextern \"C\" {{\n#endif\n\n{1}\n"
                          "#ifdef __cplusplus\n}}\n#endif\n\n#endif /* {0} */\n", guard, decl));
        if (!header_only)
            f.add(fs::path("src") / (name + ".c"),
                  std::format("#include \"{0}/{0}.h\"\n\nint {1}_add(int a, int b) {{ return a + b; }}\n\n"
                              "const char* {1}_name(void) {{ return \"{0}\"; }}\n", name, id));
        f.add(fs::path("tests") / ("test_" + name + ".cpp"),
              std::format("#include <cpp-maker/test.hpp>\n#include <string>\n\n#include \"{0}/{0}.h\"\n\n"
                          "TEST(add) {{\n    CHECK_EQ({1}_add(2, 3), 5);\n    CHECK_EQ({1}_add(-1, 1), 0);\n}}\n\n"
                          "TEST(name) {{ CHECK_EQ(std::string({1}_name()), \"{0}\"); }}\n", name, id));
        return f;
    }
    const std::string ns = cxx_namespace(name, layer);
    if (header_only) {
        f.add(fs::path("include") / name / (name + ".hpp"),
              std::format("#pragma once\n\n#include <string>\n\nnamespace {0} {{\n\n/// Exemple : renvoie a + b.\n"
                          "inline int add(int a, int b) {{ return a + b; }}\n\n/// Message de bienvenue.\n"
                          "inline std::string greeting(const std::string& who) {{ return \"Bonjour, \" + who + \" !\"; }}\n\n"
                          "}} // namespace {0}\n", ns));
    } else {
        f.add(fs::path("include") / name / (name + ".hpp"),
              std::format("#pragma once\n\n#include <string>\n\nnamespace {0} {{\n\n/// Exemple : renvoie a + b.\n"
                          "int add(int a, int b);\n\n/// Message de bienvenue.\nstd::string greeting(const std::string& who);\n\n"
                          "}} // namespace {0}\n", ns));
        f.add(fs::path("src") / (name + ".cpp"),
              std::format("#include \"{0}/{0}.hpp\"\n\nnamespace {1} {{\n\nint add(int a, int b) {{ return a + b; }}\n\n"
                          "std::string greeting(const std::string& who) {{ return \"Bonjour, \" + who + \" !\"; }}\n\n"
                          "}} // namespace {1}\n", name, ns));
    }
    f.add(fs::path("tests") / ("test_" + name + ".cpp"),
          std::format("#include <cpp-maker/test.hpp>\n\n#include \"{0}/{0}.hpp\"\n\n"
                      "TEST(add) {{\n    CHECK_EQ({1}::add(2, 3), 5);\n    CHECK_EQ({1}::add(-1, 1), 0);\n}}\n\n"
                      "TEST(greeting) {{ CHECK_EQ({1}::greeting(\"monde\"), \"Bonjour, monde !\"); }}\n", name, ns));
    return f;
}

/// Bibliothèque générée par cpp-maker parmi les dépendances (exemple d'appel).
struct Uses {
    std::string name;
    bool c = false;
    std::string ns;   ///< espace de noms C++ lu dans l'en-tête
};

Files app_sources(const std::string& name, const std::string& lang, const std::vector<Uses>& uses) {
    Files f;
    if (lang == "c") {
        std::string inc = "#include <stdio.h>\n";
        std::string body = std::format("    printf(\"Hello from {}!\\n\");\n", name);
        for (auto& u : uses) {
            inc += std::format("#include \"{0}/{0}.h\"\n", u.name);
            body += std::format("    printf(\"{0}_add(2, 3) = %d\\n\", {0}_add(2, 3));\n", util::identifier(u.name));
        }
        f.add(fs::path("src") / "main.c",
              std::format("{}\nint main(int argc, char** argv) {{\n    (void)argc;\n    (void)argv;\n{}    return 0;\n}}\n", inc, body));
        return f;
    }
    std::string inc = "#include <iostream>\n";
    std::string body = std::format("    std::cout << \"Hello from {}!\\n\";\n", name);
    for (auto& u : uses) {
        if (u.c) {
            inc += std::format("#include \"{0}/{0}.h\"\n", u.name);
            body += std::format("    std::cout << \"{0}_add(2, 3) = \" << {0}_add(2, 3) << '\\n';\n", util::identifier(u.name));
        } else {
            inc += std::format("#include \"{0}/{0}.hpp\"\n", u.name);
            body += std::format("    std::cout << {}::greeting(\"monde\") << '\\n';\n", u.ns);
        }
    }
    f.add(fs::path("src") / "main.cpp",
          std::format("{}\nint main(int argc, char** argv) {{\n{}    for (int i = 1; i < argc; ++i) "
                      "std::cout << \"  argument : \" << argv[i] << '\\n';\n    return 0;\n}}\n", inc, body));
    return f;
}

std::vector<Uses> sample_uses(const model::Workspace* ws, const std::vector<std::string>& dep_ids) {
    std::vector<Uses> r;
    if (!ws) return r;
    for (auto& id : dep_ids) {
        auto* t = ws->find(id);
        if (!t) continue;
        fs::path inc = ws->abs(*t) / "include" / t->name;
        std::string text;
        if (util::read_file(inc / (t->name + ".hpp"), text) && text.find("greeting(") != std::string::npos) {
            static const std::regex ns_re(R"(namespace\s+([A-Za-z_]\w*)\s*\{)");
            std::smatch m;
            if (std::regex_search(text, m, ns_re)) r.push_back({t->name, false, m[1].str()});
        } else if (util::read_file(inc / (t->name + ".h"), text) && text.find("_add(") != std::string::npos) {
            r.push_back({t->name, true, ""});
        }
    }
    return r;
}

const char* kGitignore = R"(# cpp-maker
/build/
/dist/
/.cpp-maker.busy
/.cpp-maker-trash/
/compile_commands.json
)";

} // namespace

// --- Fichiers de configuration -----------------------------------------------

std::string project_toml(const ProjectOptions& o, const std::vector<std::string>& layer_order) {
    return std::format(R"(# Configuration du projet (cpp-maker {0}).
# Référence des clés : « cpp-maker config keys project » ; interface : « cpp-maker tui ».

[project]
name = {1}
version = "0.1.0"
description = ""
language = {2}             # langage par défaut des nouvelles cibles : c | c++
cxx_standard = {3}
c_standard = {4}

[paths]
layers = "libs"             # libs/<layer>/<module>/
applications = "apps"       # apps/<application>/

[layers]
# Couches de la plus basse à la plus haute : un module ne peut dépendre que
# de sa couche ou d'une couche inférieure.
order = {5}

[build]
default_mode = "debug"      # release, debug, relwithdebinfo, minsize, asan, tsan, ubsan…
default_toolchain = "host"  # host, gcc, clang, ou [toolchains.<nom>]
jobs = 0                    # 0 = nombre de cœurs - 1

[tests]
framework = "builtin"       # builtin (#include <cpp-maker/test.hpp>) | gtest | none
timeout = 300

[package]
formats = ["tar.gz"]        # tar.gz, tar.xz, zip, deb

# Mode personnalisé :
# [build.modes.profiling]
# base = "relwithdebinfo"
# flags = ["-fno-omit-frame-pointer"]

# Toolchain croisée :
# [toolchains.armhf]
# prefix = "arm-linux-gnueabihf-"

# Déploiement (cpp-maker deploy rpi) :
# [deploy.rpi]
# method = "rsync"
# host = "raspberrypi.local"
# user = "pi"
# path = "/home/pi/{{name}}"
# toolchain = "armhf"
)",
                       cppmaker::kVersion, q(o.name), q(o.language), q(o.cxx_standard), q(o.c_standard),
                       list(layer_order));
}

std::string layer_toml(const LayerOptions& o) {
    return std::format(R"([layer]
name = {}
version = "0.1.0"
description = {}
# modules = []              # liste explicite (défaut : sous-dossiers contenant module.toml)

[dependencies]
# Héritées par chaque module du layer.
modules = {}
pkg_config = []
system = []
)",
                       q(o.name), q(o.description), list(o.deps));
}

std::string module_toml(const ModuleOptions& o, const std::string& language) {
    return std::format(R"([module]
name = {}
version = "0.1.0"
description = {}
type = {}            # static | shared | header-only
language = {}

[dependencies]
modules = {}         # « layer.module », « module » ou « layer.* »
pkg_config = []
system = []
)",
                       q(o.name), q(o.description), q(o.type), q(language), list(o.deps));
}

std::string app_toml(const AppOptions& o, const std::string& language) {
    return std::format(R"([application]
name = {}
version = "0.1.0"
description = {}
language = {}

[dependencies]
modules = {}         # « layer.module », « module » ou « layer.* »
pkg_config = []
system = []

[run]
args = []            # arguments par défaut de « cpp-maker run »
)",
                       q(o.name), q(o.description), q(language), list(o.deps));
}

// --- Création ------------------------------------------------------------------

fs::path create_project(const ProjectOptions& in) {
    ProjectOptions o = in;
    check_name("projet", o.name);
    if (o.language != "c" && o.language != "c++") util::fail("--lang : c ou c++ attendu");
    if (o.layout != "full" && o.layout != "app" && o.layout != "lib" && o.layout != "empty")
        util::fail("--layout : full, app, lib ou empty attendu (reçu '{}')", o.layout);
    if (o.module.empty()) o.module = o.name;
    if (o.app.empty()) o.app = o.name;
    fs::path root = o.dir.empty() ? fs::absolute(o.name) : fs::absolute(o.dir);
    std::error_code ec;
    if (fs::exists(root / model::kProjectFile, ec)) util::fail("un projet existe déjà dans {}", root.string());
    if (!o.in_place && fs::exists(root, ec) && !fs::is_empty(root, ec))
        util::fail("le dossier {} existe et n'est pas vide (cpp-maker init pour l'adopter)", root.string());

    bool with_layer = o.layout == "full" || o.layout == "lib";
    bool with_app = o.layout == "full" || o.layout == "app";
    const bool existed = fs::exists(root, ec);
    try {
    model::Transaction tx(root);
    tx.create_dir(root);
    tx.create_file(root / model::kProjectFile, project_toml(o, with_layer ? std::vector<std::string>{o.layer} : std::vector<std::string>{}));
    tx.create_dir(root / "libs");
    tx.create_dir(root / "apps");
    if (!fs::exists(root / ".gitignore", ec)) tx.create_file(root / ".gitignore", kGitignore);
    if (!fs::exists(root / "README.md", ec))
        tx.create_file(root / "README.md",
                       std::format("# {0}\n\nProjet C/C++ géré par cpp-maker.\n\n```sh\ncpp-maker build        # compile (debug)\n"
                                   "cpp-maker run          # compile et exécute\ncpp-maker test         # tests\n"
                                   "cpp-maker tui          # interface interactive\n```\n", o.name));
    auto ws = tx.commit(false);
    if (with_layer) {
        model::Transaction t2(root);
        add_layer(t2, ws, {o.layer, "", {}, ""});
        ws = t2.commit(false);
        model::Transaction t3(root);
        add_module(t3, ws, {o.layer, o.module, "static", o.language, {}, "", true});
        ws = t3.commit(false);
    }
    if (with_app) {
        model::Transaction t4(root);
        AppOptions a{o.app, "", o.language, {}, "", true};
        if (with_layer) a.deps = {o.layer + "." + o.module};
        add_app(t4, ws, a);
        ws = t4.commit(true);
    }
    } catch (...) {
        // Projet partiellement créé : on ne laisse rien derrière nous.
        if (!existed) fs::remove_all(root, ec);
        else fs::remove(root / model::kProjectFile, ec);
        throw;
    }
    return root;
}

void add_layer(model::Transaction& tx, const model::Workspace& ws, const LayerOptions& o) {
    check_name("layer", o.name);
    if (ws.find_layer(o.name)) util::fail("le layer '{}' existe déjà", o.name);
    std::string base = o.dir.empty() ? ws.project.layers_dirs.front() : o.dir;
    if (!util::contains(ws.project.layers_dirs, base))
        util::fail("--dir : '{}' n'est pas un dossier de layers ([paths].layers : {})", base,
                   util::join(ws.project.layers_dirs, ", "));
    fs::path dir = ws.root / base / o.name;
    std::error_code ec;
    if (fs::exists(dir, ec)) util::fail("le dossier {} existe déjà", util::display(dir, ws.root));
    tx.create_dir(dir);
    tx.create_file(dir / model::kLayerFile, layer_toml(o));
    // Ordre des couches explicite : le nouveau layer devient le plus haut.
    if (!ws.project.layer_order.empty()) {
        auto& d = tx.doc(ws.project.file);
        auto order = model::get_list(d, {"layers"}, "order");
        util::push_unique(order, o.name);
        d.set({"layers"}, "order", toml::Value(order));
    }
}

void add_module(model::Transaction& tx, const model::Workspace& ws, const ModuleOptions& in) {
    ModuleOptions o = in;
    check_name("module", o.name);
    const auto* L = ws.find_layer(o.layer);
    if (!L) {
        std::vector<std::string> names;
        for (auto& l : ws.layers) names.push_back(l.name);
        auto s = util::suggest(o.layer, names);
        util::fail("layer inconnu '{}'{} — créez-le avec « cpp-maker add layer {} »", o.layer,
                   s.empty() ? "" : " (vouliez-vous dire '" + s + "' ?)", o.layer);
    }
    if (ws.find(o.layer + "." + o.name)) util::fail("le module '{}.{}' existe déjà", o.layer, o.name);
    if (o.type != "static" && o.type != "shared" && o.type != "header-only")
        util::fail("--type : static, shared ou header-only attendu (reçu '{}')", o.type);
    std::string lang = o.language.empty() ? ws.project.language : o.language;
    if (lang != "c" && lang != "c++") util::fail("--lang : c ou c++ attendu");
    std::vector<std::string> dep_ids;
    for (auto& d : o.deps)
        for (auto& id : ws.resolve_module_ref(d, o.layer)) util::push_unique(dep_ids, id);

    fs::path dir = ws.root / L->dir / o.name;
    std::error_code ec;
    if (fs::exists(dir, ec)) util::fail("le dossier {} existe déjà", util::display(dir, ws.root));
    tx.create_dir(dir);
    tx.create_file(dir / model::kModuleFile, module_toml(o, lang));
    if (o.sources)
        for (auto& [p, content] : module_sources(o.name, o.layer, o.type, lang).list) tx.create_file(dir / p, content);
    else {
        tx.create_dir(dir / "include" / o.name);
        if (o.type != "header-only") tx.create_dir(dir / "src");
        tx.create_dir(dir / "tests");
    }
    if (!L->modules.empty() && L->has_file) {
        auto& d = tx.doc(L->file);
        auto mods = model::get_list(d, {"layer"}, "modules");
        util::push_unique(mods, o.name);
        d.set({"layer"}, "modules", toml::Value(mods));
    }
}

void add_app(model::Transaction& tx, const model::Workspace& ws, const AppOptions& in) {
    AppOptions o = in;
    check_name("application", o.name);
    if (auto* t = ws.find(o.name); t && t->is_app()) util::fail("l'application '{}' existe déjà", o.name);
    std::string lang = o.language.empty() ? ws.project.language : o.language;
    if (lang != "c" && lang != "c++") util::fail("--lang : c ou c++ attendu");
    std::vector<std::string> dep_ids;
    for (auto& d : o.deps)
        for (auto& id : ws.resolve_module_ref(d)) util::push_unique(dep_ids, id);
    std::string base = o.dir.empty() ? ws.project.apps_dirs.front() : o.dir;
    if (!util::contains(ws.project.apps_dirs, base))
        util::fail("--dir : '{}' n'est pas un dossier d'applications ([paths].applications : {})", base,
                   util::join(ws.project.apps_dirs, ", "));
    fs::path dir = ws.root / base / o.name;
    std::error_code ec;
    if (fs::exists(dir, ec)) util::fail("le dossier {} existe déjà", util::display(dir, ws.root));
    tx.create_dir(dir);
    tx.create_file(dir / model::kApplicationFile, app_toml(o, lang));
    if (o.sources)
        for (auto& [p, content] : app_sources(o.name, lang, sample_uses(&ws, dep_ids)).list) tx.create_file(dir / p, content);
    else
        tx.create_dir(dir / "src");
}

} // namespace scaffold
