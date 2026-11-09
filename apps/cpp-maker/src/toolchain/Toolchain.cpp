/* @file toolchain/Toolchain.cpp */

#include "toolchain/Toolchain.hpp"

#include "core/Process.hpp"
#include "core/Util.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <thread>
#include <tuple>

#include <sys/utsname.h>
#include <unistd.h>

namespace toolchain {

std::string_view family_name(Family f) {
    switch (f) {
    case Family::Gcc: return "gcc";
    case Family::Clang: return "clang";
    case Family::Msvc: return "msvc";
    case Family::Tcc: return "tcc";
    case Family::Unknown: return "inconnue";
    }
    return "?";
}

const Tool& Toolchain::c_tool() const {
    if (cc) return *cc;
    return *cxx;
}

const Tool& Toolchain::cxx_tool() const {
    if (cxx) return *cxx;
    return *cc;
}

// --- Options -----------------------------------------------------------------

std::string include_flag(Family f, const std::string& dir) {
    return (f == Family::Msvc ? "/I" : "-I") + dir;
}

std::string define_flag(Family f, const std::string& d) {
    return (f == Family::Msvc ? "/D" : "-D") + d;
}

std::string std_flag(Family f, const std::string& standard) {
    if (f == Family::Tcc) return {};
    if (f == Family::Msvc) {
        if (standard.find("++") == std::string::npos) return {};
        std::string v = standard;
        if (v.starts_with("gnu++")) v = "c++" + v.substr(5);
        return "/std:" + v;
    }
    return "-std=" + standard;
}

std::vector<std::string> warning_flags(Family f, const std::vector<std::string>& gcc_style) {
    if (f == Family::Tcc) {
        std::vector<std::string> r;
        for (auto& w : gcc_style)
            if (w == "-Wall") r.push_back(w);
        return r;
    }
    if (f != Family::Msvc) return gcc_style;
    std::vector<std::string> r;
    for (auto& w : gcc_style) {
        if (w == "-Wall" || w == "-Wextra") util::push_unique(r, "/W4");
        else if (w == "-Wpedantic") util::push_unique(r, "/permissive-");
    }
    return r;
}

// --- Sondage -----------------------------------------------------------------

std::optional<Tool> probe(const std::string& exe, const std::vector<std::string>& pre) {
    if (!util::have(exe)) return std::nullopt;
    Tool t;
    t.exe = exe;
    t.pre_args = pre;
    std::string name = util::lower(fs::path(exe).filename().string());
    if (name == "cl" || name == "cl.exe") t.family = Family::Msvc;
    else if (name.starts_with("tcc")) t.family = Family::Tcc;

    auto r = proc::capture({exe, "--version"});
    if (t.family == Family::Unknown) {
        std::string out = util::lower(r.output);
        if (out.find("clang") != std::string::npos) t.family = Family::Clang;
        else if (out.find("gcc") != std::string::npos || out.find("free software foundation") != std::string::npos ||
                 name.find("g++") != std::string::npos || name.find("gcc") != std::string::npos)
            t.family = Family::Gcc;
        else if (out.find("tiny c") != std::string::npos) t.family = Family::Tcc;
    }
    auto nl = r.output.find('\n');
    t.version = util::trim(r.output.substr(0, nl == std::string::npos ? r.output.size() : nl));
    return t;
}

std::string dumpmachine(const Tool& t) {
    std::vector<std::string> cmd = {t.exe};
    cmd.insert(cmd.end(), t.pre_args.begin(), t.pre_args.end());
    cmd.push_back("-dumpmachine");
    auto r = proc::capture(cmd);
    if (r.exit_code != 0) return {};
    return util::trim(r.output);
}

std::string normalize_arch(std::string a) {
    a = util::lower(a);
    if (a == "amd64" || a == "x64" || a == "x86_64" || a == "em64t") return "x86_64";
    if (a == "i386" || a == "i486" || a == "i586" || a == "i686" || a == "x86" || a == "ia32") return "x86";
    if (a == "arm64" || a == "aarch64") return "aarch64";
    if (a.starts_with("armv") || a == "armhf" || a == "armel" || a == "arm") return "arm";
    if (a == "powerpc64le" || a == "ppc64le" || a == "ppc64el") return "powerpc64le";
    if (a == "loongarch" || a == "loongarch64") return "loongarch64";
    return a;
}

std::string os_from_triple(const std::string& t) {
    std::string l = util::lower(t);
    auto has = [&](std::string_view s) { return l.find(s) != std::string::npos; };
    if (has("mingw") || has("windows") || has("cygwin") || has("msvc") || has("win32")) return "windows";
    if (has("apple") || has("darwin")) return "macos";
    if (has("android")) return "android";
    if (has("freebsd")) return "freebsd";
    if (has("linux")) return "linux";
    if (has("none") || has("elf") || l.ends_with("eabi") || l.ends_with("eabihf")) return "baremetal";
    return "linux";
}

std::string host_arch() {
    utsname u{};
    if (uname(&u) == 0) return normalize_arch(u.machine);
    return "unknown";
}

std::string host_os() {
#if defined(__APPLE__)
    return "macos";
#elif defined(__FreeBSD__)
    return "freebsd";
#elif defined(__ANDROID__)
    return "android";
#else
    return "linux";
#endif
}

std::string deb_arch(const Toolchain& t) {
    auto& a = t.arch;
    if (a == "x86_64") return "amd64";
    if (a == "x86") return "i386";
    if (a == "aarch64") return "arm64";
    if (a == "arm") return t.triple.find("hf") != std::string::npos ? "armhf" : "armel";
    if (a == "powerpc64le") return "ppc64el";
    return a;
}

namespace {

std::vector<std::string> auto_runner(const Toolchain& t, std::string& note) {
    if (t.os == "windows") {
        for (auto w : {"wine64", "wine"})
            if (util::have(w)) return {w};
        note = "cible Windows : installez wine pour exécuter les binaires";
        return {};
    }
    if (t.os == "baremetal") {
        note = "cible bare-metal : exécution impossible sur l'hôte";
        return {};
    }
    if (t.os == "macos") {
        note = "cible macOS : exécution impossible sur l'hôte";
        return {};
    }
    static const std::map<std::string, std::string> qemu = {
        {"arm", "qemu-arm"}, {"aarch64", "qemu-aarch64"}, {"x86", "qemu-i386"}, {"x86_64", "qemu-x86_64"},
        {"riscv64", "qemu-riscv64"}, {"riscv32", "qemu-riscv32"}, {"mips", "qemu-mips"},
        {"mipsel", "qemu-mipsel"}, {"mips64", "qemu-mips64"}, {"mips64el", "qemu-mips64el"},
        {"powerpc", "qemu-ppc"}, {"powerpc64", "qemu-ppc64"}, {"powerpc64le", "qemu-ppc64le"},
        {"s390x", "qemu-s390x"}, {"loongarch64", "qemu-loongarch64"},
    };
    auto it = qemu.find(t.arch);
    if (it == qemu.end()) {
        note = "aucun émulateur connu pour l'architecture " + t.arch;
        return {};
    }
    std::string exe;
    for (auto cand : {it->second, it->second + "-static"})
        if (util::have(cand)) { exe = cand; break; }
    if (exe.empty()) {
        note = std::format("installez {} (paquet qemu-user) pour exécuter les binaires {}", it->second, t.arch);
        return {};
    }
    std::vector<std::string> r = {exe};
    std::string sysroot = t.cfg.sysroot;
    std::error_code ec;
    if (sysroot.empty() && !t.triple.empty() && fs::is_directory("/usr/" + t.triple, ec)) sysroot = "/usr/" + t.triple;
    if (!sysroot.empty()) {
        r.push_back("-L");
        r.push_back(sysroot);
    }
    return r;
}

} // namespace

std::vector<std::string> host_candidates(bool cxx, const std::string& family) {
    std::vector<std::string> r;
    if (const char* e = std::getenv(cxx ? "CXX" : "CC"); e && *e && family == "auto") r.push_back(e);
    if (family == "auto" || family == "gcc")
        for (auto& c : cxx ? std::vector<std::string>{"g++", "g++-15", "g++-14", "g++-13"}
                           : std::vector<std::string>{"gcc", "gcc-15", "gcc-14", "gcc-13"})
            r.push_back(c);
    if (family == "auto" || family == "clang")
        for (auto& c : cxx ? std::vector<std::string>{"clang++", "clang++-20", "clang++-19", "clang++-18"}
                           : std::vector<std::string>{"clang", "clang-20", "clang-19", "clang-18"})
            r.push_back(c);
    if (family == "auto" && !cxx) r.push_back("tcc");
    if (family == "auto") r.push_back(cxx ? "c++" : "cc");
    return r;
}

std::vector<std::string> detect_prefixes() {
    std::set<std::string> out;
    const char* env = std::getenv("PATH");
    for (auto& dir : util::split_list(env ? env : "/usr/bin", ':')) {
        std::error_code ec;
        fs::directory_iterator it(dir, ec);
        if (ec) continue;
        for (; it != fs::directory_iterator(); it.increment(ec)) {
            if (ec) break;
            auto n = it->path().filename().string();
            for (std::string suf : {"-gcc", "-g++"}) {
                if (n.size() > suf.size() && n.ends_with(suf)) {
                    auto pre = n.substr(0, n.size() - suf.size() + 1);
                    if (std::count(pre.begin(), pre.end(), '-') >= 3) out.insert(pre);
                }
            }
        }
    }
    return {out.begin(), out.end()};
}

std::vector<std::string> known_names(const model::ProjectConfig& p) {
    std::vector<std::string> r;
    for (auto& [n, t] : p.toolchains) r.push_back(n);
    util::push_unique(r, std::vector<std::string>{"host", "gcc", "clang"});
    for (auto& pre : detect_prefixes()) util::push_unique(r, pre.substr(0, pre.size() - 1));
    return r;
}

std::optional<model::ToolchainConfig> config_for(const model::ProjectConfig& p, const std::string& name) {
    if (auto it = p.toolchains.find(name); it != p.toolchains.end()) {
        auto c = it->second;
        c.name = name;
        return c;
    }
    model::ToolchainConfig c;
    c.name = name;
    if (name == "host") return c;
    if (name == "gcc") { c.cc = "gcc"; c.cxx = "g++"; c.family = "gcc"; return c; }
    if (name == "clang") { c.cc = "clang"; c.cxx = "clang++"; c.family = "clang"; return c; }
    // Compilateur précis : g++-14, clang++-19, arm-linux-gnueabihf-g++…
    for (auto [suf, is_cxx] : {std::tuple{"clang++", true}, {"clang", false}, {"g++", true}, {"gcc", false}}) {
        std::string s = suf;
        auto pos = name.rfind(s);
        if (pos == std::string::npos) continue;
        if (pos != 0 && name[pos - 1] != '-' && name[pos - 1] != '/') continue;
        std::string tail = name.substr(pos + s.size());
        if (!tail.empty() && !(tail[0] == '-' && std::all_of(tail.begin() + 1, tail.end(), ::isdigit))) continue;
        if (!util::have(name)) continue;
        std::string prefix = name.substr(0, pos);
        std::string other_base = s == "clang++" ? "clang" : s == "clang" ? "clang++" : s == "g++" ? "gcc" : "g++";
        std::string other = prefix + other_base + tail;
        if (is_cxx) { c.cxx = name; c.cc = util::have(other) ? other : "none"; }
        else { c.cc = name; c.cxx = util::have(other) ? other : "none"; }
        return c;
    }
    std::string prefix = name.ends_with("-") ? name : name + "-";
    if (util::have(prefix + "gcc") || util::have(prefix + "g++")) {
        c.prefix = prefix;
        return c;
    }
    return std::nullopt;
}

Toolchain resolve(const model::ProjectConfig& p, const std::string& name, const std::string& cc_override,
                  const std::string& cxx_override) {
    auto oc = config_for(p, name);
    if (!oc) {
        auto known = known_names(p);
        auto s = util::suggest(name, known);
        util::fail("toolchain inconnue '{}'{}\n  disponibles : {}", name,
                   s.empty() ? "" : " (vouliez-vous dire '" + s + "' ?)", util::join(known, ", "));
    }
    Toolchain t;
    t.cfg = *oc;
    auto& c = t.cfg;
    if (!cc_override.empty()) c.cc = cc_override;
    if (!cxx_override.empty()) c.cxx = cxx_override;
    t.name = name;
    if (!cc_override.empty() || !cxx_override.empty()) {
        std::string base = fs::path(!cxx_override.empty() ? cxx_override : cc_override).filename().string();
        t.name = util::is_safe_name(base) ? base : "custom";
    }
    std::vector<std::string> pre;
    if (!c.target.empty()) pre.push_back("--target=" + c.target);
    if (!c.sysroot.empty()) pre.push_back("--sysroot=" + c.sysroot);

    auto pick = [&](bool cxx) -> std::optional<Tool> {
        std::string spec = cxx ? c.cxx : c.cc;
        if (spec == "none") return std::nullopt;
        if (spec != "auto" && !spec.empty()) return probe(spec, pre);
        if (!c.prefix.empty()) {
            if (c.family == "clang") {
                auto v = pre;
                if (c.target.empty()) v.insert(v.begin(), "--target=" + c.prefix.substr(0, c.prefix.size() - 1));
                return probe(cxx ? "clang++" : "clang", v);
            }
            return probe(c.prefix + (cxx ? "g++" : "gcc"), pre);
        }
        for (auto& cand : host_candidates(cxx, c.family))
            if (auto r = probe(cand, pre)) return r;
        return std::nullopt;
    };
    t.cc = pick(false);
    t.cxx = pick(true);
    if (!t.cc && !t.cxx)
        util::fail("toolchain '{}' : aucun compilateur C ou C++ utilisable (cc = \"{}\", cxx = \"{}\"{})", name, c.cc,
                   c.cxx, c.prefix.empty() ? "" : ", prefix = \"" + c.prefix + "\"");

    auto bin = [&](const std::string& explicit_, const std::string& tool) {
        if (!explicit_.empty()) return explicit_;
        if (!c.prefix.empty() && util::have(c.prefix + tool)) return c.prefix + tool;
        const Tool& any = t.cxx ? *t.cxx : *t.cc;
        if (any.family == Family::Clang && !c.target.empty() && util::have("llvm-" + tool)) return "llvm-" + tool;
        if (any.family == Family::Gcc && tool == "ar") {
            auto exe = fs::path(any.exe).filename().string();
            auto pos = exe.find("g++");
            if (pos == std::string::npos) pos = exe.find("gcc");
            if (pos != std::string::npos) {
                std::string gar = exe.substr(0, pos) + "gcc-ar" + exe.substr(pos + 3);
                if (util::have(gar)) return gar;
            }
        }
        return tool;
    };
    t.ar = bin(c.ar, "ar");
    t.strip = bin(c.strip, "strip");
    if (!c.pkg_config.empty()) t.pkg_config = c.pkg_config;
    else if (!c.prefix.empty() && util::have(c.prefix + "pkg-config")) t.pkg_config = c.prefix + "pkg-config";
    else t.pkg_config = "pkg-config";

    t.triple = dumpmachine(t.cxx ? *t.cxx : *t.cc);
    if (t.triple.empty() && !c.target.empty()) t.triple = c.target;
    if (t.triple.empty() && !c.prefix.empty()) t.triple = c.prefix.substr(0, c.prefix.size() - 1);
    t.arch = normalize_arch(t.triple.empty() ? host_arch() : t.triple.substr(0, t.triple.find('-')));
    t.os = !c.os.empty() ? c.os : (t.triple.empty() ? host_os() : os_from_triple(t.triple));
    t.cross = t.arch != host_arch() || t.os != host_os();

    if (c.runner_set) {
        t.runner = c.runner;
        if (t.runner.empty() && t.cross) t.runner_note = "runner = [] : exécution désactivée";
    } else if (t.cross) {
        t.runner = auto_runner(t, t.runner_note);
    }
    return t;
}

bool smoke_compile(const Tool& tool, const std::string& lang, const std::string& standard, std::string* err_out,
                   const std::vector<std::string>& extra) {
    std::error_code ec;
    auto tmp = fs::temp_directory_path(ec);
    if (ec) return true;
    auto dir = tmp / std::format("cpp-maker-smoke-{}-{}", static_cast<int>(::getpid()),
                                 std::hash<std::thread::id>{}(std::this_thread::get_id()));
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    if (ec) return true;
    bool c = lang == "c";
    auto src = dir / (c ? "smoke.c" : "smoke.cpp");
    auto bin = dir / "smoke.bin";
    {
        std::ofstream f(src);
        if (c) f << "#include <stdio.h>\nint main(void) { printf(\"ok\\n\"); return 0; }\n";
        else f << "#include <iostream>\nint main() { std::cout << \"ok\\n\"; return 0; }\n";
    }
    std::vector<std::string> cmd = {tool.exe};
    cmd.insert(cmd.end(), tool.pre_args.begin(), tool.pre_args.end());
    if (auto s = std_flag(tool.family, standard); !s.empty()) cmd.push_back(s);
    cmd.insert(cmd.end(), extra.begin(), extra.end());
    cmd.insert(cmd.end(), {src.string(), "-o", bin.string()});
    auto r = proc::capture(cmd);
    fs::remove_all(dir, ec);
    if (r.exit_code == 0) return true;
    if (err_out) *err_out = r.output;
    return false;
}

} // namespace toolchain
