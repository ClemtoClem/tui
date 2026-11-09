/* @file build/Tests.cpp */

#include "build/Tests.hpp"

#include "core/Process.hpp"
#include "core/Term.hpp"

#include <chrono>
#include <map>

namespace build {

int run_tests(const model::Workspace& ws, const Request& r, const std::vector<std::string>& targets,
              const std::string& mode, const TestOptions& o, std::vector<TestResult>& out) {
    int failures = 0;
    int timeout = o.timeout >= 0 ? o.timeout : ws.project.test_timeout;
    bool capture = !o.junit.empty();
    for (auto& tcn : pick_toolchains(ws, r)) {
        auto probe = toolchain::resolve(ws.project, tcn, r.cc, r.cxx);
        for (auto& req : sanitize::expand_all(r.san, *ws.project.find_mode(mode), probe)) {
            Request rr = r;
            rr.san = req;
            auto s = Session::open(ws, tcn, mode, targets, true, rr);
            const std::string suite = s.tc->name + "/" + s.mode.name;
            if (s.build(rr, "test") != 0) {
                out.push_back({suite, "compilation", "fail", 0, "échec de compilation", 1});
                ++failures;
                continue;
            }
            if (r.dry_run) continue;
            std::string why;
            auto l = s.launcher(why);
            bool any = false;
            for (auto& id : s.plan->selected) {
                auto& tp = s.plan->targets.at(id);
                if (tp.test_exe.empty()) continue;
                any = true;
                TestResult res;
                res.suite = suite;
                res.name = id;
                if (!l) {
                    res.status = "skip";
                    res.output = why;
                    term::outln("  {} {} — {}", term::yellow("[ SKIP ]"), id, why);
                    out.push_back(res);
                    continue;
                }
                std::vector<std::string> cmd = *l;
                cmd.push_back(tp.test_exe.string());
                if (!o.filter.empty() && tp.framework != "none") {
                    if (tp.framework == "gtest") cmd.push_back("--gtest_filter=*" + o.filter + "*");
                    else cmd.insert(cmd.end(), {"--filter", o.filter});
                }
                cmd.insert(cmd.end(), o.passthrough.begin(), o.passthrough.end());
                term::outln("\n{}", term::cyan(std::format("── tests : {} ({}) ──", id, suite)));
                if (r.verbose) term::outln("      $ {}", util::shell_join(cmd));
                std::vector<std::pair<std::string, std::string>> env;
                for (auto& [k, v] : ws.project.test_env) env.emplace_back(k, util::replace_all(v, "{root}", ws.root.string()));
                auto t0 = std::chrono::steady_clock::now();
                auto pr = proc::run(cmd, {.capture = capture, .cwd = ws.root,
                                           .timeout_sec = o.timeout >= 0 ? o.timeout : tp.cfg->test_timeout >= 0 ? tp.cfg->test_timeout : timeout, .env = env});
                res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                res.output = pr.output;
                res.code = pr.exit_code;
                if (capture && !pr.output.empty()) std::cout << pr.output;
                if (pr.timed_out) res.status = "timeout";
                else if (pr.exit_code == 0) res.status = "pass";
                else if (pr.exit_code == 77) res.status = "skip";
                else res.status = "fail";
                if (res.status == "fail" || res.status == "timeout") ++failures;
                out.push_back(res);
            }
            if (!any) term::outln("  {}", term::dim("(aucun test pour " + util::join(s.plan->selected, ", ") + ")"));
        }
    }
    return failures;
}

void write_junit(const fs::path& file, const std::vector<TestResult>& results) {
    std::map<std::string, std::vector<const TestResult*>> suites;
    for (auto& r : results) suites[r.suite].push_back(&r);
    std::string s = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<testsuites>\n";
    for (auto& [name, rs] : suites) {
        int fail = 0, skip = 0;
        double t = 0;
        for (auto* r : rs) {
            fail += r->status == "fail" || r->status == "timeout";
            skip += r->status == "skip";
            t += r->seconds;
        }
        s += std::format("  <testsuite name=\"{}\" tests=\"{}\" failures=\"{}\" skipped=\"{}\" time=\"{:.3f}\">\n",
                         util::xml_escape(name), rs.size(), fail, skip, t);
        for (auto* r : rs) {
            s += std::format("    <testcase classname=\"{}\" name=\"{}\" time=\"{:.3f}\">\n", util::xml_escape(name),
                             util::xml_escape(r->name), r->seconds);
            if (r->status == "fail" || r->status == "timeout")
                s += std::format("      <failure message=\"{}\"/>\n",
                                 r->status == "timeout" ? "délai dépassé" : std::format("code de sortie {}", r->code));
            if (r->status == "skip") s += "      <skipped/>\n";
            s += "      <system-out>" + util::xml_escape(r->output) + "</system-out>\n    </testcase>\n";
        }
        s += "  </testsuite>\n";
    }
    s += "</testsuites>\n";
    util::write_file(file, s);
}

int print_test_summary(const std::vector<TestResult>& results) {
    if (results.empty()) return 0;
    term::outln("\n{}", term::cyan("Résumé des tests"));
    int fail = 0, pass = 0, skip = 0;
    for (auto& r : results) {
        std::string tag = r.status == "pass"      ? term::green("[ PASS ]")
                          : r.status == "skip"    ? term::yellow("[ SKIP ]")
                          : r.status == "timeout" ? term::red("[DÉLAI ]")
                                                  : term::red("[ FAIL ]");
        term::outln("  {} {:<24} {:<26} {:.2f}s", tag, r.name, r.suite, r.seconds);
        if (r.status == "pass") ++pass;
        else if (r.status == "skip") ++skip;
        else ++fail;
    }
    term::outln("  {} réussi(s), {} échec(s), {} ignoré(s)", pass, fail, skip);
    return fail;
}

} // namespace build
