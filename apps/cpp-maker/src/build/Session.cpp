/* @file build/Session.cpp */

#include "build/Session.hpp"

#include "build/TestSupport.hpp"
#include "core/Process.hpp"
#include "core/Term.hpp"
#include "core/Version.hpp"

#include <algorithm>
#include <chrono>
#include <set>

namespace build {

std::vector<std::string> pick_toolchains(const model::Workspace& ws, const Request& r) {
    if (r.toolchains.empty()) return {ws.project.default_toolchain};
    std::vector<std::string> out;
    for (auto& t : r.toolchains) {
        if (t == "all") {
            util::push_unique(out, ws.project.default_toolchain);
            for (auto& [n, c] : ws.project.toolchains) util::push_unique(out, n);
        } else {
            util::push_unique(out, t);
        }
    }
    return out;
}

std::vector<std::string> pick_modes(const model::Workspace& ws, const Request& r) {
    std::vector<std::string> out = r.modes.empty() ? std::vector<std::string>{ws.project.default_mode} : r.modes;
    for (auto& m : out) {
        if (!ws.project.find_mode(m)) {
            std::vector<std::string> names;
            for (auto& x : ws.project.modes) names.push_back(x.name);
            auto s = util::suggest(m, names);
            util::fail("mode inconnu '{}'{} (modes : {})", m, s.empty() ? "" : " — vouliez-vous dire '" + s + "' ?",
                       util::join(names, ", "));
        }
    }
    return out;
}

Session Session::open(const model::Workspace& ws, const std::string& tc_name, const std::string& mode_name,
                      const std::vector<std::string>& targets, bool with_tests, const Request& r, bool with_lock) {
    Session s;
    s.ws = &ws;
    s.tc = std::make_unique<toolchain::Toolchain>(toolchain::resolve(ws.project, tc_name, r.cc, r.cxx));
    const model::ModeConfig* base = ws.project.find_mode(mode_name);
    if (!base) util::fail("mode inconnu '{}'", mode_name);
    s.mode = sanitize::apply(*base, r.san, *s.tc);
    if (with_lock)
        s.lock.acquire(ws.root / model::kBusyLockFile, "un autre cpp-maker travaille sur ce projet, attente…");
    s.plan = std::make_unique<Plan>(make_plan(ws, *s.tc, s.mode, targets, with_tests));
    return s;
}

void Session::print_header() const {
    term::info("{} · toolchain {} ({}{}) · mode {}", ws->project.name, term::bold(tc->name),
               tc->triple.empty() ? "?" : tc->triple, tc->cross ? ", croisée" : "", term::bold(mode.name));
    std::vector<std::string> names;
    for (auto& m : plan->order) names.push_back(util::contains(plan->selected, m) ? m : m + "*");
    term::outln("{}", term::dim(std::format("            cibles : {}{}", util::join(names, ", "),
                                            names.size() != plan->selected.size() ? "   (* = dépendance)" : "")));
}

int Session::build(const Request& r, const std::string& command) {
    print_header();
    auto& pl = *plan;
    if (r.dry_run) {
        auto dirty = predict_dirty(pl.tasks);
        size_t n = 0;
        for (size_t i = 0; i < pl.tasks.size(); ++i) {
            if (!dirty[i]) continue;
            ++n;
            term::outln("  {} {:<6} {}", term::magenta("[simulation]"), pl.tasks[i].kind, pl.tasks[i].label);
            if (r.verbose) term::outln("      $ {}", util::shell_join(pl.tasks[i].cmd));
        }
        term::outln("{} tâche(s) à exécuter sur {}.", n, pl.tasks.size());
        return 0;
    }
    if (r.clean) {
        std::error_code ec;
        fs::remove_all(pl.build_dir, ec);
        term::info("nettoyage de {}", util::display(pl.build_dir, ws->root));
        if (pl.with_tests) pl.support_dir = ensure_test_support(pl.build_dir);
    }
    write_compdb(pl, pl.build_dir / "compile_commands.json");
    if (!run_generate_steps(*ws, command, r.verbose)) return 1;

    auto t0 = std::chrono::steady_clock::now();
    ExecOptions eo;
    eo.jobs = r.jobs > 0 ? r.jobs : ws->project.jobs;
    eo.verbose = r.verbose;
    eo.keep_going = r.keep_going;
    eo.root = ws->root;
    auto st = execute(pl.tasks, eo);
    if (st.failed > 0) {
        term::errln("{} ({})", term::red(std::format("✗ {} tâche(s) en échec, {} non exécutée(s)", st.failed, st.skipped)),
                    util::elapsed(t0));
        return 1;
    }
    if (st.ran == 0) term::ok("rien à faire, tout est à jour ({} tâches)", st.uptodate);
    else term::ok("{} tâche(s) exécutée(s), {} à jour — {}", st.ran, st.uptodate, util::elapsed(t0));
    return 0;
}

std::optional<std::vector<std::string>> Session::launcher(std::string& why) const {
    std::vector<std::string> cmd;
    if (!tc->can_execute()) {
        why = tc->runner_note.empty() ? "exécution impossible pour cette cible" : tc->runner_note;
        return std::nullopt;
    }
    if (!mode.runner.empty()) {
        if (!tc->runner.empty()) {
            why = std::format("le mode '{}' ({}) n'est pas compatible avec un émulateur", mode.name, mode.runner);
            return std::nullopt;
        }
        if (!util::have(mode.runner)) {
            why = std::format("{} introuvable — mode '{}' indisponible", mode.runner, mode.name);
            return std::nullopt;
        }
        cmd.push_back(mode.runner);
        cmd.insert(cmd.end(), mode.runner_args.begin(), mode.runner_args.end());
    }
    cmd.insert(cmd.end(), tc->runner.begin(), tc->runner.end());
    return cmd;
}

// ============================================================================
// Exports
// ============================================================================

void write_compdb(const Plan& pl, const fs::path& out) {
    std::string s = "[\n";
    bool first = true;
    for (auto& t : pl.tasks) {
        if (!t.is_compile()) continue;
        if (!first) s += ",\n";
        first = false;
        s += "  {\n    \"directory\": \"" + util::json_escape(pl.ws->root.string()) + "\",\n    \"arguments\": [";
        for (size_t i = 0; i < t.cmd.size(); ++i) {
            if (i) s += ", ";
            s += "\"" + util::json_escape(t.cmd[i]) + "\"";
        }
        s += "],\n    \"file\": \"" + util::json_escape(t.source.string()) + "\",\n";
        s += "    \"output\": \"" + util::json_escape(t.outputs[0].string()) + "\"\n  }";
    }
    s += "\n]\n";
    util::write_file_if_changed(out, s);
}

namespace {
std::string ninja_path(const std::string& s) {
    std::string r;
    for (char c : s) {
        if (c == '$' || c == ' ' || c == ':') r += '$';
        if (c == '\n') continue;
        r += c;
    }
    return r;
}
std::string ninja_value(const std::string& s) {
    return util::replace_all(util::replace_all(s, "$", "$$"), "\n", " ");
}
} // namespace

fs::path write_ninja(const Plan& pl) {
    std::string s;
    s += std::format("# Généré par cpp-maker {} — ne pas modifier.\n", cppmaker::kVersion);
    s += std::format("# Toolchain : {}   Mode : {}\n", pl.tc->name, pl.mode->name);
    s += "# Usage : ninja -f " + (pl.build_dir / "build.ninja").string() + "\n\n";
    s += "ninja_required_version = 1.3\n";
    s += "builddir = " + ninja_path(pl.build_dir.string()) + "\n\n";
    s += "rule compile\n  command = $cmd\n  description = $kind $label\n  depfile = $depfile\n  deps = gcc\n\n";
    s += "rule run\n  command = $cmd\n  description = $kind $label\n\n";
    std::vector<std::string> all;
    for (auto& t : pl.tasks) {
        std::string outs, ins, implicit;
        for (auto& o : t.outputs) outs += " " + ninja_path(o.string());
        std::set<std::string> in_set;
        for (auto& i : t.inputs) {
            ins += " " + ninja_path(i.string());
            in_set.insert(i.string());
        }
        for (auto d : t.deps)
            for (auto& o : pl.tasks[d].outputs)
                if (!in_set.count(o.string())) implicit += " " + ninja_path(o.string());
        std::string cmd = util::shell_join(t.cmd);
        if (t.remove_outputs_first) cmd = "rm -f " + util::shell_quote(t.outputs[0].string()) + " && " + cmd;
        s += std::format("build{}: {}{}{}\n", outs, t.is_compile() ? "compile" : "run", ins,
                         implicit.empty() ? "" : " |" + implicit);
        s += "  cmd = " + ninja_value(cmd) + "\n";
        s += "  kind = " + t.kind + "\n";
        s += "  label = " + ninja_value(t.label) + "\n";
        if (t.is_compile()) s += "  depfile = " + ninja_value(t.depfile.string()) + "\n";
        s += "\n";
        if (!t.is_compile())
            for (auto& o : t.outputs) all.push_back(ninja_path(o.string()));
    }
    if (all.empty())
        for (auto& t : pl.tasks)
            for (auto& o : t.outputs) all.push_back(ninja_path(o.string()));
    s += "build all: phony " + util::join(all, " ") + "\ndefault all\n";
    fs::path out = pl.build_dir / "build.ninja";
    util::write_file_if_changed(out, s);
    return out;
}

int run_ninja(Session& s, const Request& r) {
    if (!run_generate_steps(*s.ws, "build", r.verbose)) return 1;
    s.print_header();
    if (!util::have("ninja")) util::fail("ninja introuvable dans le PATH (backend ninja)");
    auto file = write_ninja(*s.plan);
    write_compdb(*s.plan, s.plan->build_dir / "compile_commands.json");
    std::vector<std::string> cmd = {"ninja", "-f", file.string(), "-j",
                                    std::to_string(parallel_jobs(r.jobs > 0 ? r.jobs : s.ws->project.jobs))};
    if (r.verbose) cmd.push_back("-v");
    if (r.keep_going) cmd.insert(cmd.end(), {"-k", "0"});
    if (r.dry_run) cmd.push_back("-n");
    return proc::run(cmd, {.cwd = s.ws->root}).exit_code;
}

// ============================================================================
// [generate.*]
// ============================================================================

bool run_generate_steps(const model::Workspace& ws, const std::string& command, bool verbose) {
    auto newest = [&](const std::vector<std::string>& paths, bool& missing) {
        fs::file_time_type t = fs::file_time_type::min();
        std::error_code ec;
        for (auto& r : paths) {
            fs::path path = ws.root / r;
            if (!fs::exists(path, ec)) {
                missing = true;
                continue;
            }
            if (fs::is_directory(path, ec)) {
                for (auto& e : fs::recursive_directory_iterator(path, ec))
                    if (e.is_regular_file(ec)) t = std::max(t, e.last_write_time(ec));
            } else {
                t = std::max(t, fs::last_write_time(path, ec));
            }
        }
        return t;
    };
    for (auto& g : ws.project.generate) {
        bool trigger = g.run_before.empty() ? command == "build" || command == "run" || command == "test" ||
                                                  command == "check" || command == "package" || command == "install"
                                            : util::contains(g.run_before, "all") || util::contains(g.run_before, command);
        if (!trigger) continue;
        fs::path stamp = ws.build_path() / "generate" / (g.name + ".stamp");
        std::error_code ec;
        bool missing_in = false, missing_out = false;
        auto in = newest(g.inputs, missing_in);
        (void)newest(g.outputs, missing_out);
        std::string old;
        bool same_cmd = util::read_file(stamp, old) && util::trim(old) == g.command;
        bool stale = g.always || !same_cmd || missing_out || g.inputs.empty() ||
                     (fs::exists(stamp, ec) && in > fs::last_write_time(stamp, ec));
        if (!stale) continue;
        term::outln("{} {} : {}", term::magenta("[generate]"), g.name, g.command);
        auto r = proc::shell(g.command, {.capture = !verbose, .cwd = ws.root});
        if (r.exit_code != 0) {
            if (!verbose) std::cerr << r.output;
            term::error("generate.{} a échoué (code {})", g.name, r.exit_code);
            return false;
        }
        util::write_file(stamp, g.command + "\n");
    }
    return true;
}

} // namespace build
