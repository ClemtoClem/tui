/* @file commands/GitCommands.cpp */

#include "commands/GitCommands.hpp"

#include "core/Process.hpp"
#include "core/Term.hpp"
#include "core/Util.hpp"
#include "model/Editor.hpp"
#include "model/Schema.hpp"
#include "vcs/Git.hpp"

#include <algorithm>
#include <map>

namespace commands {

namespace {

const model::LoadOptions kLight{.resolve_git = false, .update_git = false, .validate = false};

/// Dossiers désignés par des cibles (« project » = racine, layer = dossier du layer).
std::vector<fs::path> target_dirs(const model::Workspace& ws, const std::vector<std::string>& sels) {
    std::vector<fs::path> r;
    for (auto& s : sels) {
        if (s == "project" || s == "projet" || s == "all") {
            r.push_back(ws.root);
            continue;
        }
        if (auto* L = ws.find_layer(s); L && !ws.find(s)) {
            r.push_back(ws.root / L->dir);
            continue;
        }
        for (auto& id : ws.resolve_selector(s)) r.push_back(ws.abs(*ws.find(id)));
    }
    return r;
}

[[maybe_unused]] std::vector<std::string> rel_args(const model::Workspace& ws, const std::vector<fs::path>& dirs) {
    std::vector<std::string> r = {"--"};
    for (auto& d : dirs) r.push_back(util::display(d, ws.root));
    return r;
}

std::string status_label(const vcs::StatusEntry& e) {
    if (e.untracked()) return "nouveau (non suivi)";
    auto one = [](char c) -> std::string {
        switch (c) {
        case 'M': return "modifié";
        case 'A': return "ajouté";
        case 'D': return "supprimé";
        case 'R': return "renommé";
        case 'C': return "copié";
        case 'U': return "en conflit";
        case 'T': return "type changé";
        default: return {};
        }
    };
    std::string idx = one(e.index), wt = one(e.worktree);
    if (!idx.empty() && !wt.empty()) return idx + " (indexé) + " + wt;
    if (!idx.empty()) return idx + " (indexé)";
    return wt;
}

/// Fichier et scope d'une règle (--layer / --module / --app, défaut projet).
struct RuleFile {
    fs::path file;
    std::string label;
};

RuleFile rule_file(Context& ctx, const model::Workspace& ws) {
    auto layer = ctx.get<std::string>("--layer");
    auto module = ctx.get<std::string>("--module");
    auto app = ctx.get<std::string>("--app");
    if (!layer.empty() + !module.empty() + !app.empty() > 1) ctx.usage("--layer, --module et --app sont exclusifs");
    if (!layer.empty()) {
        auto* L = ws.find_layer(layer);
        if (!L) util::fail("layer inconnu '{}'", layer);
        return {L->file, "layer " + layer};
    }
    if (!module.empty()) {
        auto ids = ws.resolve_module_ref(module);
        if (ids.size() != 1) util::fail("'{}' désigne plusieurs modules : {}", module, util::join(ids, ", "));
        return {ws.find(ids[0])->file, "module " + ids[0]};
    }
    if (!app.empty()) {
        auto* t = ws.find(app);
        if (!t || !t->is_app()) util::fail("application inconnue '{}'", app);
        return {t->file, "application " + app};
    }
    return {ws.project.file, "projet"};
}

void add_scope_options(args::Arguments& a) {
    a.add("--layer").set_value(std::string()).flags("-L").metavar("NOM").help("Règle du layer (ses fichiers)");
    a.add("--module").set_value(std::string()).flags("-M").metavar("LAYER.MODULE").help("Règle du module (ses fichiers)");
    a.add("--app").set_value(std::string()).flags("-A").metavar("NOM").help("Règle de l'application (ses fichiers)");
}

} // namespace

// ============================================================================

std::string GitGroup::GetDetails() const {
    return "Les règles pre-commit se déclarent dans les fichiers toml (cpp-maker help git) :\n"
           "  [git.pre_commit.tests]\n"
           "  run = \"test {targets}\"        # arguments de cpp-maker (ou command = \"…\" pour le shell)\n"
           "  when = [\"libs/**\"]            # fichiers indexés concernés (défaut : toute la portée)\n"
           "  on_failure = \"block\"          # block | warn\n"
           "Portée : project.toml = tous les fichiers indexés ; layer.toml / module.toml /\n"
           "application.toml = ceux du layer ou de la cible. {targets} = cibles touchées.\n"
           "« cpp-maker git hook install » branche les règles sur git commit. Elles sont mises à\n"
           "jour par rename / remove, et vérifiées par doctor (doctor --fix les nettoie).";
}

std::string GitActionCommand::GetDescription() const {
    if (name_ == "status") return "État du dépôt, groupé par cible";
    if (name_ == "add") return "Indexer les fichiers de cibles (ou tout le projet)";
    if (name_ == "unstage") return "Désindexer les fichiers de cibles";
    if (name_ == "commit") return "Valider les fichiers indexés (déclenche le crochet pre-commit)";
    if (name_ == "log") return "Historique du projet ou d'une cible";
    if (name_ == "diff") return "Différences du projet ou d'une cible";
    if (name_ == "push") return "Envoyer les commits (git push)";
    if (name_ == "pull") return "Récupérer les commits (git pull)";
    if (name_ == "init") return "Initialiser le dépôt, .gitignore et le crochet pre-commit";
    return "Exécuter les règles pre-commit sur les fichiers indexés";
}

std::string GitActionCommand::GetUsage() const {
    if (name_ == "add" || name_ == "unstage") return "<cible…> | --all";
    if (name_ == "commit") return "-m <message> [-a] [--amend] [--no-verify]";
    if (name_ == "log") return "[<cible>] [-n N]";
    if (name_ == "diff") return "[<cible>] [--staged]";
    if (name_ == "push" || name_ == "pull") return "[-- arguments git]";
    if (name_ == "pre-commit") return "[--all] [-n] [-k] [fichier…]";
    return "";
}

std::string GitActionCommand::GetDetails() const {
    if (name_ == "pre-commit")
        return "Appelée par le crochet installé (cpp-maker git hook install). Chaque règle dont la portée\n"
               "contient un fichier indexé est exécutée, dans l'ordre : projet, layers, cibles.\n"
               "--all : tous les fichiers suivis (vérification complète) ; -n : affiche sans exécuter.";
    if (name_ == "add" || name_ == "unstage" || name_ == "log" || name_ == "diff")
        return "<cible> : module, application, layer, ou « project » (racine).";
    return {};
}

std::vector<std::string> GitActionCommand::GetExamples() const {
    if (name_ == "add") return {"cpp-maker git add core.log hello", "cpp-maker git add --all"};
    if (name_ == "commit") return {"cpp-maker git commit -m \"Nouveau module log\"", "cpp-maker git commit -a -m \"Corrections\""};
    if (name_ == "pre-commit") return {"cpp-maker git pre-commit -n", "cpp-maker git pre-commit --all"};
    if (name_ == "log") return {"cpp-maker git log core.log -n 5"};
    return {};
}

void GitActionCommand::ConfigureArgs(args::Arguments& a) {
    if (name_ == "add" || name_ == "unstage") a.add("--all").set_value(false).flags("-a").help("Tout le projet");
    if (name_ == "commit") {
        a.add("--message").set_value(std::string()).flags("-m").metavar("TEXTE").help("Message du commit").required();
        a.add("--all").set_value(false).flags("-a").help("Indexer d'abord tout le projet");
        a.add("--amend").set_value(false).help("Modifier le dernier commit");
        a.add("--no-verify").set_value(false).help("Sans crochet pre-commit");
    }
    if (name_ == "log") a.add("--count").set_value(20).flags("-n").metavar("N").help("Nombre de commits");
    if (name_ == "diff") a.add("--staged").set_value(false).flags("--cached").help("Changements indexés");
    if (name_ == "pre-commit") {
        a.add("--all").set_value(false).flags("-a").help("Tous les fichiers suivis au lieu des fichiers indexés");
        a.add("--dry-run").set_value(false).flags("-n").help("Afficher les règles sans les exécuter");
        a.add("--keep-going").set_value(false).flags("-k").help("Continuer après un échec bloquant");
    }
}

int GitActionCommand::Run(Context& ctx) {
    if (!util::have("git")) util::fail("git introuvable dans le PATH");
    if (name_ == "init") {
        ctx.expect_max_positionals(0);
        auto& ws = ctx.workspace(kLight);
        if (!vcs::toplevel(ws.root)) {
            if (vcs::git(ws.root, {"init", "-q"}) != 0) util::fail("git init a échoué");
            term::ok("dépôt git créé dans {}", ws.root.string());
        } else {
            term::info("dépôt git existant : {}", vcs::toplevel(ws.root)->string());
        }
        std::error_code ec;
        if (!fs::exists(ws.root / ".gitignore", ec)) {
            util::write_file(ws.root / ".gitignore", std::format("# cpp-maker\n/{}/\n/{}/\n/{}/\n/.cpp-maker.busy\n/.cpp-maker-trash/\n"
                                                                 "/compile_commands.json\n",
                                                                 ws.project.build_dir, ws.project.dist_dir, ws.project.out_dir));
            term::ok(".gitignore créé");
        }
        auto state = vcs::hook_state(ws.root);
        if (state == vcs::HookState::Foreign) term::warn("un crochet pre-commit existe déjà : cpp-maker git hook install --force pour le remplacer");
        else {
            vcs::install_hook(ws.root, util::self_exe(), false);
            term::ok("crochet pre-commit installé ({} règle(s))", vcs::all_rules(ws).size());
        }
        return 0;
    }
    auto& ws = ctx.workspace(kLight);
    auto top = vcs::toplevel(ws.root);
    if (!top) util::fail("{} n'est pas dans un dépôt git (cpp-maker git init)", ws.root.string());

    if (name_ == "status") {
        ctx.expect_max_positionals(0);
        auto st = vcs::status(ws.root);
        term::outln("{} {}{}{}", term::bold("Branche"), term::cyan(st.branch.empty() ? "(détachée)" : st.branch),
                    st.upstream.empty() ? "" : " → " + st.upstream,
                    st.ahead || st.behind ? std::format("  (+{} / -{})", st.ahead, st.behind) : "");
        if (st.entries.empty()) {
            term::ok("rien à valider, copie de travail propre");
            return 0;
        }
        std::map<std::string, std::vector<const vcs::StatusEntry*>> groups;
        std::vector<std::string> order;
        for (auto& e : st.entries) {
            auto owner = vcs::owner_of(ws, e.path);
            if (!groups.count(owner)) order.push_back(owner);
            groups[owner].push_back(&e);
        }
        int staged = 0;
        for (auto& owner : order) {
            std::string title = owner == "project" ? "projet" : owner.starts_with("layer:") ? "layer " + owner.substr(6) : owner;
            term::outln("\n{}", term::bold(title));
            for (auto* e : groups[owner]) {
                staged += e->staged();
                std::string mark = e->staged() ? term::green("●") : e->untracked() ? term::yellow("?") : term::red("○");
                term::outln("  {} {:<52} {}", mark, util::display(e->path, ws.root), term::dim(status_label(*e)));
            }
        }
        term::outln("\n{} fichier(s) dont {} indexé(s) · ● indexé  ○ modifié  ? non suivi", st.entries.size(), staged);
        auto rules = vcs::all_rules(ws);
        if (!rules.empty() && vcs::hook_state(ws.root) != vcs::HookState::Ours)
            term::warn("{} règle(s) pre-commit, mais le crochet n'est pas installé (cpp-maker git hook install)", rules.size());
        return 0;
    }
    if (name_ == "add" || name_ == "unstage") {
        auto pos = ctx.positionals();
        bool all = ctx.flag("--all");
        if (pos.empty() && !all) ctx.usage("cible(s) ou --all attendu");
        auto dirs = all ? std::vector<fs::path>{ws.root} : target_dirs(ws, pos);
        std::string err = name_ == "add" ? vcs::stage(ws.root, dirs) : vcs::unstage(ws.root, dirs);
        if (!err.empty()) util::fail("git : {}", err);
        std::vector<std::string> shown;
        for (auto& d : dirs) shown.push_back(d == ws.root ? std::string("tout le projet") : util::display(d, ws.root));
        term::ok("{} : {}", name_ == "add" ? "indexé" : "désindexé", util::join(shown, ", "));
        return 0;
    }
    if (name_ == "commit") {
        ctx.expect_max_positionals(0);
        if (ctx.flag("--all")) {
            int rc = vcs::git(ws.root, {"add", "-A", "--", "."});
            if (rc != 0) return rc;
        }
        std::vector<std::string> args = {"commit", "-m", ctx.get<std::string>("--message")};
        if (ctx.flag("--amend")) args.push_back("--amend");
        if (ctx.flag("--no-verify")) args.push_back("--no-verify");
        return vcs::git(ws.root, args);
    }
    if (name_ == "log" || name_ == "diff") {
        ctx.expect_max_positionals(1);
        std::vector<std::string> args;
        if (name_ == "log") args = {"log", "--oneline", "--decorate", "-n", std::to_string(ctx.get<int>("--count"))};
        else {
            args = {"diff", "--stat", "-p"};
            if (ctx.flag("--staged")) args.push_back("--cached");
        }
        auto pos = ctx.positionals();
        auto rel = rel_args(ws, pos.empty() ? std::vector<fs::path>{ws.root} : target_dirs(ws, pos));
        args.insert(args.end(), rel.begin(), rel.end());
        return vcs::git(ws.root, args);
    }
    if (name_ == "push" || name_ == "pull") {
        ctx.expect_max_positionals(0);
        std::vector<std::string> args = {name_};
        for (auto& t : ctx.args().trailing()) args.push_back(t);
        return vcs::git(ws.root, args);
    }
    // pre-commit
    vcs::PreCommitOptions o;
    o.all = ctx.flag("--all");
    o.dry_run = ctx.flag("--dry-run");
    o.keep_going = ctx.flag("--keep-going");
    o.files = ctx.positionals();
    o.self = util::self_exe();
    o.cwd = ctx.cwd();
    for (auto& e : ws.validate()) term::error("{}", e);
    if (!ws.validate().empty()) return 1;
    return vcs::run_pre_commit(ws, o);
}

// ============================================================================

std::string GitHookCommand::GetDescription() const {
    if (name_ == "install") return "Installer le crochet pre-commit (exécute cpp-maker git pre-commit)";
    if (name_ == "uninstall") return "Retirer le crochet (l'ancien crochet sauvegardé est restauré)";
    return "État du crochet et règles actives";
}

void GitHookCommand::ConfigureArgs(args::Arguments& a) {
    if (name_ == "install") a.add("--force").set_value(false).flags("-f").help("Remplacer un crochet existant (sauvegardé)");
}

int GitHookCommand::Run(Context& ctx) {
    ctx.expect_max_positionals(0);
    auto& ws = ctx.workspace(kLight);
    if (name_ == "install") {
        vcs::install_hook(ws.root, util::self_exe(), ctx.flag("--force"));
        term::ok("crochet installé : {}", vcs::hook_path(ws.root).string());
        term::outln("  {} règle(s) pre-commit · contournement ponctuel : git commit --no-verify", vcs::all_rules(ws).size());
        return 0;
    }
    if (name_ == "uninstall") {
        vcs::uninstall_hook(ws.root);
        term::ok("crochet cpp-maker retiré");
        return 0;
    }
    auto state = vcs::hook_state(ws.root);
    term::outln("{} {}", term::bold("Crochet"), vcs::hook_path(ws.root).string());
    term::outln("  {}", state == vcs::HookState::Ours      ? term::green("installé par cpp-maker")
                        : state == vcs::HookState::Foreign ? term::yellow("crochet tiers (non géré par cpp-maker)")
                                                           : term::red("absent"));
    auto rules = vcs::all_rules(ws);
    term::outln("  {} règle(s) pre-commit (cpp-maker git rule list)", rules.size());
    return 0;
}

// ============================================================================

std::string GitRuleCommand::GetDescription() const {
    if (name_ == "list") return "Lister les règles pre-commit (avec leur portée)";
    if (name_ == "add") return "Ajouter une règle dans project.toml, un layer.toml, module.toml ou application.toml";
    return "Supprimer une règle";
}

std::string GitRuleCommand::GetUsage() const {
    if (name_ == "list") return "";
    if (name_ == "add") return "<nom> (--run ARGS | --command CMD) [--when MOTIFS] [--env K=V,…] [--warn] [--layer L|--module M|--app A]";
    return "<nom> [--layer L|--module M|--app A]";
}

std::vector<std::string> GitRuleCommand::GetExamples() const {
    if (name_ == "add")
        return {"cpp-maker git rule add tests --run \"test {targets}\" --description \"Tests des cibles modifiées\"",
                "cpp-maker git rule add sanitizers --module infrastructure.vk --run \"check --modes asan,tsan {targets}\"",
                "cpp-maker git rule add possession --command tools/check_ownership.sh",
                "cpp-maker git rule add perf --command tools/perf_gate.sh --when \"libs/infrastructure/render3d/**,apps/voxelia/**\" --warn"};
    if (name_ == "remove") return {"cpp-maker git rule remove perf", "cpp-maker git rule remove sanitizers --module vk"};
    return {};
}

void GitRuleCommand::ConfigureArgs(args::Arguments& a) {
    if (name_ == "list") return;
    add_scope_options(a);
    if (name_ == "add") {
        a.add("--run").set_value(std::string()).metavar("ARGS").help("Arguments de cpp-maker ({targets}, {files}, {root})");
        a.add("--command").set_value(std::string()).flags("-c").metavar("CMD").help("Commande shell (depuis la racine)");
        a.add("--when").set_value(std::vector<std::string>{}).metavar("MOTIFS").help("Fichiers concernés (*, **, ?)");
        a.add("--env").set_value(std::vector<std::string>{}).metavar("K=V,…").help("Variables d'environnement");
        a.add("--description").set_value(std::string()).flags("-d").metavar("TEXTE").help("Description");
        a.add("--warn").set_value(false).help("Échec = avertissement (le commit n'est pas bloqué)");
        a.add("--disabled").set_value(false).help("Créer la règle désactivée");
    }
}

int GitRuleCommand::Run(Context& ctx) {
    auto& ws = ctx.workspace(kLight);
    if (name_ == "list") {
        ctx.expect_max_positionals(0);
        auto rules = vcs::all_rules(ws);
        if (rules.empty()) {
            term::outln("Aucune règle pre-commit. Exemple : cpp-maker git rule add tests --run \"test {{targets}}\"");
            return 0;
        }
        for (auto& r : rules) {
            auto& rule = *r.rule;
            term::outln("{} {}  {}{}", term::cyan(rule.name), term::dim("(" + r.scope + ")"),
                        rule.enabled ? "" : term::yellow("[désactivée] "), rule.on_failure == "warn" ? term::yellow("[avertissement] ") : "");
            if (!rule.description.empty()) term::outln("    {}", rule.description);
            term::outln("    {} {}", term::magenta("$"), rule.run.empty() ? rule.command : "cpp-maker " + rule.run);
            if (!rule.when.empty()) term::outln("    {}", term::dim("si : " + util::join(rule.when, ", ")));
            for (auto& s : vcs::stale_rule_refs(ws, rule)) term::warn("{} : {}", rule.name, s);
        }
        auto state = vcs::hook_state(ws.root);
        if (state != vcs::HookState::Ours) term::warn("crochet non installé : cpp-maker git hook install");
        return 0;
    }
    ctx.expect_max_positionals(1);
    std::string name = ctx.positional(0, "nom de la règle");
    if (!util::is_safe_name(name)) ctx.usage("nom de règle invalide '" + name + "'");
    auto rf = rule_file(ctx, ws);
    model::Transaction tx(ws.root);
    auto& d = tx.doc(rf.file);
    std::vector<std::string> table = {"git", "pre_commit", name};
    if (name_ == "remove") {
        if (!d.remove_table(table)) util::fail("aucune règle '{}' dans {} ({})", name, util::display(rf.file, ws.root), rf.label);
        tx.commit();
        term::ok("règle '{}' supprimée ({})", name, rf.label);
        return 0;
    }
    if (d.has_table(table)) util::fail("la règle '{}' existe déjà ({}) — cpp-maker config set … ou git rule remove", name, rf.label);
    auto run = ctx.get<std::string>("--run"), command = ctx.get<std::string>("--command");
    if (run.empty() == command.empty()) ctx.usage("--run ou --command (l'un des deux)");
    if (run.starts_with("cpp-maker ")) run = run.substr(10);
    if (auto desc = ctx.get<std::string>("--description"); !desc.empty()) d.set(table, "description", toml::Value(desc));
    if (!run.empty()) d.set(table, "run", toml::Value(run));
    else d.set(table, "command", toml::Value(command));
    if (auto w = ctx.get<std::vector<std::string>>("--when"); !w.empty()) d.set(table, "when", toml::Value(w));
    if (auto env = ctx.get<std::vector<std::string>>("--env"); !env.empty()) {
        toml::Value t = toml::Value::table();
        t.set_inline(true);
        for (auto& kv : env) {
            auto eq = kv.find('=');
            if (eq == std::string::npos) ctx.usage("--env : CLE=valeur attendu, reçu '" + kv + "'");
            t.set(kv.substr(0, eq), toml::Value(kv.substr(eq + 1)));
        }
        d.set(table, "env", t);
    }
    if (ctx.flag("--warn")) d.set(table, "on_failure", toml::Value("warn"));
    if (ctx.flag("--disabled")) d.set(table, "enabled", toml::Value(false));
    tx.commit();
    term::ok("règle '{}' ajoutée ({}) : {}", name, rf.label, util::display(rf.file, ws.root));
    if (vcs::toplevel(ws.root) && vcs::hook_state(ws.root) != vcs::HookState::Ours)
        term::outln("  {}", term::dim("crochet non installé : cpp-maker git hook install"));
    return 0;
}

} // namespace commands
