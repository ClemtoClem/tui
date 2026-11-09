/* @file commands/DepsCommands.cpp */

#include "commands/DepsCommands.hpp"

#include "core/Process.hpp"
#include "core/Term.hpp"
#include "core/Util.hpp"
#include "model/Editor.hpp"
#include "model/GitDeps.hpp"

namespace commands {

namespace {

/// Fichier et contexte édités pour une cible de `deps add/remove`.
struct EditTarget {
    fs::path file;
    std::string label;
    std::string layer;   ///< contexte de résolution
};

EditTarget edit_target(const model::Workspace& ws, const std::string& name) {
    if (name == "project" || name == "projet") return {ws.project.file, "projet", ""};
    if (auto* t = ws.find(name); t && t->is_app()) return {t->file, "application " + t->id, ""};
    if (auto* L = ws.find_layer(name); L && !ws.find(name)) {
        bool module_named_so = false;
        for (auto& t : ws.targets)
            if (t.is_module() && t.name == name) module_named_so = true;
        if (!module_named_so) {
            if (!L->has_file) util::fail("le layer '{}' n'a pas de layer.toml", name);
            return {L->file, "layer " + L->name + " (hérité par ses modules)", L->name};
        }
    }
    auto ids = ws.resolve_module_ref(name);
    if (ids.size() != 1) util::fail("'{}' désigne plusieurs modules : {}", name, util::join(ids, ", "));
    auto* t = ws.find(ids[0]);
    if (t->from_git) util::fail("'{}' provient d'une dépendance git (non modifiable)", t->id);
    return {t->file, "module " + t->id, t->layer};
}

std::string classify(const model::Workspace& ws, const std::string& dep, const std::string& layer, bool m, bool p, bool s) {
    if (m) return "modules";
    if (p) return "pkg_config";
    if (s) return "system";
    if (!ws.resolve_module_ref_noexcept(dep, layer).empty()) return "modules";
    if (util::have("pkg-config") && proc::capture({"pkg-config", "--exists", dep}).exit_code == 0) return "pkg_config";
    return "system";
}

} // namespace

int DepsListCommand::Run(Context& ctx) {
    ctx.expect_max_positionals(1);
    auto& ws = ctx.workspace({.resolve_git = true, .update_git = false, .validate = false});
    std::vector<const model::TargetConfig*> targets;
    if (auto sel = ctx.positional_or(0, ""); !sel.empty())
        for (auto& id : ws.resolve_selector(sel)) targets.push_back(ws.find(id));
    else
        for (auto& t : ws.targets)
            if (!t.from_git) targets.push_back(&t);
    for (auto* t : targets) {
        term::outln("{} {}", term::cyan(t->id), term::dim("(" + t->type + ")"));
        term::outln("  modules     {}", t->dep_ids.empty() ? "—" : util::join(t->dep_ids, ", "));
        if (t->dep_ids != t->deps.modules && !t->deps.modules.empty())
            term::outln("  {}", term::dim("déclarés : " + util::join(t->deps.modules, ", ")));
        if (!t->all_pkg_config.empty()) term::outln("  pkg-config  {}", util::join(t->all_pkg_config, ", "));
        if (!t->all_system_libs.empty()) term::outln("  système     {}", util::join(t->all_system_libs, ", "));
        for (auto& g : t->deps.git) term::outln("  git         {} = {} @ {}", g.alias, g.url, g.version);
    }
    if (!ws.git_lock.empty()) {
        term::outln("\n{}", term::bold("Versions git verrouillées (cpp-maker.lock)"));
        for (auto& [url, commit] : ws.git_lock) term::outln("  {:<50} {}", url, commit.substr(0, 12));
    }
    return 0;
}

std::string DepsEditCommand::GetDetails() const {
    return "Type de dépendance, si aucune option n'est donnée :\n"
           "  1. module du projet (« layer.module », « module », « layer.* ») ;\n"
           "  2. paquet pkg-config si « pkg-config --exists » réussit ;\n"
           "  3. bibliothèque système sinon (« -lz » est normalisé en « z »).\n"
           "<cible> peut être un layer : ses dépendances sont héritées par tous ses modules,\n"
           "ou « project » : héritées par toutes les cibles.";
}

std::vector<std::string> DepsEditCommand::GetExamples() const {
    if (add_)
        return {"cpp-maker deps add hello core.log", "cpp-maker deps add core.net zlib --pkg-config",
                "cpp-maker deps add hello pthread m --system", "cpp-maker deps add ui core.*"};
    return {"cpp-maker deps remove hello core.log", "cpp-maker deps remove core.net zlib"};
}

void DepsEditCommand::ConfigureArgs(args::Arguments& a) {
    a.add("--module").set_value(false).help("Dépendance vers un module du projet");
    a.add("--pkg-config").set_value(false).flags("-p").help("Paquet pkg-config");
    a.add("--system").set_value(false).flags("-s").help("Bibliothèque système (-l)");
}

int DepsEditCommand::Run(Context& ctx) {
    auto pos = ctx.positionals();
    if (pos.size() < 2) ctx.usage("cible et dépendance(s) attendues");
    auto& ws = ctx.workspace({.resolve_git = false, .update_git = false, .validate = false});
    auto target = edit_target(ws, pos[0]);
    bool m = ctx.flag("--module"), p = ctx.flag("--pkg-config"), s = ctx.flag("--system");
    if (static_cast<int>(m) + static_cast<int>(p) + static_cast<int>(s) > 1)
        ctx.usage("--module, --pkg-config et --system sont exclusifs");
    model::Transaction tx(ws.root);
    auto& d = tx.doc(target.file);
    std::vector<std::string> report;
    for (size_t i = 1; i < pos.size(); ++i) {
        std::string dep = pos[i];
        if (dep.starts_with("-l")) dep = dep.substr(2);
        if (add_) {
            std::string key = classify(ws, dep, target.layer, m, p, s);
            if (key == "modules") (void)ws.resolve_module_ref(dep, target.layer);   // erreur claire si inconnu
            auto list = model::get_list(d, {"dependencies"}, key);
            if (!util::push_unique(list, dep)) {
                term::warn("'{}' figure déjà dans dependencies.{}", dep, key);
                continue;
            }
            d.set({"dependencies"}, key, toml::Value(list));
            report.push_back(std::format("+ {} ({})", dep, key));
        } else {
            bool found = false;
            for (const char* key : {"modules", "pkg_config", "system"}) {
                auto list = model::get_list(d, {"dependencies"}, key);
                if (!util::contains(list, dep)) continue;
                std::erase(list, dep);
                d.set({"dependencies"}, key, toml::Value(list));
                report.push_back(std::format("- {} ({})", dep, key));
                found = true;
            }
            if (!found) util::fail("'{}' n'est pas une dépendance déclarée de {}", dep, target.label);
        }
    }
    tx.commit();
    if (report.empty()) return 0;
    term::ok("{} : {}", target.label, util::display(target.file, ws.root));
    for (auto& r : report) term::outln("  {}", r);
    return 0;
}

int DepsFetchCommand::Run(Context& ctx) {
    ctx.expect_max_positionals(0);
    if (update_) {
        auto root = model::Workspace::find_root(ctx.cwd());
        if (!root) util::fail("aucun project.toml trouvé");
    }
    auto ws = model::Workspace::load(ctx.cwd(), {.resolve_git = true, .update_git = update_, .validate = true});
    if (ws.git_lock.empty()) term::outln("Aucune dépendance git déclarée ([dependencies.git.<alias>]).");
    else term::ok("{} dépendance(s) git résolue(s) — cpp-maker.lock à jour", ws.git_lock.size());
    return 0;
}

int DepsCleanCommand::Run(Context& ctx) {
    ctx.expect_max_positionals(0);
    auto root = gitdeps::cache_root();
    std::error_code ec;
    if (!fs::exists(root, ec)) {
        term::outln("Cache vide ({}).", root.string());
        return 0;
    }
    fs::remove_all(root, ec);
    if (ec) util::fail("suppression impossible : {} ({})", root.string(), ec.message());
    term::ok("cache supprimé : {}", root.string());
    return 0;
}

} // namespace commands
