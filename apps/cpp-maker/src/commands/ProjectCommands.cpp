/* @file commands/ProjectCommands.cpp */

#include "commands/ProjectCommands.hpp"

#include "build/Planner.hpp"
#include "core/Term.hpp"
#include "core/Util.hpp"
#include "model/Editor.hpp"
#include "model/Scaffold.hpp"
#include "vcs/Git.hpp"

#include <algorithm>
#include <functional>
#include <set>

#include <unistd.h>

namespace commands {

namespace {

const model::LoadOptions kEditLoad{.resolve_git = false, .update_git = false, .validate = false};

void print_created(const std::vector<std::string>& lines) {
    for (auto& l : lines) term::outln("  {} {}", term::green("+"), l);
}

/// Une liste de références de modules à mettre à jour, dans un fichier.
struct RefList {
    fs::path file;
    std::vector<std::string> table;
    std::string key;
    std::string context_layer;
};

/// Toutes les listes de références de modules du projet.
std::vector<RefList> reference_lists(const model::Workspace& ws) {
    std::vector<RefList> r;
    r.push_back({ws.project.file, {"dependencies"}, "modules", ""});
    for (auto& L : ws.layers)
        if (L.has_file) r.push_back({L.file, {"dependencies"}, "modules", L.name});
    for (auto& t : ws.targets)
        if (!t.from_git) r.push_back({t.file, {"dependencies"}, "modules", t.layer});
    return r;
}

/// Applique `fn` à chaque référence ; `fn` renvoie la nouvelle valeur (vide = supprimer).
void rewrite_refs(model::Transaction& tx, const model::Workspace& ws,
                  const std::function<std::optional<std::string>(const std::string& ref, const std::string& ctx_layer)>& fn) {
    for (auto& rl : reference_lists(ws)) {
        auto& d = tx.doc(rl.file);
        auto refs = model::get_list(d, rl.table, rl.key);
        if (refs.empty()) continue;
        std::vector<std::string> out;
        bool changed = false;
        for (auto& ref : refs) {
            auto nv = fn(ref, rl.context_layer);
            if (!nv) {
                out.push_back(ref);
                continue;
            }
            changed = true;
            if (!nv->empty()) util::push_unique(out, *nv);
        }
        if (changed) d.set(rl.table, rl.key, toml::Value(out));
    }
}

/// Vrai si `ref` (dans le contexte de `ctx_layer`) désigne l'un des `ids`.
bool ref_targets(const model::Workspace& ws, const std::string& ref, const std::string& ctx_layer,
                 const std::set<std::string>& ids) {
    try {
        for (auto& id : ws.resolve_module_ref(ref, ctx_layer))
            if (ids.count(id)) return true;
    } catch (const util::Error&) {}
    return false;
}

void rewrite_deploy_targets(model::Transaction& tx, const model::Workspace& ws,
                            const std::function<std::optional<std::string>(const std::string&)>& fn) {
    auto& d = tx.doc(ws.project.file);
    for (auto& [name, dep] : ws.project.deploys) {
        auto refs = model::get_list(d, {"deploy", name}, "targets");
        std::vector<std::string> out;
        bool changed = false;
        for (auto& ref : refs) {
            auto nv = fn(ref);
            if (!nv) { out.push_back(ref); continue; }
            changed = true;
            if (!nv->empty()) out.push_back(*nv);
        }
        if (changed) d.set({"deploy", name}, "targets", toml::Value(out));
    }
}

/// Réécrit les chemins old/… en new/… (motifs when, chemins des commandes).
vcs::RefMap rename_paths(const std::string& old_rel, const std::string& new_rel) {
    return [old_rel, new_rel](const std::string& p) -> std::optional<std::string> {
        if (p == old_rel) return new_rel;
        if (p.starts_with(old_rel + "/")) return new_rel + p.substr(old_rel.size());
        return std::nullopt;
    };
}

void print_rule_report(const std::vector<std::string>& report) {
    for (auto& r : report) term::outln("  {} {}", term::cyan("↻"), r);
}

const model::TargetConfig& require_module(const model::Workspace& ws, const std::string& ref) {
    auto ids = ws.resolve_module_ref(ref);
    if (ids.size() != 1) util::fail("'{}' désigne {} modules ({}) : précisez <layer>.<module>", ref, ids.size(), util::join(ids, ", "));
    return *ws.find(ids[0]);
}

const model::TargetConfig& require_app(const model::Workspace& ws, const std::string& name) {
    auto* t = ws.find(name);
    if (!t || !t->is_app()) {
        std::vector<std::string> names;
        for (auto* a : ws.applications()) names.push_back(a->name);
        auto s = util::suggest(name, names);
        util::fail("application inconnue '{}'{}", name, s.empty() ? "" : " (vouliez-vous dire '" + s + "' ?)");
    }
    return *t;
}

const model::LayerConfig& require_layer(const model::Workspace& ws, const std::string& name) {
    auto* L = ws.find_layer(name);
    if (!L) {
        std::vector<std::string> names;
        for (auto& l : ws.layers) names.push_back(l.name);
        auto s = util::suggest(name, names);
        util::fail("layer inconnu '{}'{}", name, s.empty() ? "" : " (vouliez-vous dire '" + s + "' ?)");
    }
    return *L;
}

/// Marqueur d'approbation affiché à côté du nom d'une cible (vide si non examinée).
std::string review_mark(const model::TargetConfig& t) {
    if (!t.review.is_set()) return "";
    if (t.review.status == "approved") return term::green(" ✓");
    if (t.review.status == "rejected") return term::red(" ✗");
    return term::yellow(" ●");
}

std::string review_label(const model::TargetConfig& t) {
    if (!t.review.is_set()) return "non examinée";
    std::string s = t.review.status;
    if (!t.review.reviewer.empty()) s += " · " + t.review.reviewer;
    if (!t.review.date.empty()) s += " · " + t.review.date;
    if (!t.review.notes.empty()) s += " — " + t.review.notes;
    return s;
}

} // namespace

// ============================================================================
// new / init
// ============================================================================

std::string NewCommand::GetDetails() const {
    return "Crée <nom>/ avec project.toml, libs/, apps/, .gitignore et README.md.\n"
           "Disposition (--layout) :\n"
           "  full   layer « core » + module <nom> + application <nom> qui l'utilise (défaut)\n"
           "  app    application seule\n"
           "  lib    layer + module seuls\n"
           "  empty  aucun layer ni application";
}

void NewCommand::ConfigureArgs(args::Arguments& a) {
    a.add("--lang").set_value(std::string("c++")).metavar("c|c++").help("Langage principal");
    a.add("--std").set_value(std::string("c++23")).metavar("STD").help("Standard C++");
    a.add("--c-std").set_value(std::string("c17")).metavar("STD").help("Standard C");
    a.add("--layout").set_value(std::string("full")).metavar("full|app|lib|empty").help("Contenu initial");
    a.add("--layer").set_value(std::string("core")).metavar("NOM").help("Nom du layer initial");
    a.add("--module").set_value(std::string()).metavar("NOM").help("Nom du module initial (défaut : nom du projet)");
    a.add("--app").set_value(std::string()).metavar("NOM").help("Nom de l'application (défaut : nom du projet)");
    a.add("--dir").set_value(std::string()).metavar("DOSSIER").help("Dossier à créer (défaut : ./<nom>)");
}

int NewCommand::Run(Context& ctx) {
    ctx.expect_max_positionals(1);
    scaffold::ProjectOptions o;
    o.name = ctx.positional(0, "nom du projet");
    o.language = ctx.get<std::string>("--lang");
    o.cxx_standard = ctx.get<std::string>("--std");
    o.c_standard = ctx.get<std::string>("--c-std");
    o.layout = ctx.get<std::string>("--layout");
    o.layer = ctx.get<std::string>("--layer");
    o.module = ctx.get<std::string>("--module");
    o.app = ctx.get<std::string>("--app");
    if (auto d = ctx.get<std::string>("--dir"); !d.empty()) o.dir = d;
    if (!util::contains(model::cxx_standards(), o.cxx_standard)) ctx.usage("--std : standard C++ inconnu '" + o.cxx_standard + "'");
    if (!util::contains(model::c_standards(), o.c_standard)) ctx.usage("--c-std : standard C inconnu '" + o.c_standard + "'");
    fs::path root = scaffold::create_project(o);
    auto ws = model::Workspace::load_root(root, kEditLoad);
    term::ok("projet '{}' créé dans {}", o.name, util::display(root, ctx.cwd()));
    std::vector<std::string> lines = {"project.toml"};
    for (auto& L : ws.layers) lines.push_back(util::display(L.file, root));
    for (auto& t : ws.targets) lines.push_back(util::display(t.file, root));
    print_created(lines);
    term::outln("\n  cd {}", util::display(root, ctx.cwd()));
    term::outln("  cpp-maker build              # compile (debug)");
    term::outln("  cpp-maker run                # compile et exécute");
    term::outln("  cpp-maker test               # compile et lance les tests");
    term::outln("  cpp-maker tui                # interface interactive");
    return 0;
}

void InitCommand::ConfigureArgs(args::Arguments& a) {
    a.add("--name").set_value(std::string()).metavar("NOM").help("Nom du projet (défaut : nom du dossier)");
    a.add("--lang").set_value(std::string("c++")).metavar("c|c++").help("Langage principal");
    a.add("--std").set_value(std::string("c++23")).metavar("STD").help("Standard C++");
    a.add("--c-std").set_value(std::string("c17")).metavar("STD").help("Standard C");
}

int InitCommand::Run(Context& ctx) {
    ctx.expect_max_positionals(0);
    scaffold::ProjectOptions o;
    o.name = ctx.get<std::string>("--name");
    if (o.name.empty()) o.name = fs::absolute(ctx.cwd()).filename().string();
    o.dir = ctx.cwd();
    o.in_place = true;
    o.layout = "empty";
    o.language = ctx.get<std::string>("--lang");
    o.cxx_standard = ctx.get<std::string>("--std");
    o.c_standard = ctx.get<std::string>("--c-std");
    scaffold::create_project(o);
    term::ok("project.toml créé dans {}", ctx.cwd().string());
    term::outln("  cpp-maker add layer <nom>      puis    cpp-maker add module <layer> <nom>");
    term::outln("  cpp-maker add app <nom>");
    return 0;
}

// ============================================================================
// add
// ============================================================================

void AddLayerCommand::ConfigureArgs(args::Arguments& a) {
    a.add("--description").set_value(std::string()).metavar("TEXTE").help("Description");
    a.add("--deps").set_value(std::vector<std::string>{}).metavar("MODULES").help("Dépendances héritées par les modules");
    a.add("--dir").set_value(std::string()).metavar("DOSSIER").help("Dossier de layers (défaut : le premier de [paths].layers)");
}

int AddLayerCommand::Run(Context& ctx) {
    ctx.expect_max_positionals(1);
    auto& ws = ctx.workspace(kEditLoad);
    scaffold::LayerOptions o{ctx.positional(0, "nom du layer"), ctx.get<std::string>("--description"),
                             ctx.get<std::vector<std::string>>("--deps"), ctx.get<std::string>("--dir")};
    model::Transaction tx(ws.root);
    scaffold::add_layer(tx, ws, o);
    auto nw = tx.commit();
    auto* L = nw.find_layer(o.name);
    term::ok("layer '{}' créé", o.name);
    print_created({util::display(L->file, ws.root)});
    term::outln("  {}", term::dim("cpp-maker add module " + o.name + " <nom>"));
    return 0;
}

void AddModuleCommand::ConfigureArgs(args::Arguments& a) {
    a.add("--type").set_value(std::string("static")).metavar("static|shared|header-only").help("Type de bibliothèque");
    a.add("--lang").set_value(std::string()).metavar("c|c++").help("Langage (défaut : celui du projet)");
    a.add("--deps").set_value(std::vector<std::string>{}).metavar("MODULES").help("Dépendances (layer.module, module, layer.*)");
    a.add("--description").set_value(std::string()).metavar("TEXTE").help("Description");
    a.add("--no-sources").set_value(false).help("Ne pas générer de sources d'exemple");
}

int AddModuleCommand::Run(Context& ctx) {
    auto pos = ctx.positionals();
    std::string layer, name;
    if (pos.size() == 1 && pos[0].find('.') != std::string::npos) {
        layer = pos[0].substr(0, pos[0].find('.'));
        name = pos[0].substr(pos[0].find('.') + 1);
    } else if (pos.size() == 2) {
        layer = pos[0];
        name = pos[1];
    } else {
        ctx.usage(pos.empty() ? "layer et nom du module manquants" : "arguments attendus : <layer> <nom>");
    }
    auto* ws = &ctx.workspace(kEditLoad);
    std::optional<model::Workspace> fresh;
    if (!ws->find_layer(layer)) {
        model::Transaction tx(ws->root);
        scaffold::add_layer(tx, *ws, {layer, "", {}, ""});
        fresh = tx.commit(false);
        ws = &*fresh;
        term::ok("layer '{}' créé", layer);
    }
    scaffold::ModuleOptions o;
    o.layer = layer;
    o.name = name;
    o.type = ctx.get<std::string>("--type");
    o.language = ctx.get<std::string>("--lang");
    o.deps = ctx.get<std::vector<std::string>>("--deps");
    o.description = ctx.get<std::string>("--description");
    o.sources = !ctx.flag("--no-sources");
    model::Transaction tx(ws->root);
    scaffold::add_module(tx, *ws, o);
    auto nw = tx.commit();
    auto* t = nw.find(layer + "." + name);
    term::ok("module '{}' ({}) créé", t->id, t->type);
    std::vector<std::string> files;
    for (auto& e : fs::recursive_directory_iterator(nw.abs(*t)))
        if (e.is_regular_file()) files.push_back(util::display(e.path(), nw.root));
    std::sort(files.begin(), files.end());
    print_created(files);
    return 0;
}

void AddAppCommand::ConfigureArgs(args::Arguments& a) {
    a.add("--lang").set_value(std::string()).metavar("c|c++").help("Langage (défaut : celui du projet)");
    a.add("--deps").set_value(std::vector<std::string>{}).metavar("MODULES").help("Modules utilisés");
    a.add("--description").set_value(std::string()).metavar("TEXTE").help("Description");
    a.add("--no-sources").set_value(false).help("Ne pas générer de main d'exemple");
    a.add("--dir").set_value(std::string()).metavar("DOSSIER").help("Dossier d'applications (ex. examples ; défaut : le premier)");
}

int AddAppCommand::Run(Context& ctx) {
    ctx.expect_max_positionals(1);
    auto& ws = ctx.workspace(kEditLoad);
    scaffold::AppOptions o;
    o.name = ctx.positional(0, "nom de l'application");
    o.language = ctx.get<std::string>("--lang");
    o.deps = ctx.get<std::vector<std::string>>("--deps");
    o.description = ctx.get<std::string>("--description");
    o.sources = !ctx.flag("--no-sources");
    o.dir = ctx.get<std::string>("--dir");
    model::Transaction tx(ws.root);
    scaffold::add_app(tx, ws, o);
    auto nw = tx.commit();
    auto* t = nw.find(o.name);
    term::ok("application '{}' créée", o.name);
    std::vector<std::string> files;
    for (auto& e : fs::recursive_directory_iterator(nw.abs(*t)))
        if (e.is_regular_file()) files.push_back(util::display(e.path(), nw.root));
    std::sort(files.begin(), files.end());
    print_created(files);
    term::outln("  {}", term::dim("cpp-maker run " + o.name));
    return 0;
}

// ============================================================================
// remove
// ============================================================================

std::string RemoveTargetCommand::GetDescription() const {
    if (kind_ == "layer") return "Retirer un layer et tous ses modules";
    if (kind_ == "module") return "Retirer un module";
    return "Retirer une application";
}

void RemoveTargetCommand::ConfigureArgs(args::Arguments& a) {
    a.add("--purge").set_value(false).help("Supprimer définitivement (au lieu de la corbeille)");
    a.add("--force").set_value(false).flags("-f").help("Retirer aussi les références des dépendants");
    a.add("--yes").set_value(false).flags("-y").help("Ne pas demander de confirmation");
}

int RemoveTargetCommand::Run(Context& ctx) {
    ctx.expect_max_positionals(1);
    auto& ws = ctx.workspace(kEditLoad);
    std::string name = ctx.positional(0, "nom");
    std::set<std::string> ids;
    fs::path dir;
    std::string label;
    if (kind_ == "layer") {
        auto& L = require_layer(ws, name);
        for (auto* m : ws.modules_of(L.name)) ids.insert(m->id);
        dir = ws.root / L.dir;
        label = "layer '" + L.name + "'";
    } else if (kind_ == "module") {
        auto& t = require_module(ws, name);
        ids.insert(t.id);
        dir = ws.abs(t);
        label = "module '" + t.id + "'";
    } else {
        auto& t = require_app(ws, name);
        ids.insert(t.id);
        dir = ws.abs(t);
        label = "application '" + t.id + "'";
    }
    std::vector<std::string> users;
    for (auto& id : ids)
        for (auto& d : ws.dependents(id))
            if (!ids.count(d)) util::push_unique(users, d);
    if (!users.empty() && !ctx.flag("--force"))
        util::fail("{} est utilisé par : {} (--force pour retirer aussi ces références)", label, util::join(users, ", "));
    bool purge = ctx.flag("--purge");
    if (purge && !ctx.flag("--yes") && ::isatty(STDIN_FILENO) &&
        !term::confirm(std::format("Supprimer définitivement {} ?", util::display(dir, ws.root)), false))
        return 1;

    model::Transaction tx(ws.root);
    rewrite_refs(tx, ws, [&](const std::string& ref, const std::string& ctx_layer) -> std::optional<std::string> {
        if (ref.ends_with(".*") && kind_ != "layer") return std::nullopt;
        if (kind_ == "layer" && (ref == name + ".*" || ref == name)) return std::string();
        if (ref_targets(ws, ref, ctx_layer, ids)) return std::string();
        return std::nullopt;
    });
    rewrite_deploy_targets(tx, ws, [&](const std::string& ref) -> std::optional<std::string> {
        auto r = ws.resolve_selector_noexcept(ref);
        bool hit = !r.empty() && std::all_of(r.begin(), r.end(), [&](auto& x) { return ids.count(x) > 0; });
        return hit ? std::optional<std::string>(std::string()) : std::nullopt;
    });
    auto& pd = tx.doc(ws.project.file);
    if (kind_ == "layer") {
        auto order = model::get_list(pd, {"layers"}, "order");
        if (util::contains(order, name)) {
            std::erase(order, name);
            pd.set({"layers"}, "order", toml::Value(order));
        }
    } else if (kind_ == "module") {
        auto& t = *ws.find(*ids.begin());
        auto* L = ws.find_layer(t.layer);
        if (L && L->has_file && !L->modules.empty()) {
            auto& ld = tx.doc(L->file);
            auto mods = model::get_list(ld, {"layer"}, "modules");
            std::erase(mods, t.name);
            ld.set({"layer"}, "modules", toml::Value(mods));
        }
    }
    // Règles pre-commit : références et motifs de la cible retirés.
    const std::string removed_rel = dir.lexically_relative(ws.root).generic_string();
    auto rule_report = vcs::update_rules(
        tx, ws,
        [&](const std::string& item) -> std::optional<std::string> {
            auto r = ws.resolve_selector_noexcept(item);
            if (!r.empty() && std::all_of(r.begin(), r.end(), [&](auto& x) { return ids.count(x) > 0; })) return std::string();
            if (kind_ == "layer" && (item == name || item == name + ".*")) return std::string();
            return std::nullopt;
        },
        [&](const std::string& p) -> std::optional<std::string> {
            if (p == removed_rel || p.starts_with(removed_rel + "/")) return std::string();
            return std::nullopt;
        },
        true, dir);
    tx.remove(dir, purge);
    fs::path trash = tx.trashed();
    tx.commit();
    print_rule_report(rule_report);
    if (purge) term::ok("{} supprimé définitivement", label);
    else term::ok("{} retiré — fichiers déplacés dans {}", label, util::display(trash, ws.root));
    if (!users.empty()) term::outln("  références retirées de : {}", util::join(users, ", "));
    return 0;
}

// ============================================================================
// rename
// ============================================================================

std::string RenameTargetCommand::GetDescription() const {
    if (kind_ == "layer") return "Renommer un layer (et les identifiants de ses modules)";
    if (kind_ == "module") return "Renommer un module";
    return "Renommer une application";
}

int RenameTargetCommand::Run(Context& ctx) {
    ctx.expect_max_positionals(2);
    auto& ws = ctx.workspace(kEditLoad);
    std::string from = ctx.positional(0, "ancien nom");
    std::string to = ctx.positional(1, "nouveau nom");
    if (!util::is_safe_name(to) || to.find('.') != std::string::npos) ctx.usage("nouveau nom invalide '" + to + "'");
    model::Transaction tx(ws.root);

    if (kind_ == "module") {
        auto& t = require_module(ws, from);
        if (ws.find(t.layer + "." + to)) util::fail("le module '{}.{}' existe déjà", t.layer, to);
        std::string old_id = t.id, new_id = t.layer + "." + to, old_name = t.name;
        rewrite_refs(tx, ws, [&](const std::string& ref, const std::string& cl) -> std::optional<std::string> {
            if (ref == old_id) return new_id;
            if (ref == old_name && ref_targets(ws, ref, cl, {old_id})) return to;
            return std::nullopt;
        });
        rewrite_deploy_targets(tx, ws, [&](const std::string& ref) -> std::optional<std::string> {
            if (ref == old_id) return new_id;
            if (ref == old_name) return to;
            return std::nullopt;
        });
        if (auto* L = ws.find_layer(t.layer); L && L->has_file && !L->modules.empty()) {
            auto& ld = tx.doc(L->file);
            auto mods = model::get_list(ld, {"layer"}, "modules");
            std::replace(mods.begin(), mods.end(), old_name, to);
            ld.set({"layer"}, "modules", toml::Value(mods));
        }
        fs::path nd = ws.abs(t).parent_path() / to;
        auto rule_report = vcs::update_rules(
            tx, ws,
            [&](const std::string& item) -> std::optional<std::string> {
                if (item == old_id) return new_id;
                if (item == old_name && ref_targets(ws, item, "", {old_id})) return to;
                return std::nullopt;
            },
            rename_paths(t.dir.generic_string(), nd.lexically_relative(ws.root).generic_string()), false);
        tx.rename(ws.abs(t), nd);
        tx.doc(nd / model::kModuleFile).set({"module"}, "name", toml::Value(to));
        tx.commit();
        print_rule_report(rule_report);
        term::ok("module '{}' renommé en '{}'", old_id, new_id);
        if (fs::exists(nd / "include" / old_name))
            term::warn("les en-têtes restent dans include/{}/ ; adaptez les #include et l'espace de noms si besoin", old_name);
        return 0;
    }
    if (kind_ == "layer") {
        auto& L = require_layer(ws, from);
        if (ws.find_layer(to)) util::fail("le layer '{}' existe déjà", to);
        rewrite_refs(tx, ws, [&](const std::string& ref, const std::string&) -> std::optional<std::string> {
            if (ref == from || ref == from + ".*") return ref == from ? to : to + ".*";
            if (ref.starts_with(from + ".")) return to + ref.substr(from.size());
            return std::nullopt;
        });
        rewrite_deploy_targets(tx, ws, [&](const std::string& ref) -> std::optional<std::string> {
            if (ref == from) return to;
            if (ref.starts_with(from + ".")) return to + ref.substr(from.size());
            return std::nullopt;
        });
        auto& pd = tx.doc(ws.project.file);
        auto order = model::get_list(pd, {"layers"}, "order");
        if (util::contains(order, from)) {
            std::replace(order.begin(), order.end(), from, to);
            pd.set({"layers"}, "order", toml::Value(order));
        }
        fs::path nd = (ws.root / L.dir).parent_path() / to;
        auto rule_report = vcs::update_rules(
            tx, ws,
            [&](const std::string& item) -> std::optional<std::string> {
                if (item == from) return to;
                if (item.starts_with(from + ".")) return to + item.substr(from.size());
                return std::nullopt;
            },
            rename_paths(L.dir.generic_string(), nd.lexically_relative(ws.root).generic_string()), false);
        tx.rename(ws.root / L.dir, nd);
        if (fs::exists(nd / model::kLayerFile)) tx.doc(nd / model::kLayerFile).set({"layer"}, "name", toml::Value(to));
        tx.commit();
        term::ok("layer '{}' renommé en '{}'", from, to);
        print_rule_report(rule_report);
        return 0;
    }
    auto& t = require_app(ws, from);
    if (ws.find(to)) util::fail("'{}' existe déjà", to);
    rewrite_deploy_targets(tx, ws, [&](const std::string& ref) -> std::optional<std::string> {
        return ref == from ? std::optional<std::string>(to) : std::nullopt;
    });
    fs::path nd = ws.abs(t).parent_path() / to;
    auto rule_report = vcs::update_rules(
        tx, ws, [&](const std::string& item) -> std::optional<std::string> {
            return item == from ? std::optional<std::string>(to) : std::nullopt;
        },
        rename_paths(t.dir.generic_string(), nd.lexically_relative(ws.root).generic_string()), false);
    tx.rename(ws.abs(t), nd);
    tx.doc(nd / model::kApplicationFile).set({"application"}, "name", toml::Value(to));
    tx.commit();
    term::ok("application '{}' renommée en '{}'", from, to);
    print_rule_report(rule_report);
    return 0;
}

// ============================================================================
// list / info / graph
// ============================================================================

void ListCommand::ConfigureArgs(args::Arguments& a) {
    a.add("--names").set_value(false).help("Identifiants seuls, un par ligne (scripts)");
}

int ListCommand::Run(Context& ctx) {
    ctx.expect_max_positionals(1);
    auto& ws = ctx.workspace({.resolve_git = true, .update_git = false, .validate = false});
    std::string what = ctx.positional_or(0, "all");
    if (what != "all" && what != "layers" && what != "modules" && what != "apps")
        ctx.usage("filtre inconnu '" + what + "' (layers|modules|apps)");
    if (ctx.flag("--names")) {
        if (what == "layers")
            for (auto& L : ws.layers) term::outln("{}", L.name);
        for (auto& t : ws.targets)
            if (what == "all" || (what == "modules" && t.is_module()) || (what == "apps" && t.is_app()))
                term::outln("{}", t.id);
        return 0;
    }
    auto deps_of = [&](const model::TargetConfig& t) {
        std::vector<std::string> d = t.dep_ids;
        for (auto& p : t.all_pkg_config) d.push_back("pkg:" + p);
        for (auto& s : t.all_system_libs) d.push_back("-l" + s);
        return d.empty() ? std::string("—") : util::join(d, ", ");
    };
    term::outln("{} {} ({})", term::bold("Projet"), ws.project.name, ws.project.version);
    if (what == "all" || what == "layers" || what == "modules") {
        term::outln("\n{}", term::bold(std::format("Layers ({})", ws.layers.size())));
        if (ws.layers.empty()) term::outln("  (aucun — cpp-maker add layer <nom>)");
        for (auto& L : ws.layers) {
            int rank = ws.layer_rank(L.name);
            term::outln("  {}{}{}", term::cyan(L.name), rank >= 0 ? term::dim(std::format("  [couche {}]", rank)) : "",
                        L.description.empty() ? "" : "  " + term::dim(L.description));
            if (what == "layers") continue;
            auto mods = ws.modules_of(L.name);
            for (size_t i = 0; i < mods.size(); ++i) {
                auto* m = mods[i];
                term::outln("    {} {:<20} {:<12} {:<8} → {}{}", i + 1 == mods.size() ? "└─" : "├─", m->name, m->type,
                            m->version, deps_of(*m), review_mark(*m));
            }
        }
    }
    if (what == "all" || what == "apps") {
        auto apps = ws.applications();
        term::outln("\n{}", term::bold(std::format("Applications ({})", apps.size())));
        if (apps.empty()) term::outln("  (aucune — cpp-maker add app <nom>)");
        for (auto* a : apps) term::outln("  {:<24} {:<8} → {}{}", term::cyan(a->name), a->version, deps_of(*a), review_mark(*a));
    }
    for (auto& w : ws.warnings) term::warn("{}", w);
    return 0;
}

namespace {

void kv(const std::string& k, const std::string& v) {
    if (!v.empty()) term::outln("  {:<22} {}", k, v);
}

std::string list_or_dash(const std::vector<std::string>& v) { return v.empty() ? "—" : util::join(v, ", "); }

void print_target(const model::Workspace& ws, const model::TargetConfig& t) {
    term::outln("{} {}", term::bold(t.is_app() ? "Application" : "Module"), term::cyan(t.id));
    kv("dossier", util::display(ws.abs(t), ws.root));
    kv("fichier", util::display(t.file, ws.root));
    kv("type", t.type);
    kv("langage", t.language);
    kv("version", t.version);
    kv("description", t.description);
    kv("standard C++", t.cxx_standard.empty() ? ws.project.cxx_standard + " (projet)" : t.cxx_standard);
    kv("standard C", t.c_standard.empty() ? ws.project.c_standard + " (projet)" : t.c_standard);
    if (!t.is_header_only()) kv("artefact", t.artifact_base());
    kv("installé", t.install ? "oui" : "non");
    if (t.from_git) kv("origine", "dépendance git (lecture seule)");
    if (t.review.is_set()) kv("revue", review_label(t));
    term::outln("\n  {}", term::bold("Sources"));
    kv("dossiers", list_or_dash(t.source_dirs));
    if (!t.source_files.empty()) kv("fichiers", util::join(t.source_files, ", "));
    if (!t.exclude.empty()) kv("exclusions", util::join(t.exclude, ", "));
    auto srcs = build::discover_sources(ws, t, t.source_dirs);
    kv("découvertes", std::format("{} fichier(s)", srcs.size()));
    for (auto& s : srcs) term::outln("  {:<22} {}", "", term::dim(util::display(s.path, ws.root)));
    if (t.is_app()) kv("main", t.main.empty() ? "(détection automatique)" : t.main);
    if (t.is_module()) kv("en-têtes publics", list_or_dash(t.public_includes));
    kv("en-têtes privés", list_or_dash(t.private_includes));
    term::outln("\n  {}", term::bold("Dépendances"));
    kv("déclarées", list_or_dash(t.deps.modules));
    kv("résolues", list_or_dash(t.dep_ids));
    try {
        kv("fermeture", list_or_dash(ws.closure(t.id)));
    } catch (const util::Error& e) {
        kv("fermeture", e.what());
    }
    kv("pkg-config", list_or_dash(t.all_pkg_config));
    kv("système", list_or_dash(t.all_system_libs));
    for (auto& g : t.deps.git) kv("git " + g.alias, g.url + " @ " + g.version);
    if (t.is_module()) kv("utilisé par", list_or_dash(ws.dependents(t.id)));
    if (!t.flags.defines.empty() || !t.flags.cxxflags.empty() || !t.flags.cflags.empty() || !t.flags.ldflags.empty()) {
        term::outln("\n  {}", term::bold("Options"));
        kv("defines", util::join(t.flags.defines, " "));
        kv("defines publics", util::join(t.flags.public_defines, " "));
        kv("cflags", util::join(t.flags.cflags, " "));
        kv("cxxflags", util::join(t.flags.cxxflags, " "));
        kv("ldflags", util::join(t.flags.ldflags, " "));
    }
    term::outln("\n  {}", term::bold("Tests"));
    kv("dossiers", list_or_dash(t.test_dirs));
    kv("framework", t.test_framework.empty() ? ws.project.test_framework + " (projet)" : t.test_framework);
    auto tests = build::discover_sources(ws, t, t.test_dirs);
    kv("fichiers", std::format("{}", tests.size()));
    if (!t.platforms.empty()) {
        std::vector<std::string> p;
        for (auto& [k, v] : t.platforms) p.push_back(k);
        kv("plates-formes", util::join(p, ", "));
    }
    if (t.is_app() && (!t.run_args.empty() || !t.run_cwd.empty())) {
        term::outln("\n  {}", term::bold("Exécution"));
        kv("arguments", util::join(t.run_args, " "));
        kv("répertoire", t.run_cwd);
    }
}

void print_layer(const model::Workspace& ws, const model::LayerConfig& L) {
    term::outln("{} {}", term::bold("Layer"), term::cyan(L.name));
    kv("dossier", util::display(ws.root / L.dir, ws.root));
    kv("fichier", L.has_file ? util::display(L.file, ws.root) : "(aucun layer.toml)");
    kv("version", L.version);
    kv("description", L.description);
    int r = ws.layer_rank(L.name);
    kv("couche", r >= 0 ? std::to_string(r) : "non ordonnée");
    kv("modules", list_or_dash(L.discovered));
    kv("dépendances héritées", list_or_dash(L.deps.modules));
    kv("pkg-config hérités", list_or_dash(L.deps.pkg_config));
    kv("système hérités", list_or_dash(L.deps.system));
    kv("pkg-config interdits", list_or_dash(L.forbid_pkg_config));
}

} // namespace

int InfoCommand::Run(Context& ctx) {
    ctx.expect_max_positionals(1);
    auto& ws = ctx.workspace({.resolve_git = true, .update_git = false, .validate = false});
    if (auto sel = ctx.positional_or(0, ""); !sel.empty()) {
        if (auto* t = ws.find(sel)) {
            print_target(ws, *t);
            return 0;
        }
        if (auto* L = ws.find_layer(sel)) {
            print_layer(ws, *L);
            return 0;
        }
        auto ids = ws.resolve_selector(sel);
        for (size_t i = 0; i < ids.size(); ++i) {
            if (i) term::outln();
            print_target(ws, *ws.find(ids[i]));
        }
        return 0;
    }
    auto& p = ws.project;
    term::outln("{} {}", term::bold("Projet"), term::cyan(p.name));
    kv("racine", ws.root.string());
    kv("version", p.version);
    kv("description", p.description);
    kv("langage", p.language);
    kv("standards", p.cxx_standard + " / " + p.c_standard);
    kv("mode par défaut", p.default_mode);
    kv("toolchain par défaut", p.default_toolchain);
    kv("parallélisme", p.jobs == 0 ? "auto" : std::to_string(p.jobs));
    if (p.ccache) kv("ccache", "automatique si disponible");
    if (p.require_approval) kv("approbation", "requise pour build/test/package (cpp-maker review)");
    std::vector<std::string> modes;
    for (auto& m : p.modes) modes.push_back(m.builtin ? m.name : m.name + "*");
    kv("modes", util::join(modes, ", ") + "  (* = défini dans project.toml)");
    std::vector<std::string> tcs;
    for (auto& [n, t] : p.toolchains) tcs.push_back(n);
    kv("toolchains", list_or_dash(tcs));
    std::vector<std::string> layers;
    for (auto& L : ws.layers) layers.push_back(std::format("{} ({})", L.name, ws.modules_of(L.name).size()));
    kv("layers", list_or_dash(layers));
    kv("ordre des couches", list_or_dash(p.layer_order));
    std::vector<std::string> apps;
    for (auto* a : ws.applications()) apps.push_back(a->name);
    kv("applications", list_or_dash(apps));
    std::vector<std::string> deps;
    for (auto& [n, d] : p.deploys) deps.push_back(n);
    kv("déploiements", list_or_dash(deps));
    kv("formats de paquet", util::join(p.package.formats, ", "));
    kv("tests", std::format("{} · délai {} s", p.test_framework, p.test_timeout));
    try {
        auto tc = toolchain::resolve(p, p.default_toolchain);
        term::outln("\n  {} {}", term::bold("Toolchain"), tc.name);
        if (tc.cxx) kv("c++", tc.cxx->exe + " — " + tc.cxx->version);
        if (tc.cc) kv("c", tc.cc->exe + " — " + tc.cc->version);
        kv("cible", std::format("{} ({} / {}){}", tc.triple, tc.arch, tc.os, tc.cross ? ", croisée" : ""));
    } catch (const util::Error& e) {
        term::warn("{}", e.what());
    }
    auto errors = ws.validate();
    for (auto& w : ws.warnings) term::warn("{}", w);
    for (auto& e : errors) term::error("{}", e);
    return errors.empty() ? 0 : 1;
}

void GraphCommand::ConfigureArgs(args::Arguments& a) {
    a.add("--dot").set_value(false).help("Sortie Graphviz (digraph)");
    a.add("--all").set_value(false).flags("-a").help("Toutes les cibles comme racines");
}

int GraphCommand::Run(Context& ctx) {
    auto& ws = ctx.workspace({.resolve_git = true, .update_git = false, .validate = false});
    std::vector<std::string> roots;
    for (auto& s : ctx.positionals()) util::push_unique(roots, ws.resolve_selector(s));
    if (roots.empty()) {
        for (auto& t : ws.targets)
            if (ctx.flag("--all") || ws.dependents(t.id).empty()) roots.push_back(t.id);
    }
    if (ctx.flag("--dot")) {
        term::outln("digraph \"{}\" {{", ws.project.name);
        term::outln("  rankdir=LR;\n  node [shape=box, style=rounded, fontname=\"Helvetica\"];");
        for (auto& L : ws.layers) {
            term::outln("  subgraph \"cluster_{}\" {{\n    label=\"{}\";\n    style=dashed;", L.name, L.name);
            for (auto* m : ws.modules_of(L.name))
                term::outln("    \"{}\" [label=\"{}\\n{}\"];", m->id, m->name, m->type);
            term::outln("  }}");
        }
        for (auto* a : ws.applications()) term::outln("  \"{}\" [shape=component];", a->id);
        for (auto& t : ws.targets) {
            for (auto& d : t.dep_ids) term::outln("  \"{}\" -> \"{}\";", t.id, d);
            for (auto& p : t.deps.pkg_config) {
                term::outln("  \"pkg:{}\" [shape=ellipse, style=dashed];", p);
                term::outln("  \"{}\" -> \"pkg:{}\" [style=dashed];", t.id, p);
            }
        }
        term::outln("}}");
        return 0;
    }
    std::function<void(const std::string&, const std::string&, bool, int, std::set<std::string>&)> walk =
        [&](const std::string& id, const std::string& prefix, bool last, int depth, std::set<std::string>& path) {
            auto* t = ws.find(id);
            std::string label = t ? (t->is_app() ? term::cyan(id) : id) : id;
            std::string extra = t ? term::dim(" (" + t->type + ")") : "";
            if (depth == 0) term::outln("{}{}", label, extra);
            else term::outln("{}{} {}{}", prefix, last ? "└─" : "├─", label, extra);
            if (!t || depth > 32) return;
            if (path.count(id)) {
                term::outln("{}   {}", prefix, term::red("(cycle)"));
                return;
            }
            path.insert(id);
            std::string np = depth == 0 ? "" : prefix + (last ? "   " : "│  ");
            for (size_t i = 0; i < t->dep_ids.size(); ++i) walk(t->dep_ids[i], np, i + 1 == t->dep_ids.size(), depth + 1, path);
            path.erase(id);
        };
    for (auto& r : roots) {
        std::set<std::string> path;
        walk(r, "", true, 0, path);
    }
    return 0;
}

} // namespace commands
