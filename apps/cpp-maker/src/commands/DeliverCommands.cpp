/* @file commands/DeliverCommands.cpp */

#include "commands/DeliverCommands.hpp"

#include "commands/Options.hpp"
#include "core/Term.hpp"
#include "core/Util.hpp"
#include "deliver/Deliver.hpp"

#include <set>

namespace commands {

namespace {
void default_release(const model::Workspace& ws, build::Request& r) {
    if (r.modes.empty()) r.modes = {ws.project.find_mode("release") ? "release" : ws.project.default_mode};
}
} // namespace

void PackageCommand::ConfigureArgs(args::Arguments& a) {
    add_build_options(a, kOptTargets | kOptToolchain | kOptJobs | kOptVerbose | kOptClean);
    a.add("--format").set_value(std::vector<std::string>{}).metavar("LISTE")
        .help("Formats : tar.gz, tar.xz, zip, deb, stage (défaut : [package].formats)");
    a.add("--no-strip").set_value(false).help("Ne pas alléger les binaires");
}

int PackageCommand::Run(Context& ctx) {
    auto& ws = ctx.workspace();
    auto r = read_request(ctx);
    default_release(ws, r);
    auto formats = ctx.get<std::vector<std::string>>("--format");
    if (formats.empty()) formats = ws.project.package.formats;
    static const std::set<std::string> ok = {"tar.gz", "tar.xz", "zip", "deb", "stage"};
    for (auto& f : formats)
        if (!ok.count(f)) ctx.usage("format inconnu '" + f + "' (tar.gz|tar.xz|zip|deb|stage)");
    std::vector<std::string> ids = r.selectors.empty() ? ws.select({"all"}, ctx.cwd()) : ws.select(r.selectors, ctx.cwd());
    for (auto& tcn : build::pick_toolchains(ws, r)) {
        for (auto& mode : r.modes) {
            auto s = build::Session::open(ws, tcn, mode, ids, false, r);
            if (s.build(r, "package") != 0) return 1;
            if (r.dry_run) continue;
            bool strip = ws.project.package.strip && !ctx.flag("--no-strip") && !deliver::mode_has_debug(s.mode);
            for (auto& f : deliver::make_packages(s, formats, strip, r.verbose)) {
                std::error_code ec;
                term::outln("  {} {:<56} {}", term::green("paquet"), util::display(f, ws.root), util::human_size(fs::file_size(f, ec)));
            }
        }
    }
    return 0;
}

void InstallCommand::ConfigureArgs(args::Arguments& a) {
    add_build_options(a, kOptTargets | kOptToolchain | kOptJobs | kOptVerbose);
    a.add("--prefix").set_value(std::string("/usr/local")).metavar("CHEMIN").help("Préfixe d'installation");
    a.add("--destdir").set_value(std::string()).metavar("CHEMIN").help("Racine alternative (<destdir><prefix>)");
    a.add("--no-strip").set_value(false).help("Ne pas alléger les binaires");
}

int InstallCommand::Run(Context& ctx) {
    auto& ws = ctx.workspace();
    auto r = read_request(ctx);
    default_release(ws, r);
    auto tcs = build::pick_toolchains(ws, r);
    if (tcs.size() != 1 || r.modes.size() != 1) ctx.usage("install : une seule toolchain et un seul mode");
    fs::path prefix = ctx.get<std::string>("--prefix");
    if (prefix.is_relative()) prefix = fs::absolute(prefix);
    auto destdir = ctx.get<std::string>("--destdir");
    fs::path target = destdir.empty() ? prefix : fs::absolute(destdir) / prefix.relative_path();
    auto ids = r.selectors.empty() ? ws.select({"all"}, ctx.cwd()) : ws.select(r.selectors, ctx.cwd());
    auto s = build::Session::open(ws, tcs[0], r.modes[0], ids, false, r);
    if (s.build(r, "install") != 0) return 1;
    if (r.dry_run) return 0;
    bool strip = ws.project.package.strip && !ctx.flag("--no-strip") && !deliver::mode_has_debug(s.mode);
    auto files = deliver::stage(s, target, strip);
    std::string manifest;
    for (auto& f : files) {
        term::outln("  installé : {}", f.string());
        manifest += f.string() + "\n";
    }
    util::write_file(s.plan->build_dir / "install_manifest.txt", manifest);
    term::ok("{} fichier(s) installé(s) dans {}", files.size(), target.string());
    return 0;
}

void DeployCommand::ConfigureArgs(args::Arguments& a) {
    add_build_options(a, kOptJobs | kOptVerbose);
}

int DeployCommand::Run(Context& ctx) {
    ctx.expect_max_positionals(1);
    auto& ws = ctx.workspace();
    std::string name = ctx.positional_or(0, "list");
    if (name == "list") {
        if (ws.project.deploys.empty()) {
            term::outln("Aucune cible de déploiement : ajoutez une table [deploy.<nom>] à project.toml");
            term::outln("  {}", term::dim("cpp-maker config set deploy.rpi.method rsync"));
            return 0;
        }
        term::outln("{}", term::bold("Cibles de déploiement"));
        for (auto& [n, d] : ws.project.deploys)
            term::outln("  {:<12} {} → {}{}{} ({} · {} · {})", term::cyan(n), d.method, d.user.empty() ? "" : d.user + "@",
                        d.host.empty() ? "" : d.host + ":", d.path,
                        d.toolchain.empty() ? ws.project.default_toolchain : d.toolchain, d.mode, d.artifact);
        return 0;
    }
    auto it = ws.project.deploys.find(name);
    if (it == ws.project.deploys.end()) {
        std::vector<std::string> names;
        for (auto& [n, d] : ws.project.deploys) names.push_back(n);
        auto s = util::suggest(name, names);
        util::fail("cible de déploiement inconnue '{}'{}", name, s.empty() ? "" : " (vouliez-vous dire '" + s + "' ?)");
    }
    const auto& d = it->second;
    build::Request r;
    r.jobs = ctx.get<int>("--jobs");
    r.keep_going = ctx.flag("--keep-going");
    r.verbose = ctx.flag("--verbose");
    r.dry_run = ctx.flag("--dry-run");
    std::vector<std::string> ids = ws.select(d.targets.empty() ? std::vector<std::string>{"all"} : d.targets, ctx.cwd());
    std::string tc = d.toolchain.empty() ? ws.project.default_toolchain : d.toolchain;
    auto s = build::Session::open(ws, tc, d.mode, ids, false, r);
    build::Request br = r;
    br.dry_run = false;   // la simulation porte sur le transfert, pas sur la compilation
    if (s.build(br, "deploy") != 0) return 1;
    return deliver::deploy(s, d, {.dry_run = r.dry_run, .verbose = r.verbose});
}

} // namespace commands
