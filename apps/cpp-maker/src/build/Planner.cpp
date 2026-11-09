/* @file build/Planner.cpp */

#include "build/Planner.hpp"

#include "build/TestSupport.hpp"
#include "core/Process.hpp"
#include "core/Util.hpp"

#include <algorithm>
#include <fstream>
#include <regex>
#include <set>

namespace build {

using model::TargetConfig;
using toolchain::Family;

std::optional<Lang> lang_of(const fs::path& p) {
    auto e = p.extension().string();
    if (e == ".c") return Lang::C;
    if (e == ".cpp" || e == ".cc" || e == ".cxx" || e == ".c++" || e == ".C" || e == ".cp") return Lang::CXX;
    if (e == ".S" || e == ".s" || e == ".sx") return Lang::ASM;
    return std::nullopt;
}

fs::path build_dir_for(const model::Workspace& ws, const std::string& tc, const std::string& mode) {
    return ws.build_path() / tc / mode;
}

bool glob_match(std::string_view pat, std::string_view s) {
    if (pat.empty()) return s.empty();
    if (pat.starts_with("**")) {
        auto rest = pat.substr(2);
        if (rest.starts_with("/")) rest.remove_prefix(1);
        for (size_t i = 0; i <= s.size(); ++i)
            if (glob_match(rest, s.substr(i))) return true;
        return false;
    }
    if (pat[0] == '*') {
        for (size_t i = 0; i <= s.size(); ++i) {
            if (glob_match(pat.substr(1), s.substr(i))) return true;
            if (i < s.size() && s[i] == '/') break;
        }
        return false;
    }
    if (s.empty()) return false;
    if (pat[0] == '?' || pat[0] == s[0]) return glob_match(pat.substr(1), s.substr(1));
    return false;
}

namespace {

bool excluded(const fs::path& tdir, const TargetConfig& t, const fs::path& p) {
    std::string rel = p.lexically_relative(tdir).generic_string();
    for (auto& e : t.exclude) {
        std::string ex = fs::path(e).lexically_normal().generic_string();
        if (rel == ex || rel.starts_with(ex + "/") || glob_match(ex, rel)) return true;
    }
    return false;
}

} // namespace

std::vector<Source> discover_sources(const model::Workspace& ws, const TargetConfig& t,
                                     const std::vector<std::string>& dirs) {
    std::vector<Source> out;
    fs::path tdir = ws.abs(t);
    for (auto& d : dirs) {
        fs::path dir = util::under(tdir, d);
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) continue;
        fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
        if (ec) continue;
        for (; it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) break;
            if (!it->is_regular_file(ec)) continue;
            auto l = lang_of(it->path());
            if (!l) continue;
            fs::path p = it->path().lexically_normal();
            if (!excluded(tdir, t, p)) out.push_back({p, *l});
        }
    }
    std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.path < b.path; });
    out.erase(std::unique(out.begin(), out.end(), [](auto& a, auto& b) { return a.path == b.path; }), out.end());
    return out;
}

bool platform_matches(const std::string& key, const std::string& os) {
    if (key == os) return true;
    if (key == "unix") return os != "windows" && os != "baremetal";
    return false;
}

std::vector<std::string> pkg_query(const toolchain::Toolchain& t, const std::string& pkg, const std::string& what) {
    std::vector<std::string> cmd = {t.pkg_config, what, pkg};
    bool restricted = t.cross && t.cfg.pkg_config.empty() && t.pkg_config == "pkg-config";
    if (restricted) {
        // Sans pkg-config dédié à la cible : on limite la recherche aux .pc de
        // la cible (sysroot, multiarch), sinon ceux de l'hôte seraient pris.
        std::vector<std::string> dirs;
        auto add = [&](const std::string& d) {
            std::error_code ec;
            if (fs::is_directory(d, ec)) dirs.push_back(d);
        };
        const std::string& sr = t.cfg.sysroot;
        if (!sr.empty())
            for (auto sub : {std::string("/usr/lib/pkgconfig"), "/usr/lib/" + t.triple + "/pkgconfig",
                             std::string("/usr/share/pkgconfig")})
                add(sr + sub);
        if (!t.triple.empty()) {
            add("/usr/lib/" + t.triple + "/pkgconfig");
            add("/usr/" + t.triple + "/lib/pkgconfig");
        }
        add("/usr/share/pkgconfig");
        cmd = {"env", "PKG_CONFIG_LIBDIR=" + util::join(dirs, ":"), "PKG_CONFIG_PATH="};
        if (!sr.empty()) cmd.push_back("PKG_CONFIG_SYSROOT_DIR=" + sr);
        cmd.insert(cmd.end(), {t.pkg_config, what, pkg});
    }
    auto r = proc::capture(cmd);
    if (r.exit_code != 0)
        util::fail("pkg-config : paquet '{}' introuvable ({} {} {}){}\n{}", pkg, t.pkg_config, what, pkg,
                   restricted ? std::format("\n  cible {} : installez le paquet de développement de la cible "
                                            "ou définissez toolchains.{}.sysroot / pkg_config", t.triple, t.name)
                              : "",
                   util::trim(r.output));
    return util::split_ws(r.output);
}

std::string artifact_name(const toolchain::Toolchain& t, const TargetConfig& m) {
    std::string base = m.artifact_base();
    if (m.is_app()) return base + t.exe_suffix();
    if (m.type == "static") return "lib" + base + ".a";
    if (m.type == "shared") {
        if (t.os == "windows") return base + ".dll";
        if (t.os == "macos") return "lib" + base + ".dylib";
        return "lib" + base + ".so";
    }
    return {};
}

// ============================================================================

namespace {

class Planner {
public:
    explicit Planner(Plan& pl) : pl_(pl), ws_(*pl.ws), p_(pl.ws->project), t_(*pl.tc), mode_(*pl.mode) {}

    void run() {
        pl_.order = ws_.topo_order(pl_.selected);
        for (auto& n : pl_.order) prepare(n);
        for (auto& n : pl_.order) emit_target(n);
        if (pl_.with_tests)
            for (auto& n : pl_.selected) emit_tests(n);
    }

private:
    Plan& pl_;
    const model::Workspace& ws_;
    const model::ProjectConfig& p_;
    const toolchain::Toolchain& t_;
    const model::ModeConfig& mode_;

    std::string rel(const fs::path& p) const { return util::display(p, ws_.root); }

    const model::LayerConfig* layer_of(const TargetConfig& t) const {
        return t.is_module() ? ws_.find_layer(t.layer) : nullptr;
    }

    std::vector<const model::PlatformOverride*> overrides(const TargetConfig& t) const {
        std::vector<const model::PlatformOverride*> r;
        if (auto* L = layer_of(t))
            for (auto& [k, o] : L->platforms)
                if (platform_matches(k, t_.os)) r.push_back(&o);
        for (auto& [k, o] : t.platforms)
            if (platform_matches(k, t_.os)) r.push_back(&o);
        return r;
    }

    void prepare(const std::string& id) {
        auto& tp = pl_.targets[id];
        const TargetConfig& t = *ws_.find(id);
        tp.cfg = &t;
        tp.closure = ws_.closure(id);
        fs::path tdir = ws_.abs(t);

        auto srcs = discover_sources(ws_, t, t.source_dirs);
        std::vector<std::string> files = t.source_files;
        for (auto* o : overrides(t)) files.insert(files.end(), o->sources.begin(), o->sources.end());
        for (auto& f : files) {
            fs::path path = util::under(tdir, f);
            auto l = lang_of(path);
            std::error_code ec;
            if (!fs::is_regular_file(path, ec)) util::fail("{}: source introuvable : {}", t.file.string(), rel(path));
            if (!l) util::fail("{}: extension non reconnue : {}", t.file.string(), rel(path));
            if (std::none_of(srcs.begin(), srcs.end(), [&](auto& s) { return s.path == path; }))
                srcs.push_back({path, *l});
        }
        if (t.is_header_only() && !srcs.empty())
            util::fail("module '{}' (header-only) : {} source(s) trouvée(s) dans {} — changez le type en static",
                       id, srcs.size(), util::join(t.source_dirs, ", "));
        if (!t.is_header_only() && srcs.empty())
            util::fail("{} '{}' : aucune source C/C++ dans {} ({})", t.kind_label(), id,
                       util::join(t.source_dirs, ", "), rel(tdir));

        fs::path main_path;
        if (t.is_app()) {
            if (!t.main.empty()) {
                main_path = util::under(tdir, t.main);
                if (std::none_of(srcs.begin(), srcs.end(), [&](auto& s) { return s.path == main_path; }))
                    util::fail("{}: main = '{}' ne fait pas partie des sources", t.file.string(), t.main);
            } else {
                for (auto& s : srcs)
                    if (s.path.stem() == "main") { main_path = s.path; break; }
                if (main_path.empty()) {
                    static const std::regex main_re(R"((^|\n)\s*int\s+main\s*\()");
                    for (auto& s : srcs) {
                        std::string text;
                        util::read_file(s.path, text);
                        if (std::regex_search(text, main_re)) { main_path = s.path; break; }
                    }
                }
                if (main_path.empty())
                    util::fail("application '{}' : aucune fonction main() trouvée (précisez [application].main)", id);
            }
        }
        for (auto& s : srcs) {
            if (s.lang == Lang::CXX) tp.has_cxx = true;
            if (s.lang == Lang::C) tp.has_c = true;
            if (!main_path.empty() && s.path == main_path) tp.main = s;
            else tp.sources.push_back(s);
        }
        tp.framework = t.test_framework.empty() ? p_.test_framework : t.test_framework;
        if (pl_.with_tests && util::contains(pl_.selected, id)) tp.tests = discover_sources(ws_, t, t.test_dirs);

        for (auto& pkg : t.all_pkg_config) {
            auto cf = pkg_query(t_, pkg, "--cflags");
            auto lf = pkg_query(t_, pkg, "--libs");
            tp.pc_cflags.insert(tp.pc_cflags.end(), cf.begin(), cf.end());
            tp.pc_libs.insert(tp.pc_libs.end(), lf.begin(), lf.end());
        }

        std::string an = artifact_name(t_, t);
        if (t.is_app()) tp.artifact = pl_.build_dir / "bin" / an;
        else if (t.type == "static") tp.artifact = pl_.build_dir / "lib" / an;
        else if (t.type == "shared") {
            if (t_.os == "windows") {
                tp.artifact = pl_.build_dir / "bin" / an;
                tp.import_lib = pl_.build_dir / "lib" / ("lib" + t.artifact_base() + ".dll.a");
            } else {
                tp.artifact = pl_.build_dir / "lib" / an;
            }
        }
    }

    bool closure_uses_cxx(const TargetPlan& tp, bool tests) const {
        if (tp.has_cxx) return true;
        if (tests && (tp.framework != "none" ||
                      std::any_of(tp.tests.begin(), tp.tests.end(), [](auto& s) { return s.lang == Lang::CXX; })))
            return true;
        for (auto& d : tp.closure)
            if (pl_.targets.at(d).has_cxx) return true;
        return false;
    }

    bool pic(const TargetConfig& t) const {
        if (t_.os == "windows") return false;
        return t.type == "shared" || (t.type == "static" && p_.static_pic);
    }

    std::vector<std::string> gtest_flags(const char* what) const {
        std::vector<std::string> r;
        auto q = proc::capture({t_.pkg_config, what, "gtest_main", "gtest"});
        if (q.exit_code == 0) return util::split_ws(q.output);
        if (std::string_view(what) == "--libs") return {"-lgtest_main", "-lgtest", "-pthread"};
        return r;
    }

    std::vector<std::string> compile_flags(const TargetPlan& tp, Lang lang, bool tests) const {
        const TargetConfig& t = *tp.cfg;
        const auto& tool = lang == Lang::C ? t_.c_tool() : t_.cxx_tool();
        Family fam = tool.family;
        std::vector<std::string> c = {tool.exe};
        c.insert(c.end(), tool.pre_args.begin(), tool.pre_args.end());
        if (lang == Lang::CXX) {
            if (auto s = toolchain::std_flag(fam, t.cxx_standard.empty() ? p_.cxx_standard : t.cxx_standard); !s.empty())
                c.push_back(s);
        } else if (lang == Lang::C) {
            if (auto s = toolchain::std_flag(fam, t.c_standard.empty() ? p_.c_standard : t.c_standard); !s.empty())
                c.push_back(s);
        }
        auto add = [&](const std::vector<std::string>& v) { c.insert(c.end(), v.begin(), v.end()); };
        auto by_lang = [&](const std::vector<std::string>& cf, const std::vector<std::string>& cxf) {
            if (lang == Lang::C) add(cf);
            if (lang == Lang::CXX) add(cxf);
        };
        const auto* L = layer_of(t);
        add(mode_.flags);
        if (lang != Lang::ASM) {
            // Avertissements propres au C++ : inutiles (et signalés) en C.
            static const std::set<std::string> cxx_only = {
                "-Wnon-virtual-dtor", "-Woverloaded-virtual", "-Wold-style-cast", "-Wuseless-cast", "-Weffc++",
                "-Wctor-dtor-privacy", "-Wsign-promo", "-Wzero-as-null-pointer-constant", "-Wextra-semi",
                "-Wsuggest-override", "-Wreorder", "-Wdeprecated-copy-dtor", "-Wmismatched-tags", "-Wredundant-tags"};
            auto warn = [&](const std::vector<std::string>& w) {
                for (auto& f : toolchain::warning_flags(fam, w))
                    if (lang != Lang::C || !cxx_only.count(f)) c.push_back(f);
            };
            warn(p_.warnings);
            if (L) warn(L->flags.warnings);
            warn(t.flags.warnings);
        }
        by_lang(t_.cfg.cflags, t_.cfg.cxxflags);
        by_lang(p_.cflags, p_.cxxflags);
        if (L) by_lang(L->flags.cflags, L->flags.cxxflags);
        by_lang(t.flags.cflags, t.flags.cxxflags);
        for (auto* o : overrides(t)) by_lang(o->cflags, o->cxxflags);
        if (pic(t)) c.push_back("-fPIC");

        auto def = [&](const std::vector<std::string>& v) {
            for (auto& d : v) c.push_back(toolchain::define_flag(fam, d));
        };
        def(p_.defines);
        def(mode_.defines);
        if (L) {
            def(L->flags.defines);
            def(L->flags.public_defines);
        }
        def(t.flags.defines);
        def(t.flags.public_defines);
        for (auto* o : overrides(t)) def(o->defines);
        for (auto& d : tp.closure) {
            const auto* dt = ws_.find(d);
            def(dt->flags.public_defines);
            if (auto* DL = layer_of(*dt); DL && DL != L) def(DL->flags.public_defines);
        }

        fs::path tdir = ws_.abs(t);
        auto inc = [&](const fs::path& base, const std::vector<std::string>& v) {
            for (auto& i : v) c.push_back(toolchain::include_flag(fam, util::under(base, i).string()));
        };
        inc(tdir, t.public_includes);
        inc(tdir, t.private_includes);
        if (tests) {
            inc(tdir, t.test_dirs);
            if (tp.framework == "builtin") c.push_back(toolchain::include_flag(fam, pl_.support_dir.string()));
        }
        for (auto& d : tp.closure) {
            const auto* dt = ws_.find(d);
            inc(ws_.abs(*dt), dt->public_includes);
        }
        add(tp.pc_cflags);
        for (auto& d : tp.closure) add(pl_.targets.at(d).pc_cflags);
        if (tests && tp.framework == "gtest") add(gtest_flags("--cflags"));
        return c;
    }

    fs::path object_path(const TargetConfig& t, const fs::path& src) const {
        fs::path tdir = ws_.abs(t);
        std::string r = src.lexically_relative(tdir).generic_string();
        if (r.empty() || r.starts_with("..")) r = util::replace_all(src.generic_string(), "/", "_");
        fs::path base = t.is_app() ? fs::path("apps") / t.name : fs::path("libs") / t.layer / t.name;
        return pl_.build_dir / "obj" / base / (r + ".o");
    }

    size_t add_compile(const TargetPlan& tp, const Source& s, bool tests, fs::path& obj_out) {
        Task task;
        task.source = s.path;
        obj_out = object_path(*tp.cfg, s.path);
        task.outputs = {obj_out};
        task.depfile = fs::path(obj_out.string() + ".d");
        task.stamp = fs::path(obj_out.string() + ".stamp");
        task.inputs = {s.path};
        task.cmd = compile_flags(tp, s.lang, tests);
        task.tool_version = (s.lang == Lang::C ? t_.c_tool() : t_.cxx_tool()).version;
        task.cmd.insert(task.cmd.end(), {"-MMD", "-MP", "-MF", task.depfile.string(), "-c", s.path.string(), "-o",
                                         obj_out.string()});
        task.kind = s.lang == Lang::CXX ? "CXX" : s.lang == Lang::C ? "CC" : "AS";
        task.label = rel(s.path);
        pl_.tasks.push_back(std::move(task));
        return pl_.tasks.size() - 1;
    }

    void link_libs(const TargetPlan& tp, std::vector<std::string>& cmd, std::vector<fs::path>& inputs,
                   std::vector<size_t>& deps, bool& rpath) const {
        for (auto it = tp.closure.rbegin(); it != tp.closure.rend(); ++it) {
            auto& dp = pl_.targets.at(*it);
            if (dp.cfg->is_header_only()) continue;
            fs::path lib = dp.import_lib.empty() ? dp.artifact : dp.import_lib;
            cmd.push_back(lib.string());
            inputs.push_back(lib);
            if (dp.link_task) deps.push_back(*dp.link_task);
            if (dp.cfg->type == "shared") rpath = true;
        }
        cmd.insert(cmd.end(), tp.pc_libs.begin(), tp.pc_libs.end());
        for (auto& d : tp.closure) {
            auto& dl = pl_.targets.at(d).pc_libs;
            cmd.insert(cmd.end(), dl.begin(), dl.end());
        }
        std::vector<std::string> sys = tp.cfg->all_system_libs;
        for (auto* o : overrides(*tp.cfg)) sys.insert(sys.end(), o->system_libs.begin(), o->system_libs.end());
        for (auto& d : tp.closure) {
            const auto* dt = ws_.find(d);
            sys.insert(sys.end(), dt->all_system_libs.begin(), dt->all_system_libs.end());
            for (auto* o : overrides(*dt)) sys.insert(sys.end(), o->system_libs.begin(), o->system_libs.end());
        }
        std::set<std::string> seen;
        for (auto& l : sys)
            if (seen.insert(l).second) cmd.push_back(l.starts_with("-") ? l : "-l" + l);
        if (t_.os == "macos")
            for (auto& fw : tp.cfg->deps.frameworks) {
                cmd.push_back("-framework");
                cmd.push_back(fw);
            }
    }

    const toolchain::Tool& link_tool(const TargetPlan& tp, bool tests) const {
        return closure_uses_cxx(tp, tests) ? t_.cxx_tool() : t_.c_tool();
    }

    std::vector<std::string> link_prefix(const TargetPlan& tp, bool tests) const {
        const auto& tool = link_tool(tp, tests);
        std::vector<std::string> c = {tool.exe};
        c.insert(c.end(), tool.pre_args.begin(), tool.pre_args.end());
        auto add = [&](const std::vector<std::string>& v) { c.insert(c.end(), v.begin(), v.end()); };
        add(mode_.link_flags);
        add(t_.cfg.ldflags);
        add(p_.ldflags);
        if (auto* L = layer_of(*tp.cfg)) add(L->flags.ldflags);
        add(tp.cfg->flags.ldflags);
        for (auto* o : overrides(*tp.cfg)) add(o->ldflags);
        return c;
    }

    void add_rpath(std::vector<std::string>& cmd) const {
        if (t_.os == "windows" || t_.os == "baremetal") return;
        cmd.push_back(t_.os == "macos" ? "-Wl,-rpath,@loader_path/../lib" : "-Wl,-rpath,$ORIGIN/../lib");
    }

    void emit_target(const std::string& id) {
        auto& tp = pl_.targets[id];
        const TargetConfig& t = *tp.cfg;
        for (auto& s : tp.sources) {
            fs::path obj;
            tp.object_tasks.push_back(add_compile(tp, s, false, obj));
            tp.objects.push_back(obj);
        }
        if (tp.main) tp.main_task = add_compile(tp, *tp.main, false, tp.main_object);
        if (t.is_header_only()) return;

        Task task;
        task.outputs = {tp.artifact};
        task.stamp = pl_.build_dir / "stamps" / (id + ".link.stamp");
        task.label = rel(tp.artifact);
        std::vector<fs::path> objs = tp.objects;
        std::vector<size_t> deps = tp.object_tasks;
        if (tp.main) {
            objs.push_back(tp.main_object);
            deps.push_back(*tp.main_task);
        }
        if (t.type == "static") {
            task.kind = "AR";
            task.cmd = {t_.ar, "rcs", tp.artifact.string()};
            for (auto& o : objs) task.cmd.push_back(o.string());
            task.inputs = objs;
            task.remove_outputs_first = true;
            task.tool_version = t_.ar;
        } else {
            task.kind = t.type == "shared" ? "SOLINK" : "LINK";
            task.cmd = link_prefix(tp, false);
            if (t.type == "shared") {
                task.cmd.push_back(t_.os == "macos" ? "-dynamiclib" : "-shared");
                if (t_.os == "windows") {
                    task.cmd.push_back("-Wl,--out-implib," + tp.import_lib.string());
                    task.outputs.push_back(tp.import_lib);
                } else if (t_.os == "macos") {
                    task.cmd.push_back("-Wl,-install_name,@rpath/" + tp.artifact.filename().string());
                } else {
                    task.cmd.push_back("-Wl,-soname," + tp.artifact.filename().string());
                }
            }
            for (auto& o : objs) task.cmd.push_back(o.string());
            task.inputs = objs;
            bool rpath = false;
            link_libs(tp, task.cmd, task.inputs, deps, rpath);
            if (rpath) add_rpath(task.cmd);
            task.cmd.push_back("-o");
            task.cmd.push_back(tp.artifact.string());
            task.tool_version = link_tool(tp, false).version;
        }
        task.deps = deps;
        pl_.tasks.push_back(std::move(task));
        tp.link_task = pl_.tasks.size() - 1;
    }

    size_t test_main_task(const TargetPlan& tp) {
        if (pl_.test_main_task) return *pl_.test_main_task;
        if (!t_.cxx) util::fail("toolchain '{}' : un compilateur C++ est requis pour le framework de test intégré", t_.name);
        Source s{pl_.support_dir / "cpp-maker" / "test_main.cpp", Lang::CXX};
        Task task;
        task.source = s.path;
        pl_.test_main_object = pl_.build_dir / "obj" / "support" / "test_main.o";
        task.outputs = {pl_.test_main_object};
        task.depfile = fs::path(pl_.test_main_object.string() + ".d");
        task.stamp = fs::path(pl_.test_main_object.string() + ".stamp");
        task.inputs = {s.path};
        const auto& tool = t_.cxx_tool();
        task.cmd = {tool.exe};
        task.cmd.insert(task.cmd.end(), tool.pre_args.begin(), tool.pre_args.end());
        if (auto st = toolchain::std_flag(tool.family, p_.cxx_standard); !st.empty()) task.cmd.push_back(st);
        task.cmd.insert(task.cmd.end(), mode_.flags.begin(), mode_.flags.end());
        task.cmd.push_back(toolchain::include_flag(tool.family, pl_.support_dir.string()));
        task.cmd.insert(task.cmd.end(), {"-MMD", "-MP", "-MF", task.depfile.string(), "-c", s.path.string(), "-o",
                                         pl_.test_main_object.string()});
        task.kind = "CXX";
        task.label = "cpp-maker/test_main.cpp";
        task.tool_version = tool.version;
        (void)tp;
        pl_.tasks.push_back(std::move(task));
        pl_.test_main_task = pl_.tasks.size() - 1;
        return *pl_.test_main_task;
    }

    void emit_tests(const std::string& id) {
        auto& tp = pl_.targets[id];
        const TargetConfig& t = *tp.cfg;
        if (tp.tests.empty() || tp.framework.empty()) return;
        std::vector<fs::path> objs;
        std::vector<size_t> deps;
        for (auto& s : tp.tests) {
            fs::path obj;
            deps.push_back(add_compile(tp, s, true, obj));
            objs.push_back(obj);
        }
        if (tp.framework == "builtin") {
            deps.push_back(test_main_task(tp));
            objs.push_back(pl_.test_main_object);
        }
        Task task;
        task.kind = "LINK";
        tp.test_exe = pl_.build_dir / "tests" / (id + "-tests" + t_.exe_suffix());
        task.outputs = {tp.test_exe};
        task.stamp = pl_.build_dir / "stamps" / (id + ".tests.stamp");
        task.label = rel(tp.test_exe);
        task.cmd = link_prefix(tp, true);
        for (auto& o : objs) task.cmd.push_back(o.string());
        task.inputs = objs;
        bool rpath = false;
        if (t.is_app()) {
            // Objets de l'application, sauf celui qui contient main().
            for (auto& o : tp.objects) {
                task.cmd.push_back(o.string());
                task.inputs.push_back(o);
            }
            deps.insert(deps.end(), tp.object_tasks.begin(), tp.object_tasks.end());
        } else if (!t.is_header_only()) {
            fs::path lib = tp.import_lib.empty() ? tp.artifact : tp.import_lib;
            task.cmd.push_back(lib.string());
            task.inputs.push_back(lib);
            deps.push_back(*tp.link_task);
            if (t.type == "shared") rpath = true;
        }
        link_libs(tp, task.cmd, task.inputs, deps, rpath);
        if (tp.framework == "gtest") {
            auto g = gtest_flags("--libs");
            task.cmd.insert(task.cmd.end(), g.begin(), g.end());
        }
        if (rpath) add_rpath(task.cmd);
        task.cmd.push_back("-o");
        task.cmd.push_back(tp.test_exe.string());
        task.tool_version = link_tool(tp, true).version;
        task.deps = deps;
        pl_.tasks.push_back(std::move(task));
        tp.test_task = pl_.tasks.size() - 1;
    }
};

} // namespace

Plan make_plan(const model::Workspace& ws, const toolchain::Toolchain& tc, const model::ModeConfig& mode,
               const std::vector<std::string>& selected, bool with_tests) {
    Plan pl;
    pl.ws = &ws;
    pl.tc = &tc;
    pl.mode = &mode;
    pl.build_dir = build_dir_for(ws, tc.name, mode.name);
    pl.selected = selected;
    pl.with_tests = with_tests;
    if (with_tests) pl.support_dir = ensure_test_support(pl.build_dir);
    Planner(pl).run();
    return pl;
}

} // namespace build
