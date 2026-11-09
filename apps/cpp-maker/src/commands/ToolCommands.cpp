/* @file commands/ToolCommands.cpp */

#include "commands/ToolCommands.hpp"

#include "analysis/Includes.hpp"
#include "build/Session.hpp"
#include "commands/GlobalContext.hpp"
#include "commands/Options.hpp"
#include "core/Process.hpp"
#include "core/Term.hpp"
#include "core/Util.hpp"
#include "core/Version.hpp"
#include "model/Editor.hpp"
#include "model/Schema.hpp"
#include "ui/TuiApp.hpp"

#include <tui/style/Themes.hpp>
#include "vcs/Git.hpp"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <set>

#include <unistd.h>

namespace commands {

namespace {

const model::LoadOptions kLight{.resolve_git = false, .update_git = false, .validate = false};

struct ConfigTarget {
    fs::path file;
    schema::FileKind kind = schema::FileKind::Project;
    std::string label;
};

ConfigTarget config_target(Context& ctx, const model::Workspace& ws) {
    auto layer = ctx.get<std::string>("--layer");
    auto module = ctx.get<std::string>("--module");
    auto app = ctx.get<std::string>("--app");
    int n = !layer.empty() + !module.empty() + !app.empty();
    if (n > 1) ctx.usage("--layer, --module et --app sont exclusifs");
    if (!layer.empty()) {
        auto* L = ws.find_layer(layer);
        if (!L) util::fail("layer inconnu '{}'", layer);
        return {L->file, schema::FileKind::Layer, "layer " + layer};
    }
    if (!module.empty()) {
        auto ids = ws.resolve_module_ref(module);
        if (ids.size() != 1) util::fail("'{}' désigne plusieurs modules : {}", module, util::join(ids, ", "));
        auto* t = ws.find(ids[0]);
        if (t->from_git) util::fail("'{}' provient d'une dépendance git (non modifiable)", t->id);
        return {t->file, schema::FileKind::Module, "module " + t->id};
    }
    if (!app.empty()) {
        auto* t = ws.find(app);
        if (!t || !t->is_app()) util::fail("application inconnue '{}'", app);
        return {t->file, schema::FileKind::Application, "application " + app};
    }
    return {ws.project.file, schema::FileKind::Project, "projet"};
}

toml::Document load_doc(const fs::path& file) {
    std::error_code ec;
    if (!fs::exists(file, ec)) return toml::Document::from_text("", file);
    return toml::Document::load(file);
}

std::string type_label(const schema::Field& f) {
    switch (f.type) {
    case schema::FieldType::String: return "chaîne";
    case schema::FieldType::Integer: return "entier";
    case schema::FieldType::Boolean: return "booléen";
    case schema::FieldType::StringList: return "liste";
    case schema::FieldType::Enum: return "choix";
    case schema::FieldType::Map: return "table";
    }
    return "";
}

bool matches_filters(const std::string& dotted, const std::vector<std::string>& filters) {
    if (filters.empty()) return true;
    for (auto& f : filters)
        if (dotted == f || dotted.starts_with(f + ".")) return true;
    return false;
}

/// Instances concrètes d'une famille de sections (build.modes.* → {build,modes,release}…).
std::vector<std::vector<std::string>> family_instances(const toml::Value& root, const std::string& pattern) {
    std::vector<std::vector<std::string>> out;
    auto parts = toml::split_path(pattern);
    std::function<void(const toml::Value&, size_t, std::vector<std::string>&)> rec =
        [&](const toml::Value& v, size_t i, std::vector<std::string>& cur) {
            if (i == parts.size()) {
                out.push_back(cur);
                return;
            }
            if (!v.is_table()) return;
            if (parts[i] == "*") {
                for (size_t k = 0; k < v.size(); ++k) {
                    if (!v.value_at(k).is_table()) continue;
                    cur.push_back(v.keys()[k]);
                    rec(v.value_at(k), i + 1, cur);
                    cur.pop_back();
                }
            } else if (auto* c = v.find(parts[i])) {
                cur.push_back(parts[i]);
                rec(*c, i + 1, cur);
                cur.pop_back();
            }
        };
    std::vector<std::string> cur;
    rec(root, 0, cur);
    return out;
}

} // namespace

// ============================================================================
// config
// ============================================================================

void ConfigFileCommand::ConfigureArgs(args::Arguments& a) {
    a.add("--layer").set_value(std::string()).flags("-L").metavar("NOM").help("Éditer libs/<nom>/layer.toml");
    a.add("--module").set_value(std::string()).flags("-M").metavar("LAYER.MODULE").help("Éditer le module.toml du module");
    a.add("--app").set_value(std::string()).flags("-A").metavar("NOM").help("Éditer apps/<nom>/application.toml");
}

void ConfigShowCommand::ConfigureArgs(args::Arguments& a) {
    ConfigFileCommand::ConfigureArgs(a);
    a.add("--defaults").set_value(false).flags("-d").help("Afficher aussi les clés absentes avec leur défaut");
}

int ConfigShowCommand::Run(Context& ctx) {
    auto& ws = ctx.workspace(kLight);
    auto ct = config_target(ctx, ws);
    auto doc = load_doc(ct.file);
    auto filters = ctx.positionals();
    bool defaults = ctx.flag("--defaults");
    term::outln("{}", term::dim(std::format("# {} — {}", ct.label, util::display(ct.file, ws.root))));
    size_t shown = 0;
    for (auto& sec : schema::sections(ct.kind)) {
        std::vector<std::vector<std::string>> instances;
        if (sec.is_family()) instances = family_instances(doc.data(), sec.path);
        else instances = {toml::split_path(sec.path)};
        for (auto& inst : instances) {
            for (auto* f : schema::fields(ct.kind, sec.path)) {
                std::string dotted = toml::join_path(inst) + "." + f->key;
                if (!matches_filters(dotted, filters)) continue;
                const toml::Value* v = doc.get(inst, f->key);
                if (v) {
                    term::outln("{} = {}", dotted, toml::format_value(*v));
                    ++shown;
                } else if (defaults || !filters.empty()) {
                    term::outln("{}", term::dim(std::format("{} = {}   (défaut)", dotted,
                                                             f->placeholder.empty() ? "—" : f->placeholder)));
                    ++shown;
                }
            }
        }
    }
    if (shown == 0 && !filters.empty()) {
        std::vector<std::string> names;
        for (auto& sec : schema::sections(ct.kind))
            for (auto* f : schema::fields(ct.kind, sec.path)) names.push_back(sec.path + "." + f->key);
        auto s = util::suggest(filters[0], names);
        util::fail("aucune clé ne correspond à '{}'{}", filters[0], s.empty() ? "" : " (essayez : " + s + ")");
    }
    return 0;
}

int ConfigGetCommand::Run(Context& ctx) {
    ctx.expect_max_positionals(1);
    auto& ws = ctx.workspace(kLight);
    auto ct = config_target(ctx, ws);
    std::string dotted = ctx.positional(0, "clé");
    std::vector<std::string> sec;
    std::string key;
    bool known = schema::split_key(ct.kind, dotted, sec, key);
    auto doc = load_doc(ct.file);
    if (auto* v = doc.get(sec, key)) {
        term::outln("{}", toml::value_to_text(*v));
        return 0;
    }
    if (known)
        if (auto* f = schema::find(ct.kind, sec, key); f && !f->placeholder.empty()) {
            term::outln("{}", f->placeholder);
            return 0;
        }
    return 1;
}

std::string ConfigSetCommand::GetDescription() const {
    if (mode_ == "set") return "Définir une valeur (créée si absente)";
    if (mode_ == "unset") return "Supprimer une clé (retour à la valeur par défaut)";
    if (mode_ == "add") return "Ajouter des éléments à une liste";
    return "Retirer des éléments d'une liste";
}

std::string ConfigSetCommand::GetUsage() const {
    if (mode_ == "unset") return "<section.clé>… [--layer L|--module M|--app A]";
    if (mode_ == "set") return "<section.clé> <valeur…> | <clé=valeur>… [--layer L|--module M|--app A]";
    return "<section.clé> <valeur…> [--layer L|--module M|--app A] [-- valeurs commençant par -]";
}

std::string ConfigSetCommand::GetDetails() const {
    return "Les valeurs sont converties selon le schéma (cpp-maker config keys) : listes séparées\n"
           "par des virgules ou en plusieurs arguments, booléens true/false, entiers. Les valeurs\n"
           "commençant par « - » se placent après « -- » (config add build.warnings -- -Wshadow).";
}

std::vector<std::string> ConfigSetCommand::GetExamples() const {
    if (mode_ == "set")
        return {"cpp-maker config set project.version 1.2.0", "cpp-maker config set build.jobs 4",
                "cpp-maker config set build.modes.prof.base relwithdebinfo",
                "cpp-maker config set module.type shared --module core.log",
                "cpp-maker config set dependencies.modules core.log,core.math --app hello",
                "cpp-maker config set deploy.rpi.method=rsync deploy.rpi.host=rpi.local deploy.rpi.path=/opt/app"};
    if (mode_ == "add") return {"cpp-maker config add build.warnings -- -Wshadow -Wundef", "cpp-maker config add package.formats deb"};
    if (mode_ == "remove") return {"cpp-maker config remove package.formats zip"};
    return {"cpp-maker config unset build.jobs", "cpp-maker config unset build.modes.prof"};
}

void ConfigSetCommand::ConfigureArgs(args::Arguments& a) {
    ConfigFileCommand::ConfigureArgs(a);
    if (mode_ == "set") a.add("--force").set_value(false).help("Accepter une clé absente du schéma");
}

int ConfigSetCommand::Run(Context& ctx) {
    auto& ws = ctx.workspace(kLight);
    auto ct = config_target(ctx, ws);
    const auto pos = ctx.positionals();
    if (pos.empty()) ctx.usage("clé manquante");
    const std::string kind_arg = ct.kind == schema::FileKind::Project ? "project"
                                 : ct.kind == schema::FileKind::Layer ? "layer"
                                 : ct.kind == schema::FileKind::Module ? "module" : "app";
    model::Transaction tx(ws.root);
    auto& doc = tx.doc(ct.file);

    // Une affectation : renvoie la valeur écrite.
    auto apply = [&](const std::string& dotted, const std::vector<std::string>& values) -> toml::Value {
        std::vector<std::string> sec;
        std::string key;
        (void)schema::split_key(ct.kind, dotted, sec, key);
        const schema::Field* f = schema::find(ct.kind, sec, key);
        if (mode_ == "unset") {
            if (!values.empty()) ctx.usage("unset n'attend pas de valeur");
            if (!doc.remove(sec, key) && !doc.remove_table(toml::split_path(dotted)))
                util::fail("{} : clé absente de {}", dotted, util::display(ct.file, ws.root));
            return {};
        }
        if (values.empty()) ctx.usage("valeur manquante pour " + dotted);
        if (!f && !(mode_ == "set" && ctx.flag("--force"))) {
            std::vector<std::string> names;
            for (auto& sc : schema::sections(ct.kind))
                for (auto* x : schema::fields(ct.kind, sc.path)) names.push_back(sc.path + "." + x->key);
            auto sg = util::suggest(dotted, names);
            util::fail("clé inconnue '{}' pour un fichier {}{} (cpp-maker config keys {} ; --force pour l'imposer)", dotted,
                       schema::file_name(ct.kind), sg.empty() ? "" : " — vouliez-vous dire '" + sg + "' ?", kind_arg);
        }
        toml::Value v;
        if (mode_ == "set") {
            if (f && f->type == schema::FieldType::StringList) {
                if (values.size() == 1 && values[0].starts_with("[")) v = schema::parse_input(*f, values[0]);
                else {
                    std::vector<std::string> items;
                    for (auto& x : values)
                        for (auto& p : util::split_list(x)) items.push_back(p);
                    v = toml::Value(items);
                    // [paths] : un seul dossier s'écrit comme chaîne simple.
                    if (sec == std::vector<std::string>{"paths"} && items.size() == 1) v = toml::Value(items[0]);
                }
            } else {
                std::string text = util::join(values, " ");
                v = f ? schema::parse_input(*f, text) : *toml::value_from_text(text, toml::Type::None);
            }
        } else {
            if (!f || f->type != schema::FieldType::StringList) util::fail("{} n'est pas une liste", dotted);
            auto list = model::get_list(doc, sec, key);
            // Liste absente : on part de sa valeur par défaut (ex. build.warnings).
            if (!doc.get(sec, key) && !f->placeholder.empty()) list = util::split_list(f->placeholder);
            for (auto& x : values)
                for (auto& p : util::split_list(x)) {
                    if (mode_ == "add") util::push_unique(list, p);
                    else if (!util::contains(list, p)) util::fail("'{}' absent de {}", p, dotted);
                    else std::erase(list, p);
                }
            v = toml::Value(list);
        }
        doc.set(sec, key, v);
        return v;
    };

    std::vector<std::pair<std::string, toml::Value>> done;
    if (mode_ == "set" && pos[0].find('=') != std::string::npos) {
        // Plusieurs affectations « clé=valeur » validées ensemble.
        for (auto& a : pos) {
            auto eq = a.find('=');
            if (eq == std::string::npos) ctx.usage("affectation attendue (clé=valeur) : " + a);
            std::string k = a.substr(0, eq);
            done.emplace_back(k, apply(k, {a.substr(eq + 1)}));
        }
    } else if (mode_ == "unset") {
        for (auto& k : pos) done.emplace_back(k, apply(k, {}));
    } else {
        std::vector<std::string> values(pos.begin() + 1, pos.end());
        for (auto& t : ctx.args().trailing()) values.push_back(t);
        done.emplace_back(pos[0], apply(pos[0], values));
    }
    tx.commit();
    for (auto& [k, v] : done) {
        if (mode_ == "unset") term::ok("{} supprimé ({})", k, util::display(ct.file, ws.root));
        else term::ok("{} = {}   ({})", k, toml::format_value(v), util::display(ct.file, ws.root));
    }
    return 0;
}

int ConfigValidateCommand::Run(Context& ctx) {
    ctx.expect_max_positionals(0);
    auto& ws = ctx.workspace(kLight);
    auto errors = ws.validate();
    for (auto& w : ws.warnings) term::warn("{}", w);
    for (auto& e : errors) term::error("{}", e);
    if (!errors.empty()) {
        term::errln("{}", term::red(std::format("✗ {} erreur(s)", errors.size())));
        return 1;
    }
    term::ok("configuration valide : {} layer(s), {} module(s), {} application(s){}", ws.layers.size(),
             ws.modules().size(), ws.applications().size(),
             ws.warnings.empty() ? "" : std::format(", {} avertissement(s)", ws.warnings.size()));
    return 0;
}

int ConfigKeysCommand::Run(Context& ctx) {
    ctx.expect_max_positionals(1);
    std::string k = ctx.positional_or(0, "project");
    schema::FileKind kind;
    if (k == "project" || k == "projet") kind = schema::FileKind::Project;
    else if (k == "layer") kind = schema::FileKind::Layer;
    else if (k == "module") kind = schema::FileKind::Module;
    else if (k == "app" || k == "application") kind = schema::FileKind::Application;
    else ctx.usage("type de fichier inconnu '" + k + "' (project|layer|module|app)");
    term::outln("{}", term::bold(std::format("Clés de {}", schema::file_name(kind))));
    for (auto& sec : schema::sections(kind)) {
        term::outln("\n[{}]  {}", term::cyan(sec.path), term::dim(sec.title + (sec.help.empty() ? "" : " — " + sec.help)));
        for (auto* f : schema::fields(kind, sec.path)) {
            std::string extra;
            if (f->type == schema::FieldType::Enum) {
                std::vector<std::string> c;
                for (auto& x : f->choices) c.push_back(x.empty() ? "\"\"" : x);
                extra = " (" + util::join(c, "|") + ")";
            }
            if (!f->placeholder.empty()) extra += " [défaut : " + f->placeholder + "]";
            term::outln("  {:<26} {:<8} {}{}", f->key, type_label(*f), f->help.empty() ? f->label : f->help, term::dim(extra));
        }
    }
    return 0;
}

int ConfigEditCommand::Run(Context& ctx) {
    ctx.expect_max_positionals(0);
    auto& ws = ctx.workspace(kLight);
    auto ct = config_target(ctx, ws);
    std::string editor;
    for (auto var : {"VISUAL", "EDITOR"})
        if (const char* e = std::getenv(var); e && *e) { editor = e; break; }
    if (editor.empty()) editor = util::have("nano") ? "nano" : "vi";
    std::string before;
    util::read_file(ct.file, before);
    auto r = proc::shell(editor + " " + util::shell_quote(ct.file.string()));
    if (r.exit_code != 0) util::fail("l'éditeur a échoué (code {})", r.exit_code);
    try {
        auto nw = model::Workspace::load(ctx.cwd(), kLight);
        auto errors = nw.validate();
        for (auto& w : nw.warnings) term::warn("{}", w);
        for (auto& e : errors) term::error("{}", e);
        if (!errors.empty()) return 1;
    } catch (const std::exception& e) {
        term::error("{}", e.what());
        term::outln("  {}", term::dim("le fichier modifié est conservé ; relancez « cpp-maker config edit » pour corriger"));
        return 1;
    }
    term::ok("{} valide", util::display(ct.file, ws.root));
    return 0;
}

// ============================================================================
// export
// ============================================================================

std::string ExportCommand::GetDescription() const {
    if (what_ == "compdb") return "Écrire compile_commands.json à la racine (clangd, VS Code…)";
    if (what_ == "ninja") return "Écrire build/<tc>/<mode>/build.ninja";
    return "Archiver des cibles (configuration + sources) dans un fichier XML";
}

std::string ExportCommand::GetUsage() const {
    if (what_ == "xml") return "[cible…] [--file F|-] [--no-sources] [--no-headers] [--no-tests]";
    return "[mode] [cible…] [-t tc]";
}

void ExportCommand::ConfigureArgs(args::Arguments& a) {
    if (what_ == "xml") {
        a.add("--file").set_value(std::string()).flags("-o").metavar("FICHIER").help("Fichier de sortie (- = sortie standard)");
        a.add("--no-sources").set_value(false).help("Sans les sources");
        a.add("--no-headers").set_value(false).help("Sans les en-têtes");
        a.add("--no-tests").set_value(false).help("Sans les tests");
        return;
    }
    add_build_options(a, kOptTargets | kOptToolchain | kOptSanitize);
}

namespace {

std::string cdata(std::string_view s) {
    std::string clean;
    for (char c : s)
        if (static_cast<unsigned char>(c) >= 0x20 || c == '\n' || c == '\t' || c == '\r') clean += c;
    return "<![CDATA[" + util::replace_all(clean, "]]>", "]]]]><![CDATA[>") + "]]>";
}

void collect(const fs::path& dir, std::vector<fs::path>& out, const std::set<std::string>& exts) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return;
    for (auto& e : fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec))
        if (e.is_regular_file(ec) && exts.count(e.path().extension().string())) out.push_back(e.path());
}

} // namespace

int ExportCommand::Run(Context& ctx) {
    auto& ws = ctx.workspace(what_ == "xml" ? kLight : model::LoadOptions{});
    if (what_ == "xml") {
        std::vector<std::string> ids = ws.select(ctx.positionals(), ctx.cwd());
        static const std::set<std::string> src = {".c", ".cpp", ".cc", ".cxx", ".S", ".s"};
        static const std::set<std::string> hdr = {".h", ".hpp", ".hh", ".hxx", ".ipp", ".tpp", ".inl"};
        std::string xml = std::format("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<cpp-maker-export version=\"{}\" "
                                      "project=\"{}\" project-version=\"{}\">\n",
                                      cppmaker::kVersion, util::xml_escape(ws.project.name), util::xml_escape(ws.project.version));
        auto add_file = [&](const fs::path& f, const fs::path& base, const char* role) {
            std::string text;
            if (!util::read_file(f, text)) return;
            xml += std::format("    <file path=\"{}\" role=\"{}\">{}</file>\n", util::xml_escape(f.lexically_relative(base).generic_string()),
                               role, cdata(text));
        };
        add_file(ws.project.file, ws.root, "config");
        size_t count = 0;
        for (auto& id : ids) {
            auto* t = ws.find(id);
            if (t->from_git) continue;
            fs::path dir = ws.abs(*t);
            xml += std::format("  <target id=\"{}\" kind=\"{}\" type=\"{}\" dir=\"{}\">\n", util::xml_escape(t->id),
                               t->kind_label(), t->type, util::xml_escape(t->dir.generic_string()));
            add_file(t->file, dir, "config");
            if (t->is_module())
                if (auto* L = ws.find_layer(t->layer); L && L->has_file) add_file(L->file, dir, "layer");
            std::vector<fs::path> files;
            if (!ctx.flag("--no-sources"))
                for (auto& d : t->source_dirs) collect(util::under(dir, d), files, src);
            if (!ctx.flag("--no-headers")) {
                for (auto& d : t->public_includes) collect(util::under(dir, d), files, hdr);
                for (auto& d : t->private_includes) collect(util::under(dir, d), files, hdr);
            }
            std::vector<fs::path> tests;
            if (!ctx.flag("--no-tests"))
                for (auto& d : t->test_dirs) {
                    collect(util::under(dir, d), tests, src);
                    collect(util::under(dir, d), tests, hdr);
                }
            std::sort(files.begin(), files.end());
            files.erase(std::unique(files.begin(), files.end()), files.end());
            for (auto& f : files) add_file(f, dir, hdr.count(f.extension().string()) ? "header" : "source");
            std::sort(tests.begin(), tests.end());
            for (auto& f : tests) add_file(f, dir, "test");
            xml += "  </target>\n";
            ++count;
        }
        xml += "</cpp-maker-export>\n";
        std::string out = ctx.get<std::string>("--file");
        if (out == "-") {
            std::cout << xml;
            return 0;
        }
        fs::path file = out.empty() ? ws.dist_path() / std::format("{}-export.xml", ws.project.name) : util::under(ctx.cwd(), out);
        util::write_file(file, xml);
        term::ok("{} cible(s) exportée(s) : {} ({})", count, util::display(file, ctx.cwd()), util::human_size(xml.size()));
        return 0;
    }
    auto r = read_request(ctx);
    auto ids = ws.select(r.selectors.empty() ? std::vector<std::string>{"all"} : r.selectors, ctx.cwd());
    for (auto& tcn : build::pick_toolchains(ws, r))
        for (auto& mode : build::pick_modes(ws, r)) {
            auto s = build::Session::open(ws, tcn, mode, ids, true, r, false);
            if (what_ == "compdb") {
                build::write_compdb(*s.plan, ws.root / "compile_commands.json");
                term::ok("compile_commands.json écrit ({} · {}, {} fichier(s))", s.tc->name, s.mode.name,
                         std::count_if(s.plan->tasks.begin(), s.plan->tasks.end(), [](auto& t) { return t.is_compile(); }));
            } else {
                auto f = build::write_ninja(*s.plan);
                term::ok("{} écrit — ninja -f {}", util::display(f, ws.root), util::display(f, ctx.cwd()));
            }
        }
    return 0;
}

// ============================================================================
// toolchain
// ============================================================================

namespace {

bool is_debian_like() {
    std::error_code ec;
    return fs::exists("/etc/debian_version", ec) && util::have("apt-get");
}

std::vector<int> installed_gcc_versions() {
    std::set<int> versions;
    const char* env = std::getenv("PATH");
    for (auto& dir : util::split_list(env ? env : "/usr/bin", ':')) {
        std::error_code ec;
        for (auto& e : fs::directory_iterator(dir, ec)) {
            auto name = e.path().filename().string();
            for (std::string_view pre : {"gcc-", "g++-"}) {
                if (!name.starts_with(pre)) continue;
                auto rest = name.substr(pre.size());
                if (!rest.empty() && std::all_of(rest.begin(), rest.end(), ::isdigit)) versions.insert(std::stoi(rest));
            }
        }
    }
    return {versions.begin(), versions.end()};
}

std::optional<int> resolved_gcc_version(const fs::path& link) {
    std::error_code ec;
    auto canon = fs::canonical(link, ec);
    if (ec) return std::nullopt;
    auto name = canon.filename().string();
    for (std::string_view pre : {"gcc-", "g++-", "x86_64-linux-gnu-gcc-", "x86_64-linux-gnu-g++-"}) {
        if (!name.starts_with(pre)) continue;
        auto rest = name.substr(pre.size());
        if (!rest.empty() && std::all_of(rest.begin(), rest.end(), ::isdigit)) return std::stoi(rest);
    }
    return std::nullopt;
}

int as_root(const std::vector<std::string>& argv) {
    std::vector<std::string> full;
    if (::geteuid() != 0) {
        if (!util::have("sudo")) {
            term::error("privilèges root ou sudo requis pour : {}", util::join(argv, " "));
            return 127;
        }
        full.push_back("sudo");
    }
    full.insert(full.end(), argv.begin(), argv.end());
    term::outln("  {} {}", term::magenta("$"), util::shell_join(full));
    return proc::run(full).exit_code;
}

void as_root_checked(const std::vector<std::string>& argv) {
    if (int rc = as_root(argv); rc != 0) util::fail("commande en échec (code {}) : {}", rc, util::join(argv, " "));
}

int parse_major(const std::string& s) {
    if (s.empty() || !std::all_of(s.begin(), s.end(), ::isdigit)) util::fail("version invalide '{}' (ex. 14)", s);
    int v = std::stoi(s);
    if (v <= 0 || v > 99) util::fail("version invalide '{}'", s);
    return v;
}

void print_toolchain(const toolchain::Toolchain& t) {
    auto row = [](const char* k, const std::string& v) { term::outln("  {:<12} {}", k, v.empty() ? "—" : v); };
    row("c++", t.cxx ? t.cxx->exe + " — " + t.cxx->version : "");
    row("c", t.cc ? t.cc->exe + " — " + t.cc->version : "");
    row("famille", std::string(toolchain::family_name(t.family())));
    row("ar", t.ar);
    row("strip", t.strip);
    row("pkg-config", t.pkg_config);
    row("triplet", t.triple);
    row("cible", t.arch + " / " + t.os + (t.cross ? " (croisée)" : " (native)"));
    row("lanceur", t.runner.empty() ? (t.cross ? t.runner_note : "exécution directe") : util::join(t.runner, " "));
}

} // namespace

std::string ToolchainCommand::GetDescription() const {
    if (sub_ == "list") return "Toolchains du projet, intégrées, croisées détectées et versions gcc";
    if (sub_ == "show") return "Détail d'une toolchain résolue";
    if (sub_ == "which") return "Chemins des compilateurs C++ et C";
    if (sub_ == "add") return "Déclarer une toolchain dans project.toml";
    if (sub_ == "use") return "Définir gcc-<N> comme compilateur par défaut (Debian/Ubuntu)";
    if (sub_ == "install") return "Installer gcc-<N> ou un compilateur croisé (apt)";
    return "Désinstaller gcc-<N> (apt)";
}

std::string ToolchainCommand::GetUsage() const {
    if (sub_ == "list") return "";
    if (sub_ == "show" || sub_ == "which") return "[nom]";
    if (sub_ == "add") return "<nom> [--prefix P] [--cc C] [--cxx X] [--target T] [--sysroot S] [--runner R]";
    if (sub_ == "install") return "<version|triplet> [-y]";
    return "<version> [-y]";
}

std::vector<std::string> ToolchainCommand::GetExamples() const {
    if (sub_ == "add")
        return {"cpp-maker toolchain add armhf --prefix arm-linux-gnueabihf-",
                "cpp-maker toolchain add clang19 --cc clang-19 --cxx clang++-19",
                "cpp-maker toolchain add win64 --prefix x86_64-w64-mingw32-"};
    if (sub_ == "install") return {"cpp-maker toolchain install 14", "cpp-maker toolchain install arm-linux-gnueabihf"};
    if (sub_ == "use") return {"cpp-maker toolchain use 14"};
    return {};
}

void ToolchainCommand::ConfigureArgs(args::Arguments& a) {
    if (sub_ == "install" || sub_ == "remove") a.add("--yes").set_value(false).flags("-y").help("Sans confirmation");
    if (sub_ == "add") {
        for (auto [k, h] : {std::pair{"--prefix", "Préfixe des outils (arm-linux-gnueabihf-)"},
                            {"--cc", "Compilateur C"}, {"--cxx", "Compilateur C++"},
                            {"--target", "Triplet clang (--target=)"}, {"--sysroot", "Sysroot"},
                            {"--family", "auto | gcc | clang"}, {"--os", "OS cible (vide = déduit)"}})
            a.add(k).set_value(std::string()).metavar("VALEUR").help(h);
        a.add("--runner").set_value(std::vector<std::string>{}).metavar("CMD,…").help("Émulateur (qemu-arm,-L,/usr/…)");
        a.add("--default").set_value(false).help("En faire la toolchain par défaut");
    }
}

int ToolchainCommand::Run(Context& ctx) {
    std::optional<model::Workspace> ws;
    if (model::Workspace::find_root(ctx.cwd())) {
        try {
            ws = model::Workspace::load(ctx.cwd(), kLight);
        } catch (const std::exception& e) {
            term::warn("{}", e.what());
        }
    }
    model::ProjectConfig none;
    const model::ProjectConfig& p = ws ? ws->project : none;

    if (sub_ == "list") {
        ctx.expect_max_positionals(0);
        term::outln("{}", term::bold("Toolchains du projet"));
        if (p.toolchains.empty()) term::outln("  (aucune — « host » implicite ; cpp-maker toolchain add <nom>)");
        for (auto& [n, t] : p.toolchains) {
            std::string what = !t.prefix.empty() ? "prefix " + t.prefix : std::format("cc {} · cxx {}", t.cc, t.cxx);
            if (!t.target.empty()) what += " · target " + t.target;
            term::outln("  {:<14} {}{}", term::cyan(n), what, p.default_toolchain == n ? term::green("  (défaut)") : "");
        }
        term::outln("\n{}", term::bold("Toolchains intégrées"));
        term::outln("  {:<14} compilateur natif ($CC/$CXX, sinon gcc, clang)", "host");
        term::outln("  {:<14} gcc / g++", "gcc");
        term::outln("  {:<14} clang / clang++", "clang");
        auto pre = toolchain::detect_prefixes();
        term::outln("\n{} {}", term::bold("Compilateurs croisés détectés"), term::dim("(-t <triplet>)"));
        if (pre.empty()) term::outln("  (aucun — cpp-maker toolchain install <triplet>)");
        for (auto& x : pre) term::outln("  {}", x.substr(0, x.size() - 1));
        auto versions = installed_gcc_versions();
        auto cur = resolved_gcc_version("/usr/bin/g++");
        term::outln("\n{}", term::bold("Versions gcc de l'hôte"));
        if (versions.empty()) term::outln("  (aucune version suffixée)");
        for (int v : versions) term::outln("  g++-{}{}", v, cur == v ? term::green("  (défaut)") : "");
        return 0;
    }
    if (sub_ == "show" || sub_ == "which") {
        ctx.expect_max_positionals(1);
        auto t = toolchain::resolve(p, ctx.positional_or(0, p.default_toolchain));
        if (sub_ == "which") {
            if (t.cxx) term::outln("{}", t.cxx->exe);
            if (t.cc) term::outln("{}", t.cc->exe);
            return 0;
        }
        term::outln("{} {}", term::bold("Toolchain"), term::cyan(t.name));
        print_toolchain(t);
        if (ws) term::outln("  {:<12} {}/{}/<mode>", "build", ws->project.build_dir, t.name);
        return 0;
    }
    if (sub_ == "add") {
        ctx.expect_max_positionals(1);
        if (!ws) util::fail("aucun projet ici (cpp-maker new <nom>)");
        std::string name = ctx.positional(0, "nom de la toolchain");
        if (!util::is_safe_name(name)) ctx.usage("nom invalide '" + name + "'");
        model::Transaction tx(ws->root);
        auto& d = tx.doc(ws->project.file);
        bool any = false;
        for (auto k : {"prefix", "cc", "cxx", "target", "sysroot", "family", "os"}) {
            auto v = ctx.get<std::string>(std::string("--") + k);
            if (v.empty()) continue;
            d.set({"toolchains", name}, k, toml::Value(v));
            any = true;
        }
        if (auto r = ctx.get<std::vector<std::string>>("--runner"); !r.empty()) {
            d.set({"toolchains", name}, "runner", toml::Value(r));
            any = true;
        }
        if (!any) d.ensure_table({"toolchains", name});
        if (ctx.flag("--default")) d.set({"build"}, "default_toolchain", toml::Value(name));
        auto nw = tx.commit();
        term::ok("toolchain '{}' déclarée dans project.toml", name);
        try {
            print_toolchain(toolchain::resolve(nw.project, name));
        } catch (const util::Error& e) {
            term::warn("{}", e.what());
        }
        return 0;
    }
    bool yes = ctx.args().has("--yes") && ctx.flag("--yes");
    if (sub_ == "use") {
        ctx.expect_max_positionals(1);
        int v = parse_major(ctx.positional(0, "version"));
        fs::path gcc = std::format("/usr/bin/gcc-{}", v), gxx = std::format("/usr/bin/g++-{}", v);
        if (!fs::exists(gcc) || !fs::exists(gxx))
            util::fail("gcc-{0}/g++-{0} introuvables — cpp-maker toolchain install {0}", v);
        if (as_root({"update-alternatives", "--install", "/usr/bin/gcc", "gcc", gcc.string(), "200", "--slave",
                     "/usr/bin/g++", "g++", gxx.string()}) != 0) {
            as_root_checked({"update-alternatives", "--install", "/usr/bin/gcc", "gcc", gcc.string(), "200"});
            (void)as_root({"update-alternatives", "--install", "/usr/bin/g++", "g++", gxx.string(), "200"});
        }
        (void)as_root({"update-alternatives", "--set", "gcc", gcc.string()});
        (void)as_root({"update-alternatives", "--set", "g++", gxx.string()});
        auto r = proc::capture({"g++", "-dumpversion"});
        if (r.exit_code != 0 || util::trim(r.output).substr(0, util::trim(r.output).find('.')) != std::to_string(v)) {
            term::error("la version active reste g++ {}", util::trim(r.output));
            return 1;
        }
        term::ok("g++ → g++-{}", v);
        return 0;
    }
    if (sub_ == "install") {
        ctx.expect_max_positionals(1);
        std::string what = ctx.positional(0, "version ou triplet");
        if (!is_debian_like()) util::fail("installation automatique : Debian/Ubuntu (apt) uniquement");
        if (!std::isdigit(static_cast<unsigned char>(what[0]))) {
            if (!util::is_safe_name(what)) util::fail("triplet invalide '{}'", what);
            std::vector<std::string> pkgs = {"gcc-" + what, "g++-" + what};
            bool mingw = what.find("mingw32") != std::string::npos;
            if (mingw) pkgs = {"mingw-w64"};
            else pkgs.push_back("qemu-user");
            if (!term::confirm(std::format("Installer {} ?", util::join(pkgs, " ")), yes)) return 1;
            as_root_checked({"apt-get", "update"});
            std::vector<std::string> cmd = {"apt-get", "install", "-y"};
            cmd.insert(cmd.end(), pkgs.begin(), pkgs.end());
            as_root_checked(cmd);
            term::ok("{} installé — cpp-maker build -t {}", what, what);
            return 0;
        }
        int v = parse_major(what);
        if (!term::confirm(std::format("Installer gcc-{0} et g++-{0} (PPA ubuntu-toolchain-r/test si besoin) ?", v), yes))
            return 1;
        as_root_checked({"apt-get", "update"});
        if (as_root({"apt-get", "install", "-y", std::format("gcc-{}", v), std::format("g++-{}", v)}) != 0) {
            if (!util::have("add-apt-repository")) as_root_checked({"apt-get", "install", "-y", "software-properties-common"});
            as_root_checked({"add-apt-repository", "-y", "ppa:ubuntu-toolchain-r/test"});
            as_root_checked({"apt-get", "update"});
            as_root_checked({"apt-get", "install", "-y", std::format("gcc-{}", v), std::format("g++-{}", v)});
        }
        term::ok("gcc-{0}/g++-{0} installés — par défaut : cpp-maker toolchain use {0} ; ponctuel : -t g++-{0}", v);
        return 0;
    }
    // remove
    ctx.expect_max_positionals(1);
    int v = parse_major(ctx.positional(0, "version"));
    if (!is_debian_like()) util::fail("désinstallation automatique : Debian/Ubuntu (apt) uniquement");
    if (resolved_gcc_version("/usr/bin/g++") == v || resolved_gcc_version("/usr/bin/gcc") == v)
        util::fail("gcc-{} est la version par défaut : changez-la d'abord (cpp-maker toolchain use <autre>)", v);
    if (!term::confirm(std::format("Désinstaller gcc-{0} et g++-{0} ?", v), yes)) return 1;
    as_root_checked({"apt-get", "purge", "--autoremove", "-y", std::format("gcc-{}", v), std::format("g++-{}", v)});
    term::ok("gcc-{} désinstallé", v);
    return 0;
}

// ============================================================================
// doctor
// ============================================================================

void DoctorCommand::ConfigureArgs(args::Arguments& a) {
    a.add("--fix").set_value(false).help("Ajouter les dépendances manquantes et nettoyer les règles pre-commit");
    a.add("--strict").set_value(false).help("Compter les dépendances implicites comme des erreurs");
}

int DoctorCommand::Run(Context& ctx) {
    ctx.expect_max_positionals(0);
    int problems = 0;
    auto good = [](const std::string& m) { term::outln("  {} {}", term::green("✓"), m); };
    auto bad = [&](const std::string& m) {
        term::outln("  {} {}", term::red("✗"), m);
        ++problems;
    };
    auto note = [](const std::string& m) { term::outln("  {} {}", term::yellow("•"), m); };

    term::outln("{}", term::bold("Configuration"));
    std::optional<model::Workspace> ws;
    if (!model::Workspace::find_root(ctx.cwd())) {
        note("aucun project.toml ici : vérification de l'environnement seulement");
    } else {
        try {
            ws = model::Workspace::load(ctx.cwd(), kLight);
            auto errors = ws->validate();
            if (errors.empty()) good(std::format("{} valide ({} layer(s), {} module(s), {} application(s))",
                                                 util::display(ws->project.file, ctx.cwd()), ws->layers.size(),
                                                 ws->modules().size(), ws->applications().size()));
            for (auto& e : errors) bad(e);
            for (auto& w : ws->warnings) note(w);
        } catch (const std::exception& e) {
            bad(e.what());
        }
    }

    term::outln("\n{}", term::bold("Outils"));
    struct T { const char* exe; const char* role; bool required; };
    for (auto t : {T{"ar", "archives statiques", true}, T{"pkg-config", "dépendances pkg-config", false},
                   T{"ninja", "--backend ninja", false}, T{"git", "dépendances git", false},
                   T{"tar", "package tar.gz/tar.xz", false}, T{"zip", "package zip", false},
                   T{"dpkg-deb", "package deb", false}, T{"sha256sum", "sommes de contrôle", false},
                   T{"rsync", "deploy rsync", false}, T{"ssh", "deploy rsync/scp", false},
                   T{"valgrind", "mode valgrind", false}, T{"qemu-arm", "exécution ARM", false},
                   T{"qemu-aarch64", "exécution AArch64", false}, T{"wine", "exécution Windows", false}}) {
        if (auto p = util::find_in_path(t.exe)) good(std::format("{:<12} {}", t.exe, term::dim(p->string())));
        else if (t.required) bad(std::format("{:<12} introuvable ({})", t.exe, t.role));
        else note(std::format("{:<12} absent — {}", t.exe, t.role));
    }

    term::outln("\n{}", term::bold("Toolchains"));
    model::ProjectConfig none;
    const auto& p = ws ? ws->project : none;
    std::vector<std::string> names = {p.default_toolchain};
    for (auto& [n, t] : p.toolchains) util::push_unique(names, n);
    for (auto& n : names) {
        try {
            auto tc = toolchain::resolve(p, n);
            term::outln("  {} ({} / {})", term::cyan(tc.name), tc.arch, tc.os);
            for (auto [lang, tool] : {std::pair{"c++", tc.cxx}, {"c", tc.cc}}) {
                if (!tool) {
                    note(std::format("pas de compilateur {}", lang));
                    continue;
                }
                std::string err;
                std::string standard = std::string(lang) == "c" ? p.c_standard : p.cxx_standard;
                if (toolchain::smoke_compile(*tool, lang, standard, &err)) good(std::format("{} {} ({})", lang, tool->exe, standard));
                else bad(std::format("{} {} : échec de compilation en {}\n      {}", lang, tool->exe, standard,
                                     util::replace_all(util::trim(err.substr(0, 400)), "\n", "\n      ")));
            }
            if (!tc.cross && tc.cxx) {
                std::vector<std::string> ok, ko;
                std::vector<std::string> sans = {"address", "undefined", "thread", "leak"};
                if (tc.family() == toolchain::Family::Clang) sans.push_back("memory");
                for (auto& s : sans)
                    (toolchain::smoke_compile(*tc.cxx, "c++", p.cxx_standard, nullptr, {"-fsanitize=" + s}) ? ok : ko).push_back(s);
                if (!ok.empty()) good("sanitizers : " + util::join(ok, ", "));
                if (!ko.empty()) note("sanitizers indisponibles : " + util::join(ko, ", "));
            }
            if (tc.cross) {
                if (tc.runner.empty()) note("exécution impossible : " + tc.runner_note);
                else good("lanceur : " + util::join(tc.runner, " "));
            }
        } catch (const std::exception& e) {
            bad(std::format("{} : {}", n, e.what()));
        }
    }

    if (ws) {
        term::outln("\n{}", term::bold("Dépendances implicites (#include)"));
        auto missing = analysis::detect_missing_deps(*ws);
        if (missing.empty()) good("toutes les inclusions d'en-têtes publics sont couvertes par les dépendances déclarées");
        for (auto& m : missing) {
            std::string msg = std::format("{} inclut <{}> ({}) sans dépendre de {}", m.target, m.header,
                                          util::display(m.file, ws->root), m.module);
            if (ctx.flag("--strict")) bad(msg);
            else note(msg);
        }
        if (!missing.empty() && ctx.flag("--fix")) {
            model::Transaction tx(ws->root);
            std::map<std::string, std::vector<std::string>> add;
            for (auto& m : missing) add[m.target].push_back(m.module);
            for (auto& [id, mods] : add) {
                auto* t = ws->find(id);
                auto& d = tx.doc(t->file);
                auto list = model::get_list(d, {"dependencies"}, "modules");
                for (auto& m : mods) util::push_unique(list, m);
                d.set({"dependencies"}, "modules", toml::Value(list));
                term::outln("  {} {:<20} ← {}", term::green("+"), id, util::join(mods, ", "));
            }
            try {
                tx.commit();
                term::ok("{} fichier(s) corrigé(s)", add.size());
                if (ctx.flag("--strict")) problems -= static_cast<int>(missing.size());
            } catch (const util::Error& e) {
                bad(std::format("correction annulée : {}", e.what()));
            }
        } else if (!missing.empty()) {
            term::outln("  {}", term::dim("cpp-maker doctor --fix pour ajouter ces dépendances"));
        }

        // --- Règles pre-commit : références mortes, crochet.
        auto rules = vcs::all_rules(*ws);
        if (!rules.empty()) {
            term::outln("\n{}", term::bold("Règles pre-commit"));
            int stale = 0;
            for (auto& r : rules) {
                auto issues = vcs::stale_rule_refs(*ws, *r.rule);
                if (issues.empty()) good(std::format("{} ({}){}", r.rule->name, r.scope, r.rule->enabled ? "" : " — désactivée"));
                for (auto& i : issues) {
                    bad(std::format("{} ({}) : {}", r.rule->name, r.scope, i));
                    ++stale;
                }
            }
            if (stale > 0 && ctx.flag("--fix")) {
                try {
                    model::Transaction tx(ws->root);
                    auto report = vcs::update_rules(tx, *ws, vcs::stale_items_map(*ws), vcs::stale_paths_map(*ws), false);
                    tx.commit();
                    for (auto& m : report) term::outln("  {} {}", term::green("↻"), m);
                    problems -= stale;
                } catch (const util::Error& e) {
                    bad(std::format("correction annulée : {}", e.what()));
                }
            } else if (stale > 0) {
                term::outln("  {}", term::dim("cpp-maker doctor --fix pour retirer les références mortes"));
            }
            if (vcs::toplevel(ws->root)) {
                auto state = vcs::hook_state(ws->root);
                if (state == vcs::HookState::Ours) good("crochet pre-commit installé");
                else note(state == vcs::HookState::Foreign
                              ? "un crochet pre-commit tiers est installé : ces règles ne s'exécutent pas (cpp-maker git hook install --force)"
                              : "crochet pre-commit absent : cpp-maker git hook install");
            }
        }
    }
    term::outln();
    if (problems == 0) term::ok("aucun problème détecté");
    else term::errln("{}", term::red(std::format("✗ {} problème(s)", problems)));
    return problems == 0 ? 0 : 1;
}

// ============================================================================
// tui / help / version
// ============================================================================

void TuiCommand::ConfigureArgs(args::Arguments& a) {
    a.add("--theme").set_value(std::string()).metavar("NOM").help("Thème de couleurs (voir --themes)");
    a.add("--css").set_value(std::string()).metavar("FICHIER").help("Feuille de style TCSS supplémentaire (rechargée à chaud)");
    a.add("--themes").set_value(false).help("Lister les thèmes disponibles");
}

int TuiCommand::Run(Context& ctx) {
    ctx.expect_max_positionals(0);
    if (ctx.flag("--themes")) {
        for (auto& n : tui::themes::names()) {
            const auto* t = tui::themes::find(n);
            term::outln("{}  {}", n, term::dim(t && t->dark ? "sombre" : "clair"));
        }
        return 0;
    }
    ui::TuiOptions opts;
    opts.theme = ctx.get<std::string>("--theme");
    if (!opts.theme.empty() && !tui::themes::find(opts.theme))
        ctx.usage("thème inconnu « " + opts.theme + " » (cpp-maker tui --themes)");
    if (auto css = ctx.get<std::string>("--css"); !css.empty()) {
        opts.css = fs::absolute(css);
        if (!fs::exists(opts.css)) ctx.usage("feuille de style introuvable : " + css);
    }
    return ui::run_tui(ctx.cwd(), opts);
}

int HelpCommand::Run(Context& ctx) {
    auto pos = ctx.positionals();
    auto& g = GlobalContext::Instance();
    if (pos.empty()) {
        g.PrintRootHelp();
        return 0;
    }
    const std::string& topic = pos[0];
    if (topic == "files" || topic == "fichiers") {
        term::outln("{}", term::bold("Arborescence d'un projet"));
        term::write_out(R"(
  <racine>/
  ├── project.toml                    projet : [project] [paths] [layers] [build] [build.modes.*]
  │                                   [toolchains.*] [tests] [package] [deploy.*] [generate.*]
  ├── libs/<layer>/
  │   ├── layer.toml                  [layer] [dependencies] [flags] [forbid] — hérités par les modules
  │   └── <module>/
  │       ├── module.toml             [module] type = static|shared|header-only, [sources] [include]
  │       ├── include/<module>/…      en-têtes publics (#include "<module>/<module>.hpp")
  │       ├── src/…                   sources
  │       └── tests/…                 tests (#include <cpp-maker/test.hpp>)
  └── apps/<application>/
      ├── application.toml            [application] [run] [dependencies]…
      └── src/main.cpp

  Identifiants : un module est « <layer>.<module> », une application « <nom> ».
  Artefacts : build/<toolchain>/<mode>/{bin,lib,tests}/ ; paquets : dist/.
  Toutes les clés : cpp-maker config keys project|layer|module|app
)");
        return 0;
    }
    if (topic == "modes") {
        term::outln("{}", term::bold("Modes de compilation prédéfinis"));
        for (auto& m : model::builtin_modes())
            term::outln("  {:<16} {:<44} {}", term::cyan(m.name), util::join(m.flags, " "),
                        m.runner.empty() ? util::join(m.defines, " ") : "lanceur " + m.runner);
        term::outln("\n  Mode personnalisé : [build.modes.<nom>] base, flags, link_flags, defines, sanitizer, runner.");
        term::outln("  Sanitizers à la volée : --sanitize address,undefined → mode dérivé « debug+address+undefined ».");
        return 0;
    }
    if (topic == "sanitize" || topic == "sanitizers") {
        term::outln("{}", term::bold("Sanitizers (--sanitize, champ sanitizer d'un mode)"));
        for (auto& s : sanitize::catalog())
            term::outln("  {:<26} {}\n  {:<26} {}", term::cyan(std::string(s.name)), s.family, "", term::dim(std::string(s.detail)));
        term::outln("\n  Un seul runtime parmi address, hwaddress, thread, memory, safe-stack ; leak seulement avec address.");
        term::outln("  --sanitize all : une passe par runtime (address+undefined, thread, leak, memory, hwaddress).");
        return 0;
    }
    if (topic == "selectors" || topic == "cibles") {
        term::outln("{}", term::bold("Désigner des cibles (build, test, info, deps…)"));
        term::write_out(R"(
  hello          application « hello » (prioritaire)
  core.log       module « log » du layer « core »
  log            module « log » s'il est unique
  core           tous les modules du layer « core » (si aucun module ne s'appelle ainsi)
  core.*         tous les modules du layer « core »
  all            toutes les cibles du projet
  (rien)         la cible du dossier courant, sinon tout le projet

  Les dépendances déclarées (dependencies.modules) acceptent les mêmes formes, sauf
  les applications qui ne peuvent pas être des dépendances.
)");
        return 0;
    }
    size_t consumed = 0;
    std::string path;
    auto cmd = g.ResolvePath(pos, consumed, path);
    if (!cmd) {
        std::vector<std::string> names = {"files", "modes", "sanitize", "selectors"};
        for (auto& c : g.All()) names.push_back(c->GetName());
        auto s = util::suggest(topic, names);
        util::fail("aucune aide pour '{}'{}", topic, s.empty() ? "" : " — vouliez-vous dire '" + s + "' ?");
    }
    g.PrintCommandHelp(*cmd, path);
    return 0;
}

int VersionCommand::Run(Context& ctx) {
    ctx.expect_max_positionals(0);
    term::outln("{} {}", cppmaker::kProgram, cppmaker::kVersion);
    return 0;
}

} // namespace commands
