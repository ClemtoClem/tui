/* @file commands/Options.cpp */

#include "commands/Options.hpp"

#include "core/Term.hpp"
#include "core/Util.hpp"

namespace commands {

void add_build_options(args::Arguments& a, unsigned which) {
    a.add("--mode").set_value(std::vector<std::string>{}).metavar("MODE[,…]")
        .help("Mode(s) de compilation (aussi en positionnel : release, debug,asan…)");
    if (which & kOptTargets)
        a.add("--target").set_value(std::vector<std::string>{}).flags("-m").metavar("CIBLE[,…]")
            .help("Cible(s) : layer.module, module, layer, application, all");
    if (which & kOptToolchain) {
        a.add("--toolchain").set_value(std::vector<std::string>{}).flags("-t").metavar("TC[,…]")
            .help("Toolchain(s) : host, gcc, clang, <triplet>, nom du projet, all");
        a.add("--cc").set_value(std::string()).metavar("CHEMIN").help("Compilateur C imposé");
        a.add("--cxx").set_value(std::string()).metavar("CHEMIN").help("Compilateur C++ imposé");
    }
    if (which & kOptJobs) {
        a.add("--jobs").set_value(0).flags("-j").metavar("N").help("Tâches parallèles (0 = cœurs - 1)");
        a.add("--keep-going").set_value(false).flags("-k").help("Continuer après un échec");
    }
    if (which & kOptVerbose) {
        a.add("--verbose").set_value(false).flags("-v").help("Afficher les commandes exécutées");
        a.add("--dry-run").set_value(false).flags("-n").help("Simuler : afficher sans exécuter");
    }
    if (which & kOptClean) a.add("--clean").set_value(false).help("Supprimer le dossier de build du mode avant");
    if (which & kOptTests) a.add("--tests").set_value(false).help("Compiler aussi les exécutables de test");
    if (which & kOptBackend) a.add("--backend").set_value(std::string("native")).metavar("native|ninja")
        .help("Moteur d'exécution");
    if (which & kOptApproval) {
        a.add("--require-approval").set_value(false).help("Bloquer les cibles non approuvées ([review].status)");
        a.add("--ignore-approval").set_value(false).help("Passer outre l'approbation requise");
    }
    if (which & kOptSanitize) {
        a.add("--sanitize").set_value(std::vector<std::string>{}).flags("--sanitizer").metavar("LISTE")
            .help("Ajouter des sanitizers (address,undefined… ou all : une passe par runtime)");
        a.add("--sanitize-trap").set_value(std::vector<std::string>{}).metavar("LISTE")
            .help("-fsanitize-trap=… (instruction trap au lieu d'un rapport)");
        a.add("--no-sanitize-recover").set_value(std::vector<std::string>{}).metavar("LISTE")
            .help("-fno-sanitize-recover=… (arrêt à la première erreur)");
        a.add("--sanitize-ignorelist").set_value(std::string()).metavar("FICHIER")
            .help("-fsanitize-ignorelist=… (clang)");
    }
}

build::Request read_request(Context& ctx, bool positional_targets) {
    auto& ws = ctx.workspace();
    auto& a = ctx.args();
    build::Request r;
    auto add_modes = [&](const std::string& item) {
        for (auto& m : util::split_list(item)) util::push_unique(r.modes, m);
    };
    for (auto& m : a.get<std::vector<std::string>>("--mode")) add_modes(m);
    // Chaque mot est une liste « a,b » dont chaque élément est un mode ou une cible.
    for (auto& w : ctx.positionals()) {
        for (auto& part : util::split_list(w)) {
            bool is_mode = ws.project.find_mode(part) != nullptr;
            bool is_target = part == "all" || !ws.resolve_selector_noexcept(part).empty();
            if (!is_mode && !is_target) {
                std::vector<std::string> names;
                for (auto& m : ws.project.modes) names.push_back(m.name);
                if (positional_targets) {
                    for (auto& id : ws.ids()) names.push_back(id);
                    for (auto& L : ws.layers) names.push_back(L.name);
                }
                auto sg = util::suggest(part, names);
                util::fail("{} inconnu '{}'{}", positional_targets ? "mode ou cible" : "mode", part,
                           sg.empty() ? " (cpp-maker list ; cpp-maker help modes)" : " — vouliez-vous dire '" + sg + "' ?");
            }
            if (is_mode && !is_target) add_modes(part);
            else if (positional_targets) r.selectors.push_back(part);
            else ctx.usage("argument inattendu : " + part);
        }
    }
    if (a.has("--target"))
        for (auto& t : a.get<std::vector<std::string>>("--target")) r.selectors.push_back(t);
    if (a.has("--toolchain")) {
        r.toolchains = a.get<std::vector<std::string>>("--toolchain");
        r.cc = a.get<std::string>("--cc");
        r.cxx = a.get<std::string>("--cxx");
    }
    if (a.has("--jobs")) {
        r.jobs = a.get<int>("--jobs");
        if (r.jobs < 0) ctx.usage("--jobs doit être ≥ 0");
    }
    if (a.has("--keep-going")) r.keep_going = a.get<bool>("--keep-going");
    if (a.has("--verbose")) r.verbose = a.get<bool>("--verbose");
    if (a.has("--dry-run")) r.dry_run = a.get<bool>("--dry-run");
    if (a.has("--clean")) r.clean = a.get<bool>("--clean");
    if (a.has("--tests")) r.tests = a.get<bool>("--tests");
    if (a.has("--backend")) {
        r.backend = a.get<std::string>("--backend");
        if (r.backend != "native" && r.backend != "ninja") ctx.usage("--backend : native ou ninja");
    }
    if (a.has("--require-approval")) r.require_approval = a.get<bool>("--require-approval");
    if (a.has("--ignore-approval")) r.ignore_approval = a.get<bool>("--ignore-approval");
    if (a.has("--sanitize")) {
        r.san.sanitize = a.get<std::vector<std::string>>("--sanitize");
        r.san.trap = a.get<std::vector<std::string>>("--sanitize-trap");
        r.san.no_recover = a.get<std::vector<std::string>>("--no-sanitize-recover");
        r.san.ignorelist = a.get<std::string>("--sanitize-ignorelist");
    }
    (void)build::pick_modes(ws, r);   // vérifie les modes
    return r;
}

std::vector<std::string> selected_targets(Context& ctx, const build::Request& r) {
    auto& ws = ctx.workspace();
    auto ids = ws.select(r.selectors, ctx.cwd());
    if (r.selectors.empty()) {
        auto here = ws.targets_at(ctx.cwd());
        if (!here.empty())
            term::outln("{}", term::dim(std::format("(cible du dossier courant : {} — « all » pour tout le projet)",
                                                    util::join(here, ", "))));
    }
    if (ids.empty()) util::fail("aucune cible dans ce projet (cpp-maker add module|app …)");
    return ids;
}

void enforce_approval(const model::Workspace& ws, const std::vector<std::string>& ids, bool required, bool ignore) {
    if (!required || ignore) return;
    // Une cible est bloquée si elle-même OU l'une de ses dépendances n'est pas approuvée.
    std::vector<std::string> all;
    for (auto& id : ids) {
        util::push_unique(all, id);
        for (auto& d : ws.closure(id)) util::push_unique(all, d);
    }
    std::vector<std::string> pending, pending_ids;
    for (auto& id : all) {
        const auto* t = ws.find(id);
        if (!t || t->from_git || t->review.is_approved()) continue;
        std::string state = t->review.status == "none" ? "non examinée" : t->review.status;
        pending.push_back(std::format("{} ({})", id, state));
        pending_ids.push_back(id);
    }
    if (pending.empty()) return;
    util::fail("{} cible(s) non approuvée(s) : {}\n  cpp-maker review approve {}   (ou --ignore-approval pour passer outre)",
               pending.size(), util::join(pending, ", "), util::join(pending_ids, " "));
}

} // namespace commands
