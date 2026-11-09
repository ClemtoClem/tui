/* @file vcs/Git.cpp */

#include "vcs/Git.hpp"

#include "build/Planner.hpp"
#include "core/Process.hpp"
#include "core/Term.hpp"
#include "core/Util.hpp"
#include "core/Version.hpp"

#include <algorithm>
#include <chrono>
#include <set>

namespace vcs {

namespace {

constexpr const char* kHookMarker = "# cpp-maker: pre-commit";

bool is_within(const fs::path& p, const fs::path& dir) {
    auto rel = p.lexically_relative(dir);
    return !rel.empty() && !rel.native().starts_with("..");
}

std::vector<fs::path> split_z(const std::string& out, const fs::path& top, const fs::path& root) {
    std::vector<fs::path> r;
    for (auto& f : util::split(out, '\0', false)) {
        fs::path abs = (top / f).lexically_normal();
        if (is_within(abs, root) || abs == root) r.push_back(abs);
    }
    return r;
}

fs::path canon(const fs::path& p) {
    std::error_code ec;
    auto c = fs::weakly_canonical(p, ec);
    return ec ? p : c;
}

} // namespace

std::optional<fs::path> toplevel(const fs::path& dir) {
    auto r = proc::capture({"git", "-C", dir.string(), "rev-parse", "--show-toplevel"});
    if (r.exit_code != 0) return std::nullopt;
    return canon(util::trim(r.output));
}

int git(const fs::path& dir, const std::vector<std::string>& args) {
    std::vector<std::string> cmd = {"git", "-C", dir.string()};
    cmd.insert(cmd.end(), args.begin(), args.end());
    return proc::run(cmd).exit_code;
}

std::optional<std::string> git_capture(const fs::path& dir, const std::vector<std::string>& args) {
    std::vector<std::string> cmd = {"git", "-C", dir.string()};
    cmd.insert(cmd.end(), args.begin(), args.end());
    auto r = proc::capture(cmd);
    if (r.exit_code != 0) return std::nullopt;
    return r.output;
}

namespace {
fs::path require_top(const fs::path& root) {
    if (!util::have("git")) util::fail("git introuvable dans le PATH");
    auto top = toplevel(root);
    if (!top) util::fail("{} n'est pas dans un dépôt git (cpp-maker git init)", root.string());
    return *top;
}
} // namespace

namespace {
std::string git_paths(const fs::path& root, std::vector<std::string> args, const std::vector<fs::path>& paths) {
    auto top = require_top(root);
    args.push_back("--");
    for (auto& p : paths) args.push_back(p.lexically_relative(top).generic_string());
    std::vector<std::string> cmd = {"git", "-C", top.string()};
    cmd.insert(cmd.end(), args.begin(), args.end());
    auto r = proc::capture(cmd);
    return r.exit_code == 0 ? std::string() : util::trim(r.output);
}
} // namespace

std::string stage(const fs::path& project_root, const std::vector<fs::path>& paths) {
    return git_paths(project_root, {"add", "-A"}, paths);
}

std::string unstage(const fs::path& project_root, const std::vector<fs::path>& paths) {
    auto top = require_top(project_root);
    bool has_head = proc::capture({"git", "-C", top.string(), "rev-parse", "--verify", "-q", "HEAD"}).exit_code == 0;
    if (has_head) return git_paths(project_root, {"restore", "--staged"}, paths);
    return git_paths(project_root, {"rm", "--cached", "-r", "-q", "--ignore-unmatch"}, paths);
}

std::vector<fs::path> staged_files(const fs::path& project_root) {
    auto top = require_top(project_root);
    auto out = git_capture(top, {"diff", "--cached", "--name-only", "--diff-filter=ACMR", "-z"});
    return out ? split_z(*out, top, canon(project_root)) : std::vector<fs::path>{};
}

std::vector<fs::path> tracked_files(const fs::path& project_root) {
    auto top = require_top(project_root);
    auto out = git_capture(top, {"ls-files", "-z"});
    return out ? split_z(*out, top, canon(project_root)) : std::vector<fs::path>{};
}

Status status(const fs::path& project_root) {
    auto top = require_top(project_root);
    Status st;
    auto out = git_capture(top, {"status", "--porcelain=v1", "-b", "-z", "--untracked-files=all"});
    if (!out) util::fail("git status a échoué");
    auto root = canon(project_root);
    auto items = util::split(*out, '\0', false);
    for (size_t i = 0; i < items.size(); ++i) {
        const std::string& it = items[i];
        if (it.starts_with("## ")) {
            std::string b = it.substr(3);
            auto br = b.find(" [");
            std::string info = br == std::string::npos ? "" : b.substr(br + 2, b.size() - br - 3);
            b = b.substr(0, br);
            auto dots = b.find("...");
            st.branch = b.substr(0, dots);
            if (st.branch.starts_with("No commits yet on ")) st.branch = st.branch.substr(18);
            if (dots != std::string::npos) st.upstream = b.substr(dots + 3);
            for (auto& part : util::split_list(info)) {
                if (part.starts_with("ahead ")) st.ahead = std::atoi(part.c_str() + 6);
                if (part.starts_with("behind ")) st.behind = std::atoi(part.c_str() + 7);
            }
            continue;
        }
        if (it.size() < 4) continue;
        StatusEntry e;
        e.index = it[0];
        e.worktree = it[1];
        e.path = (top / it.substr(3)).lexically_normal();
        if (e.index == 'R' || e.index == 'C') ++i;   // l'ancien nom suit
        if (is_within(e.path, root)) st.entries.push_back(std::move(e));
    }
    return st;
}

std::string owner_of(const model::Workspace& ws, const fs::path& file) {
    fs::path f = canon(file);
    for (auto& t : ws.targets)
        if (!t.from_git && is_within(f, canon(ws.abs(t)))) return t.id;
    for (auto& L : ws.layers)
        if (is_within(f, canon(ws.root / L.dir))) return "layer:" + L.name;
    return "project";
}

// ============================================================================
// Crochet
// ============================================================================

fs::path hook_path(const fs::path& project_root) {
    auto top = require_top(project_root);
    auto out = git_capture(top, {"rev-parse", "--git-path", "hooks"});
    if (!out) util::fail("impossible de trouver le dossier des crochets git");
    fs::path dir = util::trim(*out);
    if (dir.is_relative()) dir = top / dir;
    return dir.lexically_normal() / "pre-commit";
}

HookState hook_state(const fs::path& project_root) {
    fs::path p = hook_path(project_root);
    std::error_code ec;
    if (!fs::exists(p, ec)) return HookState::Absent;
    std::string text;
    util::read_file(p, text);
    return text.find(kHookMarker) != std::string::npos ? HookState::Ours : HookState::Foreign;
}

void install_hook(const fs::path& project_root, const fs::path& self, bool force) {
    fs::path p = hook_path(project_root);
    auto state = hook_state(project_root);
    std::error_code ec;
    if (state == HookState::Foreign) {
        if (!force)
            util::fail("un crochet pre-commit existe déjà ({}) — --force pour le remplacer "
                       "(il sera conservé en pre-commit.cpp-maker-backup)", p.string());
        fs::path backup = p;
        backup += ".cpp-maker-backup";
        fs::remove(backup, ec);
        fs::rename(p, backup, ec);   // un lien symbolique est déplacé tel quel
        if (ec) util::fail("sauvegarde impossible de {} : {}", p.string(), ec.message());
        term::info("ancien crochet conservé : {}", backup.string());
    }
    std::string script = std::format(R"(#!/bin/sh
{}
# Installé par cpp-maker {} — « cpp-maker git hook uninstall » pour le retirer.
# Les règles sont définies dans les fichiers toml : [git.pre_commit.<nom>].
# Contournement ponctuel : git commit --no-verify
CM={}
[ -x "$CM" ] || CM=cpp-maker
exec "$CM" -C {} git pre-commit
)",
                                     kHookMarker, cppmaker::kVersion, util::shell_quote(self.string()),
                                     util::shell_quote(canon(project_root).string()));
    fs::create_directories(p.parent_path(), ec);
    util::write_file(p, script);
    fs::permissions(p, fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec | fs::perms::others_read |
                           fs::perms::others_exec, ec);
}

void uninstall_hook(const fs::path& project_root) {
    fs::path p = hook_path(project_root);
    auto state = hook_state(project_root);
    if (state == HookState::Absent) util::fail("aucun crochet pre-commit installé");
    if (state == HookState::Foreign) util::fail("le crochet {} n'a pas été installé par cpp-maker : laissé en place", p.string());
    std::error_code ec;
    fs::remove(p, ec);
    fs::path backup = p;
    backup += ".cpp-maker-backup";
    if (fs::is_symlink(backup, ec) || fs::exists(backup, ec)) {
        fs::rename(backup, p, ec);
        term::info("ancien crochet restauré : {}", p.string());
    }
}

// ============================================================================
// Règles
// ============================================================================

std::vector<RuleRef> all_rules(const model::Workspace& ws) {
    std::vector<RuleRef> r;
    for (auto& rule : ws.project.pre_commit) r.push_back({&rule, "projet", ws.root, "", ""});
    for (auto& L : ws.layers)
        for (auto& rule : L.pre_commit) r.push_back({&rule, "layer " + L.name, ws.root / L.dir, L.name, ""});
    for (auto& t : ws.targets) {
        if (t.from_git) continue;
        for (auto& rule : t.pre_commit) r.push_back({&rule, t.kind_label() + " " + t.id, ws.abs(t), "", t.id});
    }
    return r;
}

int run_pre_commit(const model::Workspace& ws, const PreCommitOptions& o) {
    auto rules = all_rules(ws);
    std::vector<fs::path> files;
    if (!o.files.empty()) {
        for (auto& f : o.files) files.push_back(canon(util::under(o.cwd, f)));
    } else {
        files = o.all ? tracked_files(ws.root) : staged_files(ws.root);
    }
    fs::path root = canon(ws.root);
    if (rules.empty()) {
        term::info("pre-commit : aucune règle [git.pre_commit.*] dans ce projet");
        return 0;
    }
    if (files.empty()) {
        term::info("pre-commit : aucun fichier {} — rien à vérifier", o.all ? "suivi" : "indexé");
        return 0;
    }
    // Cible de chaque fichier (pour {targets}).
    std::vector<std::string> owners;
    for (auto& f : files) owners.push_back(owner_of(ws, f));

    auto t0 = std::chrono::steady_clock::now();
    int ran = 0, failed = 0, warned = 0, skipped = 0;
    std::vector<std::string> failures;
    for (auto& ref : rules) {
        const auto& rule = *ref.rule;
        std::string label = std::format("{} ({})", rule.name, ref.scope);
        if (!rule.enabled) {
            ++skipped;
            continue;
        }
        fs::path scope_dir = canon(ref.dir);
        std::vector<std::string> rel_files, targets;
        for (size_t i = 0; i < files.size(); ++i) {
            const fs::path& f = files[i];
            if (ref.scope != "projet" && !is_within(f, scope_dir)) continue;
            std::string rel = f.lexically_relative(root).generic_string();
            if (!rule.when.empty() &&
                std::none_of(rule.when.begin(), rule.when.end(), [&](auto& w) { return build::glob_match(w, rel); }))
                continue;
            rel_files.push_back(rel);
            const std::string& own = owners[i];
            if (!ref.target.empty()) util::push_unique(targets, ref.target);
            else if (own != "project" && !own.starts_with("layer:")) {
                auto* t = ws.find(own);
                if (ref.layer.empty() || (t && t->layer == ref.layer)) util::push_unique(targets, own);
            }
        }
        if (rel_files.empty()) {
            ++skipped;
            continue;
        }
        std::string body = rule.run.empty() ? rule.command : rule.run;
        if (body.find("{targets}") != std::string::npos && targets.empty()) {
            ++skipped;   // aucune cible touchée dans la portée
            continue;
        }
        std::vector<std::string> quoted;
        for (auto& f : rel_files) quoted.push_back(util::shell_quote(f));
        body = util::replace_all(body, "{targets}", util::join(targets, ","));
        body = util::replace_all(body, "{files}", util::join(quoted, " "));
        body = util::replace_all(body, "{root}", util::shell_quote(root.string()));
        body = util::replace_all(body, "{name}", ws.project.name);
        std::string line = rule.run.empty() ? body : util::shell_quote(o.self.string()) + " " + body;
        term::outln("\n{}", term::cyan(std::format("── pre-commit : {} ──", label)));
        if (!rule.description.empty()) term::outln("{}", term::dim(rule.description));
        term::outln("  {} {}", term::magenta("$"), rule.run.empty() ? body : "cpp-maker " + body);
        if (o.dry_run) {
            ++ran;
            continue;
        }
        auto r = proc::shell(line, {.cwd = ws.root, .env = rule.env});
        ++ran;
        if (r.exit_code == 0) continue;
        if (rule.on_failure == "warn") {
            ++warned;
            term::warn("règle '{}' en échec (code {}) — avertissement seulement", rule.name, r.exit_code);
            continue;
        }
        ++failed;
        failures.push_back(label);
        term::error("règle '{}' en échec (code {})", rule.name, r.exit_code);
        if (!o.keep_going) break;
    }
    term::outln();
    if (failed > 0) {
        term::errln("{}", term::red(std::format("✗ pre-commit : {} règle(s) en échec : {} — commit refusé "
                                                "(git commit --no-verify pour passer outre)",
                                                failed, util::join(failures, ", "))));
        return 1;
    }
    term::ok("pre-commit : {} règle(s) {}, {} non concernée(s){} — {}", ran, o.dry_run ? "à exécuter" : "réussie(s)",
             skipped, warned ? std::format(", {} avertissement(s)", warned) : "", util::elapsed(t0));
    return 0;
}

// ============================================================================
// Maintenance des références
// ============================================================================

namespace {

/// Options de cpp-maker qui attendent une valeur (qui n'est pas une cible).
const std::set<std::string> kValueOptions = {
    "--mode", "-t", "--toolchain", "--cc", "--cxx", "-j", "--jobs", "--backend", "--sanitize", "--sanitizer",
    "--sanitize-trap", "--no-sanitize-recover", "--sanitize-ignorelist", "--modes", "-f", "--filter", "--junit",
    "--timeout", "--format", "--prefix", "--destdir", "--output", "--stdout", "--stderr", "-C", "--dir"};
const std::set<std::string> kTargetOptions = {"-m", "--target"};
/// Sous-commandes dont les positionnels sont des cibles (ou des modes).
const std::set<std::string> kTargetCommands = {"build", "test", "check", "run", "clean", "size", "package", "install",
                                               "info", "graph"};

/// Rôle de chaque mot d'une ligne « run » : true = liste de références de cibles.
std::vector<bool> selector_mask(const std::vector<std::string>& words) {
    std::vector<bool> mask(words.size(), false);
    size_t i = 0;
    while (i < words.size() && words[i].starts_with("-")) {   // options globales (--output f…)
        if (kValueOptions.count(words[i])) ++i;
        ++i;
    }
    if (i >= words.size() || !kTargetCommands.count(words[i])) return mask;
    for (size_t k = i + 1; k < words.size(); ++k) {
        const std::string& w = words[k];
        if (w == "--") break;
        if (w.find('{') != std::string::npos) continue;   // {targets}…
        if (w.starts_with("-")) {
            if (w.find('=') != std::string::npos) continue;
            if (kTargetOptions.count(w) && k + 1 < words.size()) mask[++k] = true;
            else if (kValueOptions.count(w)) ++k;
            continue;
        }
        mask[k] = true;
    }
    return mask;
}

std::optional<std::string> map_path(const std::string& pattern, const RefMap& paths) {
    return paths ? paths(pattern) : std::nullopt;
}

} // namespace

std::vector<std::string> run_selectors(const std::string& run) {
    auto words = util::split_ws(run);
    auto mask = selector_mask(words);
    std::vector<std::string> r;
    for (size_t i = 0; i < words.size(); ++i)
        if (mask[i])
            for (auto& item : util::split_list(words[i])) r.push_back(item);
    return r;
}

std::vector<std::string> update_rules(model::Transaction& tx, const model::Workspace& ws, const RefMap& items,
                                      const RefMap& paths, bool drop_emptied, const fs::path& skip_dir) {
    std::vector<std::string> report;
    for (auto& ref : all_rules(ws)) {
        const auto& rule = *ref.rule;
        if (!skip_dir.empty() && (rule.file.parent_path() == skip_dir || is_within(rule.file, skip_dir))) continue;
        bool changed = false, emptied = false;
        std::vector<std::string> notes;

        // run : listes de cibles.
        std::string run = rule.run;
        if (!run.empty() && items) {
            auto words = util::split_ws(run);
            auto mask = selector_mask(words);
            std::vector<std::string> out;
            for (size_t i = 0; i < words.size(); ++i) {
                if (!mask[i]) {
                    out.push_back(words[i]);
                    continue;
                }
                std::vector<std::string> kept;
                bool touched = false;
                for (auto& item : util::split_list(words[i])) {
                    auto nv = items(item);
                    if (!nv) kept.push_back(item);
                    else {
                        touched = true;
                        if (!nv->empty()) util::push_unique(kept, *nv);
                    }
                }
                if (!touched) {
                    out.push_back(words[i]);
                    continue;
                }
                changed = true;
                if (kept.empty()) {
                    emptied = true;
                    if (!out.empty() && kTargetOptions.count(out.back())) out.pop_back();
                } else {
                    out.push_back(util::join(kept, ","));
                }
            }
            run = util::join(out, " ");
        }
        // when : motifs.
        std::vector<std::string> when;
        bool when_touched = false;
        for (auto& w : rule.when) {
            auto nv = map_path(w, paths);
            if (!nv) when.push_back(w);
            else {
                when_touched = true;
                if (!nv->empty()) util::push_unique(when, *nv);
            }
        }
        if (when_touched) {
            changed = true;
            if (when.empty()) emptied = true;
        }
        // command : chemins (remplacés au renommage ; signalés à la suppression).
        std::string command = rule.command;
        if (!command.empty() && paths) {
            auto words = util::split_ws(command);
            for (auto& w : words) {
                // Plus long préfixe de chemin du mot reconnu par `paths`.
                for (std::string cur = w; !cur.empty();) {
                    if (auto nv = paths(cur)) {
                        if (nv->empty()) notes.push_back(std::format("la commande référence {} (retiré) : à vérifier", cur));
                        else {
                            command = util::replace_all(command, w, *nv + w.substr(cur.size()));
                            changed = true;
                        }
                        break;
                    }
                    auto pos = cur.rfind('/');
                    if (pos == std::string::npos) break;
                    cur = cur.substr(0, pos);
                }
            }
        }
        std::string where = std::format("règle pre-commit '{}' ({})", rule.name, ref.scope);
        for (auto& n : notes) report.push_back(where + " : " + n);
        if (!changed) continue;
        auto& d = tx.doc(rule.file);
        std::vector<std::string> table = {"git", "pre_commit", rule.name};
        if (emptied && drop_emptied) {
            d.remove_table(table);
            report.push_back(where + " supprimée (elle ne visait plus que des éléments retirés)");
            continue;
        }
        if (run != rule.run) d.set(table, "run", toml::Value(run));
        if (command != rule.command) d.set(table, "command", toml::Value(command));
        if (when_touched) {
            if (when.empty()) d.remove(table, "when");
            else d.set(table, "when", toml::Value(when));
        }
        if (emptied) {
            d.set(table, "enabled", toml::Value(false));
            report.push_back(where + " désactivée (plus aucune cible ni aucun fichier visé)");
        } else {
            report.push_back(where + " mise à jour");
        }
    }
    return report;
}

namespace {

/// Fichiers du projet (hors build, dist, .git…), relatifs à la racine.
std::vector<std::string> project_files(const model::Workspace& ws) {
    std::vector<std::string> out;
    std::set<fs::path> skip = {ws.build_path(), ws.dist_path(), ws.root / ".git", ws.root / ".cpp-maker-trash",
                               ws.root / ws.project.out_dir};
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(ws.root, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (it->is_directory(ec) && skip.count(it->path())) {
            it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file(ec)) out.push_back(it->path().lexically_relative(ws.root).generic_string());
    }
    return out;
}

bool selector_ok(const model::Workspace& ws, const std::string& item) {
    if (item == "all" || item.find('{') != std::string::npos) return true;
    auto parts = util::split_list(item);
    if (ws.project.find_mode(item)) return true;
    return !ws.resolve_selector_noexcept(item).empty();
}

} // namespace

std::vector<std::string> stale_rule_refs(const model::Workspace& ws, const model::PreCommitRule& rule) {
    std::vector<std::string> r;
    for (auto& item : run_selectors(rule.run))
        if (!selector_ok(ws, item)) r.push_back(std::format("cible inconnue '{}' dans run", item));
    if (!rule.when.empty()) {
        auto files = project_files(ws);
        for (auto& w : rule.when)
            if (std::none_of(files.begin(), files.end(), [&](auto& f) { return build::glob_match(w, f); }))
                r.push_back(std::format("le motif when '{}' ne correspond à aucun fichier", w));
    }
    return r;
}

RefMap stale_items_map(const model::Workspace& ws) {
    return [&ws](const std::string& item) -> std::optional<std::string> {
        if (selector_ok(ws, item)) return std::nullopt;
        return std::string();
    };
}

RefMap stale_paths_map(const model::Workspace& ws) {
    auto files = std::make_shared<std::vector<std::string>>(project_files(ws));
    return [files](const std::string& pattern) -> std::optional<std::string> {
        // Appelé aussi sur des préfixes de chemins de command : seuls les motifs morts sont retirés.
        bool any = std::any_of(files->begin(), files->end(), [&](auto& f) {
            return build::glob_match(pattern, f) || f.starts_with(pattern + "/");
        });
        if (any) return std::nullopt;
        if (pattern.find('*') == std::string::npos && pattern.find('?') == std::string::npos &&
            pattern.find('/') == std::string::npos)
            return std::nullopt;   // mot isolé d'une commande : ignoré
        return std::string();
    };
}

} // namespace vcs
