/* @file commands/BuildCommands.cpp */

#include "commands/BuildCommands.hpp"

#include "analysis/Elf.hpp"
#include "build/Tests.hpp"
#include "commands/Options.hpp"
#include "core/Process.hpp"
#include "core/Term.hpp"
#include "core/Util.hpp"

#include <algorithm>

namespace commands {

std::string BuildCommand::GetDetails() const {
    return "Les positionnels sont des modes (release, debug, asan… ou listes « debug,release »)\n"
           "ou des cibles : « layer.module », « module », « layer » (tous ses modules),\n"
           "« application », « all ». Sans cible : celle du dossier courant, sinon tout le projet.\n"
           "Les dépendances des cibles sont compilées automatiquement.\n"
           "Artefacts : build/<toolchain>/<mode>/{bin,lib,tests}/ ; compile_commands.json est\n"
           "régénéré dans build/<toolchain>/<mode>/ à chaque build.";
}

void BuildCommand::ConfigureArgs(args::Arguments& a) { add_build_options(a, kOptAll); }

int BuildCommand::Run(Context& ctx) {
    auto& ws = ctx.workspace();
    auto r = read_request(ctx);
    auto ids = selected_targets(ctx, r);
    enforce_approval(ws, ids, ws.project.require_approval || r.require_approval, r.ignore_approval);
    int rc = 0;
    for (auto& tcn : build::pick_toolchains(ws, r)) {
        auto probe = toolchain::resolve(ws.project, tcn, r.cc, r.cxx);
        for (auto& mode : build::pick_modes(ws, r)) {
            for (auto& san : sanitize::expand_all(r.san, *ws.project.find_mode(mode), probe)) {
                build::Request rr = r;
                rr.san = san;
                auto s = build::Session::open(ws, tcn, mode, ids, r.tests, rr);
                int one = r.backend == "ninja" ? build::run_ninja(s, rr) : s.build(rr, "build");
                if (one != 0) {
                    rc = one;
                    if (!r.keep_going) return rc;
                    continue;
                }
                if (r.dry_run) continue;
                for (auto& id : s.plan->selected) {
                    auto& tp = s.plan->targets.at(id);
                    if (!tp.artifact.empty()) term::outln("  → {:<22} {}", id, util::display(tp.artifact, ws.root));
                    if (!tp.test_exe.empty())
                        term::outln("  → {:<22} {}", id + " (tests)", util::display(tp.test_exe, ws.root));
                }
            }
        }
    }
    return rc;
}

// ============================================================================

void RunCommand::ConfigureArgs(args::Arguments& a) {
    add_build_options(a, kOptToolchain | kOptJobs | kOptVerbose | kOptSanitize | kOptClean | kOptApproval);
}

int RunCommand::Run(Context& ctx) {
    auto& ws = ctx.workspace();
    auto r = read_request(ctx);
    auto modes = build::pick_modes(ws, r);
    auto tcs = build::pick_toolchains(ws, r);
    if (modes.size() != 1) ctx.usage("run : un seul mode à la fois");
    if (tcs.size() != 1) ctx.usage("run : une seule toolchain à la fois");
    if (r.selectors.size() > 1) ctx.usage("run : une seule application à la fois");

    std::string app;
    if (!r.selectors.empty()) {
        auto ids = ws.resolve_selector(r.selectors[0]);
        if (ids.size() != 1 || !ws.find(ids[0])->is_app())
            util::fail("'{}' n'est pas une application (cpp-maker list apps)", r.selectors[0]);
        app = ids[0];
    } else {
        auto here = ws.targets_at(ctx.cwd());
        if (here.size() == 1 && ws.find(here[0])->is_app()) app = here[0];
        else {
            auto apps = ws.applications();
            if (apps.empty()) util::fail("aucune application dans ce projet (cpp-maker add app <nom>)");
            if (apps.size() > 1) {
                std::vector<std::string> names;
                for (auto* a : apps) names.push_back(a->name);
                ctx.usage("plusieurs applications (" + util::join(names, ", ") + ") : précisez laquelle");
            }
            app = apps[0]->id;
        }
    }
    enforce_approval(ws, {app}, ws.project.require_approval || r.require_approval, r.ignore_approval);
    auto s = build::Session::open(ws, tcs[0], modes[0], {app}, false, r);
    if (s.build(r, "run") != 0) return 1;
    if (r.dry_run) return 0;
    // L'application peut elle-même invoquer cpp-maker : on libère le verrou.
    s.lock.release();
    std::string why;
    auto l = s.launcher(why);
    if (!l) {
        term::error("{}", why);
        return 2;
    }
    const auto& t = *ws.find(app);
    std::vector<std::string> cmd = *l;
    cmd.push_back(s.plan->targets.at(app).artifact.string());
    const auto& args = ctx.args().has_double_dash() ? ctx.args().trailing() : t.run_args;
    cmd.insert(cmd.end(), args.begin(), args.end());
    if (r.verbose) term::outln("      $ {}", util::shell_join(cmd));
    term::info("exécution de {}{}", app, args.empty() ? "" : std::format(" ({} argument(s))", args.size()));
    proc::Options po;
    po.env = t.run_env;
    if (!t.run_cwd.empty()) po.cwd = util::under(ws.root, t.run_cwd);
    return proc::run(cmd, po).exit_code;
}

// ============================================================================

namespace {
void add_test_options(args::Arguments& a) {
    a.add("--filter").set_value(std::string()).flags("-f").metavar("TEXTE").help("Ne lancer que les tests contenant TEXTE");
    a.add("--junit").set_value(std::string()).metavar("FICHIER").help("Écrire un rapport JUnit XML");
    a.add("--timeout").set_value(-1).metavar("S").help("Délai par exécutable (défaut : [tests].timeout)");
}

build::TestOptions read_test_options(Context& ctx) {
    build::TestOptions o;
    o.filter = ctx.get<std::string>("--filter");
    o.junit = ctx.get<std::string>("--junit");
    o.timeout = ctx.get<int>("--timeout");
    o.passthrough = ctx.args().trailing();
    return o;
}

int finish_tests(Context& ctx, const build::TestOptions& o, const std::vector<build::TestResult>& results) {
    if (!o.junit.empty()) {
        fs::path f = util::under(ctx.cwd(), o.junit);
        build::write_junit(f, results);
        term::info("rapport JUnit : {}", f.string());
    }
    return build::print_test_summary(results) > 0 ? 1 : 0;
}
} // namespace

void TestCommand::ConfigureArgs(args::Arguments& a) {
    add_build_options(a, kOptAll & ~kOptTests & ~kOptBackend);
    add_test_options(a);
}

int TestCommand::Run(Context& ctx) {
    auto& ws = ctx.workspace();
    auto r = read_request(ctx);
    auto ids = selected_targets(ctx, r);
    enforce_approval(ws, ids, ws.project.require_approval || r.require_approval, r.ignore_approval);
    auto o = read_test_options(ctx);
    std::vector<build::TestResult> results;
    for (auto& mode : build::pick_modes(ws, r)) build::run_tests(ws, r, ids, mode, o, results);
    return finish_tests(ctx, o, results);
}

void CheckCommand::ConfigureArgs(args::Arguments& a) {
    add_build_options(a, kOptAll & ~kOptTests & ~kOptBackend);
    a.add("--modes").set_value(std::vector<std::string>{}).metavar("LISTE").help("Modes à tester (défaut : debug,asan,ubsan)");
    add_test_options(a);
}

int CheckCommand::Run(Context& ctx) {
    auto& ws = ctx.workspace();
    auto r = read_request(ctx);
    for (auto& m : ctx.get<std::vector<std::string>>("--modes")) util::push_unique(r.modes, m);
    if (r.modes.empty()) r.modes = {"debug", "asan", "ubsan"};
    (void)build::pick_modes(ws, r);
    auto ids = selected_targets(ctx, r);
    enforce_approval(ws, ids, ws.project.require_approval || r.require_approval, r.ignore_approval);
    auto o = read_test_options(ctx);
    std::vector<build::TestResult> results;
    for (auto& mode : r.modes) {
        term::outln("\n{}", term::magenta(std::format("===== tests : {} =====", mode)));
        build::run_tests(ws, r, ids, mode, o, results);
    }
    return finish_tests(ctx, o, results);
}

// ============================================================================

void CleanCommand::ConfigureArgs(args::Arguments& a) {
    add_build_options(a, kOptToolchain & ~0u);
    a.add("--all").set_value(false).flags("-a").help("Supprimer aussi dist/");
}

int CleanCommand::Run(Context& ctx) {
    auto& ws = ctx.workspace({.resolve_git = false, .update_git = false, .validate = false});
    auto r = read_request(ctx);
    util::FileLock lock;
    lock.acquire(ws.root / model::kBusyLockFile, "un autre cpp-maker travaille sur ce projet, attente…");
    std::error_code ec;
    int removed = 0;
    auto rm = [&](const fs::path& p) {
        if (!fs::exists(p, ec)) return;
        fs::remove_all(p, ec);
        ++removed;
        term::outln("  supprimé : {}", util::display(p, ws.root));
    };
    std::vector<fs::path> tc_dirs;
    if (!r.toolchains.empty()) {
        for (auto& t : build::pick_toolchains(ws, r)) tc_dirs.push_back(ws.build_path() / t);
    } else if (fs::is_directory(ws.build_path(), ec)) {
        for (auto& e : fs::directory_iterator(ws.build_path(), ec))
            if (e.is_directory() && e.path().filename() != "generate") tc_dirs.push_back(e.path());
    }
    std::vector<fs::path> mode_dirs;
    for (auto& d : tc_dirs) {
        if (!r.modes.empty()) {
            for (auto& m : r.modes) mode_dirs.push_back(d / m);
        } else if (fs::is_directory(d, ec)) {
            for (auto& e : fs::directory_iterator(d, ec))
                if (e.is_directory()) mode_dirs.push_back(e.path());
        }
    }
    if (!r.selectors.empty()) {
        auto ids = ws.select(r.selectors, ctx.cwd());
        for (auto& md : mode_dirs) {
            for (auto& id : ids) {
                auto* t = ws.find(id);
                rm(t->is_app() ? md / "obj" / "apps" / t->name : md / "obj" / "libs" / t->layer / t->name);
                rm(md / "tests" / (id + "-tests"));
                rm(md / "stamps" / (id + ".link.stamp"));
                rm(md / "stamps" / (id + ".tests.stamp"));
                if (t->is_app()) rm(md / "bin" / t->artifact_base());
                else
                    for (auto ext : {".a", ".so", ".dylib"}) rm(md / "lib" / ("lib" + t->artifact_base() + ext));
            }
        }
    } else if (!r.modes.empty() || !r.toolchains.empty()) {
        for (auto& md : r.modes.empty() ? tc_dirs : mode_dirs) rm(md);
    } else {
        rm(ws.build_path());
    }
    if (ctx.flag("--all")) rm(ws.dist_path());
    if (removed == 0) term::outln("Rien à nettoyer.");
    return 0;
}

// ============================================================================

void SizeCommand::ConfigureArgs(args::Arguments& a) {
    add_build_options(a, kOptToolchain);
    a.add("--verbose").set_value(false).flags("-v").help("Détail des 10 plus grosses sections");
}

int SizeCommand::Run(Context& ctx) {
    auto& ws = ctx.workspace({.resolve_git = true, .update_git = false, .validate = false});
    auto r = read_request(ctx);
    auto modes = build::pick_modes(ws, r);
    std::vector<std::string> names;
    for (auto& id : ws.select(r.selectors, ctx.cwd())) {
        auto* t = ws.find(id);
        names.push_back(t->artifact_base());
        names.push_back(id + "-tests");
    }
    bool filter = !r.selectors.empty() || !ws.targets_at(ctx.cwd()).empty();
    auto wanted = [&](const fs::path& f) {
        if (!filter) return true;
        std::string fn = f.filename().string();
        for (auto& n : names)
            if (fn == n || fn == n + ".exe" || fn.starts_with("lib" + n + ".")) return true;
        return false;
    };
    size_t listed = 0;
    for (auto& tc : build::pick_toolchains(ws, r)) {
        for (auto& mode : modes) {
            fs::path dir = build::build_dir_for(ws, tc, mode);
            std::error_code ec;
            if (!fs::is_directory(dir, ec)) {
                term::warn("rien de construit pour {} / {} ({})", tc, mode, util::display(dir, ws.root));
                continue;
            }
            term::outln("{}", term::cyan(std::format("{} — toolchain {} · mode {}", util::display(dir, ws.root), tc, mode)));
            for (auto* sub : {"bin", "lib", "tests"}) {
                std::vector<fs::path> files;
                for (fs::directory_iterator it(dir / sub, ec), end; !ec && it != end; it.increment(ec))
                    if (it->is_regular_file(ec) && it->path().extension() != ".stamp") files.push_back(it->path());
                std::sort(files.begin(), files.end());
                for (auto& f : files) {
                    if (!wanted(f)) continue;
                    ++listed;
                    auto in = elf::read(f);
                    std::string rel = f.lexically_relative(dir).generic_string();
                    if (!in.ok) {
                        term::outln("  {} — {} ({} octets)", term::bold(rel), in.note, in.file_size);
                        continue;
                    }
                    if (in.members > 0)
                        term::outln("  {} — archive statique, {} objet(s) ELF ({})", term::bold(rel), in.members,
                                    util::human_size(in.file_size));
                    else
                        term::outln("  {} — {} {}, {}, ABI {}, {}, entrée 0x{:x} ({})", term::bold(rel), in.cls,
                                    in.endian, in.machine, in.abi, in.type, in.entry, util::human_size(in.file_size));
                    term::outln("    {:>10} {:>10} {:>10} {:>10} {:>10}", "text", "data", "bss", "dec", "hex");
                    term::outln("    {:>10} {:>10} {:>10} {:>10} {:>10x}", in.text, in.data, in.bss, in.dec(), in.dec());
                    if (ctx.flag("--verbose")) {
                        auto secs = in.sections;
                        std::sort(secs.begin(), secs.end(), [](auto& a, auto& b) { return a.size > b.size; });
                        for (size_t i = 0; i < std::min<size_t>(secs.size(), 10); ++i)
                            term::outln("      {:<24} {:>10} octets [{}{}]", secs[i].name.empty() ? "(sans nom)" : secs[i].name,
                                        secs[i].size, secs[i].flags & 4 ? "X" : "-", secs[i].flags & 1 ? "W" : "R");
                    }
                }
            }
        }
    }
    if (listed == 0) util::fail("aucun binaire à analyser — lancez d'abord « cpp-maker build »");
    return 0;
}

} // namespace commands
