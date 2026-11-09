/* @file runners/ProjectRunner.cpp */

#include "runners/ProjectRunner.hpp"
#include "project/PlanApplier.hpp"
#include "utils/Shell.hpp"

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>

namespace yocto {

namespace fs = std::filesystem;

namespace {

std::string trim_eol(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s;
}

} // namespace

// --- Aides exposées ----------------------------------------------------------

fs::path find_file_containing(const fs::path& root, const std::string& needle) {
    std::vector<fs::path> matches;
    std::error_code ec;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        const auto& e = *it;
        if (e.is_directory() && e.path().filename() == ".git") {
            it.disable_recursion_pending();
            continue;
        }
        std::error_code ec2;
        if (!e.is_regular_file(ec2) || e.file_size(ec2) > 4 * 1024 * 1024) continue;
        const std::string content = read_file(e.path());
        if (content.find('\0') != std::string::npos) continue;  // binaire
        if (content.find(needle) != std::string::npos) matches.push_back(e.path());
    }
    if (matches.empty()) return {};
    return *std::min_element(matches.begin(), matches.end());  // résultat déterministe
}

size_t replace_in_string(std::string& text, const std::string& search, const std::string& replace, bool all) {
    if (search.empty()) return 0;
    size_t n = 0;
    size_t pos = 0;
    while ((pos = text.find(search, pos)) != std::string::npos) {
        text.replace(pos, search.size(), replace);
        pos += replace.size();
        ++n;
        if (!all) break;
    }
    return n;
}

// --- Runner ------------------------------------------------------------------

ProjectRunner::ProjectRunner(YoctoConfig config, bool dry_run, PasswordHasher hasher)
    : config_(std::move(config)), dry_run_(dry_run), hasher_(std::move(hasher)) {
    config_.apply_defaults();
    steps_ = {
        {ProjectStepId::FetchLayers,    "Couches externes",       "git clone / checkout sous layers/"},
        {ProjectStepId::WriteLayer,     "Écriture du layer",      "recettes, bbappends, image"},
        {ProjectStepId::ConfigureBuild, "Configuration du build", "local.conf + bblayers.conf"},
        {ProjectStepId::CheckLayers,    "Vérification des couches", "bitbake-layers show-layers"},
        {ProjectStepId::ApplyPatches,   "Patchs devtool",         "modify -> commit -> finish"},
    };
}

void ProjectRunner::log(const std::string& line) {
    if (on_output_) on_output_(line);
}

void ProjectRunner::emit(ProjectStepId id, StepStatus s, const std::string& msg) {
    if (on_status_) on_status_(id, s, msg);
}

bool ProjectRunner::exec(const std::string& cmd, std::string& err) {
    log("$ " + cmd);
    if (dry_run_) {
        log("  [simulation] non exécuté");
        return true;
    }
    // stdin fermé (aucune invite ne doit bloquer ni lire le clavier du
    // TUI), stderr fusionné pour ne pas écrire par-dessus l'interface.
    auto r = run_shell("GIT_TERMINAL_PROMPT=0 " + cmd + " </dev/null 2>&1",
                       [this](const std::string& l) { log("  " + l); });
    if (r.exit_code != 0) {
        err = "commande échouée (code " + std::to_string(r.exit_code) + ") : " + cmd;
        return false;
    }
    return true;
}

bool ProjectRunner::exec_env(const std::string& cmd, std::string& err) {
    if (!dry_run_) {
        std::error_code ec;
        if (!fs::exists(config_.poky_dir() / "oe-init-build-env", ec)) {
            err = "poky introuvable dans " + config_.poky_dir().string() + " : lancez la préparation (onglet 1)";
            log("! " + err);
            return false;
        }
    }
    log("(env) " + cmd);
    const std::string script = "cd " + shell_quote(config_.poky_dir().string()) +
                               " && source oe-init-build-env " + shell_quote(config_.build_dir) +
                               " >/dev/null && " + cmd;
    if (dry_run_) {
        log("  [simulation] non exécuté");
        return true;
    }
    auto r = run_shell("bash -c " + shell_quote(script) + " </dev/null 2>&1",
                       [this](const std::string& l) { log("  " + l); });
    if (r.exit_code != 0) {
        err = "commande échouée (code " + std::to_string(r.exit_code) + ") : " + cmd;
        return false;
    }
    return true;
}

void ProjectRunner::run(const std::vector<ProjectStepId>& enabled) {
    stop_requested_ = false;
    bool failed = false;
    for (const auto& step : steps_) {
        const bool on = std::find(enabled.begin(), enabled.end(), step.id) != enabled.end();
        if (!on) {
            emit(step.id, StepStatus::Skipped, "non sélectionnée");
            continue;
        }
        if (failed || stop_requested_) {
            emit(step.id, StepStatus::Skipped, failed ? "étape précédente en échec" : "arrêt demandé");
            continue;
        }
        if (!run_step(step.id)) failed = true;
    }
}

bool ProjectRunner::run_step(ProjectStepId id) {
    std::string label = "?";
    for (const auto& s : steps_) {
        if (s.id == id) label = s.label;
    }
    emit(id, StepStatus::Running);
    log("");
    log("=== " + label + (dry_run_ ? " (simulation)" : "") + " ===");

    std::string msg;
    bool ok = false;
    switch (id) {
        case ProjectStepId::FetchLayers:    ok = step_fetch_layers(msg); break;
        case ProjectStepId::WriteLayer:     ok = step_write_layer(msg); break;
        case ProjectStepId::ConfigureBuild: ok = step_configure_build(msg); break;
        case ProjectStepId::CheckLayers:    ok = step_check_layers(msg); break;
        case ProjectStepId::ApplyPatches:   ok = step_apply_patches(msg); break;
    }
    if (!ok && !msg.empty()) log("! " + msg);
    emit(id, ok ? StepStatus::Done : StepStatus::Failed, msg);
    return ok;
}

// --- Étapes ------------------------------------------------------------------

bool ProjectRunner::step_fetch_layers(std::string& msg) {
    int count = 0;
    std::string err;
    for (const auto& l : config_.project.extra_layers) {
        if (!l.enabled || l.name.empty()) continue;
        const fs::path dir = config_.layers_dir() / l.name;
        const std::string branch = l.branch.empty() ? config_.poky_branch : l.branch;
        const std::string dq = shell_quote(dir.string());
        std::error_code ec;

        if (!fs::exists(dir / ".git", ec)) {
            if (fs::exists(dir, ec) && !dry_run_) {
                msg = dir.string() + " existe mais n'est pas un dépôt git";
                return false;
            }
            if (!exec("git clone -b " + shell_quote(branch) + " " + shell_quote(l.url) + " " + dq, err)) {
                msg = err;
                return false;
            }
        } else {
            log("  " + l.name + " déjà cloné : mise à jour des références");
            if (!exec("git -C " + dq + " fetch origin", err)) { msg = err; return false; }
            const std::string current = trim_eol(run_shell("git -C " + dq + " rev-parse --abbrev-ref HEAD 2>/dev/null").output);
            if (current != branch) {
                log("  branche courante « " + current + " » -> « " + branch + " »");
                if (!exec("git -C " + dq + " checkout " + shell_quote(branch), err)) { msg = err; return false; }
            }
        }

        if (!dry_run_) {
            std::istringstream subs(l.sublayers);
            std::string sub;
            bool any = false;
            while (subs >> sub) {
                any = true;
                if (!fs::exists(dir / sub / "conf" / "layer.conf", ec)) {
                    msg = "sous-couche introuvable : " + (dir / sub).string();
                    return false;
                }
            }
            if (!any && !fs::exists(dir / "conf" / "layer.conf", ec)) {
                msg = dir.string() + " ne contient pas conf/layer.conf : précisez les sous-couches";
                return false;
            }
        }
        ++count;
    }
    msg = count == 0 ? "aucune couche externe" : std::to_string(count) + " couche(s) prête(s)";
    return true;
}

bool ProjectRunner::step_write_layer(std::string& msg) {
    const auto plan = LayerGenerator(config_, hasher_).generate();
    for (const auto& i : plan.issues) {
        log(std::string(i.level == Issue::Level::Error ? "  ✗ " : "  ! ") + i.where + " : " + i.message);
    }
    if (has_errors(plan.issues)) {
        msg = "projet invalide : corrigez les erreurs signalées";
        return false;
    }
    log("  layer : " + plan.layer_dir.string());
    const auto rep = apply_layer(plan, dry_run_, [this](const std::string& l) { log(l); });
    for (const auto& e : rep.errors) log("  ✗ " + e);
    if (!rep.ok()) { msg = rep.errors.front(); return false; }
    msg = std::to_string(rep.written) + " écrit(s), " + std::to_string(rep.unchanged) + " identique(s), " +
          std::to_string(rep.removed) + " supprimé(s)";
    return true;
}

bool ProjectRunner::step_configure_build(std::string& msg) {
    const auto plan = LayerGenerator(config_, hasher_).generate();
    const auto rep = apply_build_conf(plan, dry_run_, [this](const std::string& l) { log(l); });
    for (const auto& e : rep.errors) log("  ✗ " + e);
    if (!rep.ok()) { msg = rep.errors.front(); return false; }
    msg = rep.written == 0 ? "déjà à jour" : std::to_string(rep.written) + " fichier(s) mis à jour";
    return true;
}

bool ProjectRunner::step_check_layers(std::string& msg) {
    std::string err;
    if (!exec_env("bitbake-layers show-layers", err)) { msg = err; return false; }
    msg = "couches valides";
    return true;
}

bool ProjectRunner::step_apply_patches(std::string& msg) {
    // Regroupe par recette : un seul devtool modify/finish par recette,
    // un commit (donc un fichier .patch) par modification.
    std::vector<std::string> order;
    std::vector<std::vector<const PatchSpec*>> groups;
    for (const auto& p : config_.project.patches) {
        if (!p.enabled || p.method != PatchMethod::Devtool) continue;
        auto it = std::find(order.begin(), order.end(), p.recipe);
        if (it == order.end()) {
            order.push_back(p.recipe);
            groups.push_back({&p});
        } else {
            groups[static_cast<size_t>(it - order.begin())].push_back(&p);
        }
    }
    if (order.empty()) { msg = "aucun patch devtool"; return true; }

    for (size_t i = 0; i < order.size(); ++i) {
        if (stop_requested_) { msg = "arrêt demandé"; return false; }
        if (!patch_recipe(order[i], groups[i], msg)) return false;
    }
    msg = std::to_string(order.size()) + " recette(s) patchée(s)";
    return true;
}

bool ProjectRunner::patch_recipe(const std::string& recipe, const std::vector<const PatchSpec*>& patches,
                                 std::string& msg) {
    const fs::path ws = config_.build_dir_path() / "workspace" / "sources" / recipe;
    const fs::path layer = config_.layers_dir() / config_.project.layer_name;
    const std::string rq = shell_quote(recipe);
    std::string err;
    log("");
    log("--- " + recipe + " ---");

    // 1) Repartir d'un état propre : workspace devtool et export précédent.
    if (!exec_env("devtool reset " + rq + " >/dev/null 2>&1 || true", err)) { msg = err; return false; }
    std::error_code ec;
    if (fs::exists(ws, ec)) {
        log("  suppression de l'ancien arbre devtool " + ws.string());
        if (!dry_run_) fs::remove_all(ws, ec);
    }
    // Les fichiers exportés par un précédent « devtool finish » (hors
    // manifeste yocto-tui) seraient appliqués deux fois : on les retire.
    const auto manifest = read_manifest(layer);
    const std::set<std::string> generated(manifest.begin(), manifest.end());
    for (fs::directory_iterator cat(layer, ec), end; !ec && cat != end; cat.increment(ec)) {
        if (!cat->is_directory() || cat->path().filename().string().rfind("recipes-", 0) != 0) continue;
        const fs::path rdir = cat->path() / recipe;
        if (!fs::is_directory(rdir, ec)) continue;
        std::vector<fs::path> stale;
        for (fs::recursive_directory_iterator f(rdir, ec), fend; !ec && f != fend; f.increment(ec)) {
            if (!f->is_regular_file()) continue;
            const std::string rel = fs::relative(f->path(), layer, ec).string();
            if (!generated.count(rel)) stale.push_back(f->path());
        }
        for (const auto& s : stale) {
            log("  retrait de l'export devtool précédent : " + fs::relative(s, layer, ec).string());
            if (!dry_run_) fs::remove(s, ec);
        }
    }

    // 2) Sources de la recette dans le workspace devtool.
    if (!exec_env("devtool modify " + rq, err)) { msg = err; return false; }

    // 3) Une modification = un commit = un patch.
    for (const PatchSpec* p : patches) {
        const std::string where = p->file.empty() ? std::string("(recherche dans les sources)") : p->file;
        log("  « " + p->name + " » : " + where);
        if (dry_run_) {
            log("  [simulation] remplacerait « " + p->search + " » par « " + p->replace + " »");
            log("  [simulation] git commit -m " + shell_quote(p->message) +
                " (Upstream-Status: " + p->upstream_status + ")");
            continue;
        }
        const fs::path target = p->file.empty() ? find_file_containing(ws, p->search) : ws / p->file;
        if (target.empty() || !fs::exists(target, ec)) {
            msg = p->file.empty() ? "texte introuvable dans les sources de " + recipe + " : « " + p->search + " »"
                                  : "fichier introuvable : " + (ws / p->file).string();
            return false;
        }
        std::string content = read_file(target);
        const size_t n = replace_in_string(content, p->search, p->replace, p->replace_all);
        if (n == 0) {
            msg = "« " + p->search + " » absent de " + fs::relative(target, ws, ec).string();
            return false;
        }
        {
            std::ofstream out(target, std::ios::binary | std::ios::trunc);
            out << content;
            if (!out) { msg = "écriture impossible : " + target.string(); return false; }
        }
        log("  " + std::to_string(n) + " remplacement(s) dans " + fs::relative(target, ws, ec).string());

        // Le message de commit devient l'en-tête du patch : sans
        // Upstream-Status, la QA patch-status échoue.
        const std::string wsq = shell_quote(ws.string());
        const std::string commit =
            "git -C " + wsq + " add -A && git -C " + wsq +
            " -c user.name=" + shell_quote(config_.git_user_name) +
            " -c user.email=" + shell_quote(config_.git_user_email) +
            " commit -q -m " + shell_quote(p->message) +
            " -m " + shell_quote("Modification générée par yocto-tui (" + p->name + ").") +
            " -m " + shell_quote("Upstream-Status: " + p->upstream_status);
        if (!exec(commit, err)) { msg = err; return false; }
    }

    // 4) Export des patchs et du bbappend dans le layer du projet.
    if (!exec_env("devtool finish " + rq + " " + shell_quote(layer.string()), err)) { msg = err; return false; }
    msg = recipe + " patché";
    return true;
}

} // namespace yocto
