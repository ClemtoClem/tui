/* @file deliver/Deliver.cpp */

#include "deliver/Deliver.hpp"

#include "core/Process.hpp"
#include "core/Term.hpp"

#include <algorithm>
#include <map>

namespace deliver {

using model::TargetConfig;

bool mode_has_debug(const model::ModeConfig& m) {
    return std::any_of(m.flags.begin(), m.flags.end(), [](auto& f) { return f.starts_with("-g") && f != "-g0"; });
}

namespace {

void copy_into(const fs::path& src, const fs::path& dst_dir, std::vector<fs::path>& installed) {
    std::error_code ec;
    fs::create_directories(dst_dir, ec);
    if (ec) util::fail("impossible de créer {} : {}", dst_dir.string(), ec.message());
    fs::path dst = dst_dir / src.filename();
    if (fs::is_directory(src, ec)) {
        fs::copy(src, dst, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        if (ec) util::fail("copie de {} → {} : {}", src.string(), dst.string(), ec.message());
        for (auto& e : fs::recursive_directory_iterator(dst, ec))
            if (e.is_regular_file()) installed.push_back(e.path());
    } else {
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
        if (ec) util::fail("copie de {} → {} : {}", src.string(), dst.string(), ec.message());
        installed.push_back(dst);
    }
}

void copy_headers(const fs::path& inc_dir, const fs::path& dst, std::vector<fs::path>& installed) {
    std::error_code ec;
    if (!fs::is_directory(inc_dir, ec)) return;
    for (auto& e : fs::directory_iterator(inc_dir, ec)) copy_into(e.path(), dst, installed);
}

std::string pc_file(const model::Workspace& ws, const TargetConfig& t) {
    auto& p = ws.project;
    std::string s = "prefix=${pcfiledir}/../..\nexec_prefix=${prefix}\nlibdir=${prefix}/lib\n"
                    "includedir=${prefix}/include\n\n";
    s += "Name: " + t.artifact_base() + "\n";
    s += "Description: " + (t.description.empty() ? t.id + " (" + p.name + ")" : t.description) + "\n";
    s += "Version: " + (t.version.empty() ? p.version : t.version) + "\n";
    std::vector<std::string> req = t.all_pkg_config;
    for (auto& d : t.dep_ids)
        if (auto* dt = ws.find(d); dt && dt->install) req.push_back(dt->artifact_base());
    if (!req.empty()) s += "Requires: " + util::join(req, ", ") + "\n";
    if (!t.is_header_only()) s += "Libs: -L${libdir} -l" + t.artifact_base() + "\n";
    std::vector<std::string> priv;
    for (auto& l : t.all_system_libs) priv.push_back(l.starts_with("-") ? l : "-l" + l);
    if (!priv.empty()) s += "Libs.private: " + util::join(priv, " ") + "\n";
    std::string cflags = "-I${includedir}";
    for (auto& d : t.flags.public_defines) cflags += " -D" + d;
    s += "Cflags: " + cflags + "\n";
    return s;
}

void run_checked(const std::vector<std::string>& cmd, bool verbose, const std::optional<fs::path>& cwd = std::nullopt) {
    if (verbose) term::outln("      $ {}{}", cwd ? "(cd " + cwd->string() + ") " : "", util::shell_join(cmd));
    auto r = proc::run(cmd, {.capture = true, .cwd = cwd});
    if (r.exit_code != 0)
        util::fail("commande en échec (code {}) : {}\n{}", r.exit_code, util::shell_join(cmd), util::trim(r.output));
}

std::string deb_name(const model::ProjectConfig& p) {
    std::string pkg;
    for (char ch : util::lower(p.name))
        pkg += (std::isalnum(static_cast<unsigned char>(ch)) || ch == '.' || ch == '+') ? ch : '-';
    return pkg;
}

std::string deb_filename(const model::ProjectConfig& p, const toolchain::Toolchain& t) {
    return std::format("{}_{}_{}.deb", deb_name(p), p.version, toolchain::deb_arch(t));
}

fs::path make_deb(const build::Session& s, const fs::path& dist, bool strip, bool verbose) {
    if (!util::have("dpkg-deb")) util::fail("dpkg-deb introuvable : format deb indisponible");
    auto& p = s.ws->project;
    if (p.version.empty() || !std::isdigit(static_cast<unsigned char>(p.version[0])))
        util::fail("format deb : project.version doit commencer par un chiffre");
    std::string pkg = deb_name(p), arch = toolchain::deb_arch(*s.tc);
    fs::path root = s.plan->build_dir / "deb" / std::format("{}_{}_{}", pkg, p.version, arch);
    std::error_code ec;
    fs::remove_all(root, ec);
    auto files = stage(s, root / fs::path(p.package.deb_prefix).relative_path(), strip);
    uintmax_t kb = 0;
    for (auto& f : files) kb += fs::file_size(f, ec) / 1024 + 1;
    std::string control = std::format("Package: {}\nVersion: {}\nArchitecture: {}\nMaintainer: {}\nInstalled-Size: {}\n"
                                      "Section: misc\nPriority: optional\n",
                                      pkg, p.version, arch, p.package.maintainer, kb);
    if (!p.package.deb_depends.empty()) control += "Depends: " + util::join(p.package.deb_depends, ", ") + "\n";
    if (!p.package.homepage.empty()) control += "Homepage: " + p.package.homepage + "\n";
    control += "Description: " + (p.package.description.empty() ? p.name : p.package.description) + "\n";
    util::write_file(root / "DEBIAN" / "control", control);
    fs::permissions(root / "DEBIAN",
                    fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec | fs::perms::others_read |
                        fs::perms::others_exec,
                    ec);
    fs::path out = dist / deb_filename(p, *s.tc);
    run_checked({"dpkg-deb", "--build", "--root-owner-group", root.string(), out.string()}, verbose);
    return out;
}

std::string expand(std::string s, const std::map<std::string, std::string>& vars) {
    for (auto& [k, v] : vars) s = util::replace_all(s, "{" + k + "}", v);
    return s;
}

} // namespace

std::vector<fs::path> stage(const build::Session& s, const fs::path& dest, bool strip) {
    std::vector<fs::path> installed, to_strip;
    const auto& t = *s.tc;
    const auto& ws = *s.ws;
    for (auto& id : s.plan->order) {
        auto& tp = s.plan->targets.at(id);
        const TargetConfig& c = *tp.cfg;
        if (!c.install) continue;
        bool selected = util::contains(s.plan->selected, id);
        const char* so_dir = t.os == "windows" ? "bin" : "lib";
        if (!selected) {
            // Dépendance tirée par une cible demandée : seul le runtime est utile.
            if (c.type == "shared") {
                copy_into(tp.artifact, dest / so_dir, installed);
                to_strip.push_back(installed.back());
            }
            continue;
        }
        if (c.is_app()) {
            copy_into(tp.artifact, dest / "bin", installed);
            to_strip.push_back(installed.back());
            continue;
        }
        if (c.type == "shared") {
            copy_into(tp.artifact, dest / so_dir, installed);
            to_strip.push_back(installed.back());
            if (!tp.import_lib.empty()) copy_into(tp.import_lib, dest / "lib", installed);
        } else if (c.type == "static") {
            copy_into(tp.artifact, dest / "lib", installed);
        }
        for (auto& inc : c.public_includes) copy_headers(util::under(ws.abs(c), inc), dest / "include", installed);
        fs::path pc = dest / "lib" / "pkgconfig" / (c.artifact_base() + ".pc");
        util::write_file(pc, pc_file(ws, c));
        installed.push_back(pc);
    }
    std::error_code ec;
    for (auto& e : fs::directory_iterator(ws.root, ec)) {
        auto up = util::upper(e.path().filename().string());
        if (e.is_regular_file() && (up.starts_with("README") || up.starts_with("LICENSE") ||
                                    up.starts_with("COPYING") || up.starts_with("CHANGELOG")))
            copy_into(e.path(), dest / "share" / "doc" / ws.project.name, installed);
    }
    for (auto& f : ws.project.package.extra_files) {
        fs::path src = util::under(ws.root, f);
        if (!fs::exists(src, ec)) util::fail("package.extra_files : introuvable : {}", f);
        copy_into(src, dest / "share" / "doc" / ws.project.name, installed);
    }
    if (strip && !to_strip.empty()) {
        if (!util::have(t.strip)) {
            term::warn("{} introuvable : binaires non allégés", t.strip);
        } else {
            for (auto& f : to_strip) {
                bool lib = f.extension() == ".so" || f.extension() == ".dll" || f.extension() == ".dylib";
                std::vector<std::string> cmd = {t.strip};
                if (lib) cmd.push_back("--strip-unneeded");
                cmd.push_back(f.string());
                auto r = proc::capture(cmd);
                if (r.exit_code != 0) term::warn("strip {} : {}", f.string(), util::trim(r.output));
            }
        }
    }
    return installed;
}

std::string package_basename(const model::Workspace& ws, const toolchain::Toolchain& t) {
    return std::format("{}-{}-{}-{}", ws.project.name, ws.project.version, t.os, t.arch);
}

std::vector<fs::path> make_packages(const build::Session& s, const std::vector<std::string>& formats, bool strip,
                                    bool verbose, fs::path* stage_out) {
    const auto& ws = *s.ws;
    std::string base = package_basename(ws, *s.tc);
    fs::path stage_parent = s.plan->build_dir / "stage";
    fs::path stage_dir = stage_parent / base;
    std::error_code ec;
    fs::remove_all(stage_dir, ec);
    fs::create_directories(stage_dir, ec);
    auto files = stage(s, stage_dir, strip);
    term::info("staging : {} ({} fichier(s))", util::display(stage_dir, ws.root), files.size());
    if (stage_out) *stage_out = stage_dir;

    fs::path dist = ws.dist_path();
    fs::create_directories(dist, ec);
    std::vector<fs::path> out;
    for (auto& f : formats) {
        if (f == "tar.gz" || f == "tar.xz") {
            if (!util::have("tar")) util::fail("tar introuvable");
            fs::path a = dist / (base + "." + f);
            run_checked({"tar", "-C", stage_parent.string(), f == "tar.gz" ? "-czf" : "-cJf", a.string(), base}, verbose);
            out.push_back(a);
        } else if (f == "zip") {
            if (!util::have("zip")) util::fail("zip introuvable : format zip indisponible");
            fs::path a = dist / (base + ".zip");
            fs::remove(a, ec);
            run_checked({"zip", "-qry", a.string(), base}, verbose, stage_parent);
            out.push_back(a);
        } else if (f == "deb") {
            out.push_back(make_deb(s, dist, strip, verbose));
        } else if (f != "stage") {
            util::fail("format de paquet inconnu '{}' (tar.gz|tar.xz|zip|deb|stage)", f);
        }
    }
    if (ws.project.package.checksums && !out.empty() && util::have("sha256sum")) {
        std::vector<std::string> cmd = {"sha256sum"};
        for (auto& f : out) cmd.push_back(f.filename().string());
        auto r = proc::run(cmd, {.capture = true, .cwd = dist});
        if (r.exit_code == 0) {
            fs::path sums = dist / (base + ".sha256");
            util::write_file(sums, r.output);
            out.push_back(sums);
        }
    }
    return out;
}

int deploy(const build::Session& s, const model::DeployConfig& d, const DeployOptions& o) {
    const auto& ws = *s.ws;
    std::string target = (d.user.empty() ? "" : d.user + "@") + d.host;
    std::vector<std::string> ssh = {"ssh"};
    if (d.port) ssh.insert(ssh.end(), {"-p", std::to_string(d.port)});
    ssh.insert(ssh.end(), d.ssh_options.begin(), d.ssh_options.end());

    std::map<std::string, std::string> vars = {{"name", ws.project.name}, {"version", ws.project.version},
                                               {"toolchain", s.tc->name}, {"mode", s.mode.name},
                                               {"host", d.host}, {"artifact", ""}};
    std::string path = expand(d.path, vars);
    vars["path"] = path;

    auto exec = [&](const std::vector<std::string>& cmd) {
        term::outln("  {} {}", term::magenta("$"), util::shell_join(cmd));
        if (o.dry_run) return;
        auto r = proc::run(cmd, {.cwd = ws.root});
        if (r.exit_code != 0) util::fail("déploiement interrompu (code {})", r.exit_code);
    };

    term::info("déploiement '{}' : {} → {}{}", d.name, d.method, target.empty() ? "" : target + ":", path);
    bool strip = ws.project.package.strip && !mode_has_debug(s.mode);
    fs::path stage_dir;
    std::vector<fs::path> packages;
    std::vector<std::string> fmts;
    if (d.artifact != "stage") fmts.push_back(d.artifact);
    if (!o.dry_run) {
        packages = make_packages(s, fmts, strip, o.verbose, &stage_dir);
        std::erase_if(packages, [](auto& f) { return f.extension() == ".sha256"; });
    } else {
        stage_dir = s.plan->build_dir / "stage" / package_basename(ws, *s.tc);
        if (d.artifact == "deb") packages.push_back(ws.dist_path() / deb_filename(ws.project, *s.tc));
        else if (d.artifact != "stage") packages.push_back(ws.dist_path() / (package_basename(ws, *s.tc) + "." + d.artifact));
    }
    if (!packages.empty()) vars["artifact"] = path + "/" + packages[0].filename().string();
    for (auto& b : d.before) exec({"/bin/sh", "-c", expand(b, vars)});

    std::vector<fs::path> items;
    if (d.artifact == "stage") {
        std::error_code ec;
        if (!o.dry_run)
            for (auto& e : fs::directory_iterator(stage_dir, ec)) items.push_back(e.path());
        else items.push_back(stage_dir / "*");
    } else {
        items = packages;
    }

    if (d.method == "script") {
        exec({"/bin/sh", "-c", "CPP_MAKER_DIST=" + util::shell_quote(stage_dir.string()) + "; export CPP_MAKER_DIST; " +
                                   expand(d.script, vars)});
    } else if (d.method == "copy") {
        fs::path dest = util::under(ws.root, path);
        if (!o.dry_run) {
            std::vector<fs::path> done;
            for (auto& i : items) copy_into(i, dest, done);
            term::outln("  {} fichier(s) copié(s) dans {}", done.size(), dest.string());
        } else {
            for (auto& i : items) term::outln("  [simulation] copie {} → {}", i.string(), dest.string());
        }
        for (auto& a : d.after) exec({"/bin/sh", "-c", expand(a, vars)});
    } else {
        auto mk = ssh;
        mk.push_back(target);
        mk.push_back("mkdir -p " + util::shell_quote(path));
        exec(mk);
        if (d.method == "rsync") {
            std::vector<std::string> cmd = {"rsync", "-az"};
            if (d.delete_extra) cmd.push_back("--delete");
            if (o.verbose) cmd.push_back("-v");
            cmd.insert(cmd.end(), {"-e", util::shell_join(ssh)});
            if (d.artifact == "stage") cmd.push_back(stage_dir.string() + "/");
            else
                for (auto& i : items) cmd.push_back(i.string());
            cmd.push_back(target + ":" + path + "/");
            exec(cmd);
        } else {
            std::vector<std::string> cmd = {"scp", "-r"};
            if (d.port) cmd.insert(cmd.end(), {"-P", std::to_string(d.port)});
            cmd.insert(cmd.end(), d.ssh_options.begin(), d.ssh_options.end());
            for (auto& i : items) cmd.push_back(i.string());
            cmd.push_back(target + ":" + path + "/");
            exec(cmd);
        }
        for (auto& a : d.after) {
            auto cmd = ssh;
            cmd.push_back(target);
            cmd.push_back(expand(a, vars));
            exec(cmd);
        }
    }
    for (auto& a : d.after_local) exec({"/bin/sh", "-c", expand(a, vars)});
    term::ok("déploiement '{}' {}", d.name, o.dry_run ? "simulé" : "terminé");
    return 0;
}

} // namespace deliver
