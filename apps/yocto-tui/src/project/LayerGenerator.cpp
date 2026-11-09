/* @file project/LayerGenerator.cpp */

#include "project/LayerGenerator.hpp"
#include "utils/Shell.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <sstream>

namespace yocto {

namespace fs = std::filesystem;

namespace {

constexpr const char* kHeader =
    "# Généré par yocto-tui : toute modification manuelle sera écrasée\n"
    "# à la prochaine application du projet (onglet « Générer & appliquer »).\n";

constexpr const char* kMitChksum =
    "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302";

/// Branches antérieures à styhead : sources locales dépaquetées dans
/// ${WORKDIR} (et non ${UNPACKDIR}).
const std::vector<std::string> kPreUnpackdir = {
    "dunfell", "gatesgarth", "hardknott", "honister", "kirkstone",
    "langdale", "mickledore", "nanbield", "scarthgap",
};

/// Branches antérieures à mickledore : pas encore de fonctionnalités
/// empty-root-password & co, seulement debug-tweaks.
const std::vector<std::string> kPreSplitDebugTweaks = {
    "dunfell", "gatesgarth", "hardknott", "honister", "kirkstone", "langdale",
};

bool branch_in(const std::string& branch, const std::vector<std::string>& list) {
    return std::any_of(list.begin(), list.end(),
                       [&](const std::string& b) { return branch.rfind(b, 0) == 0; });
}

std::string quote_value(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\n') out += ' ';
        else out += c;
    }
    return out;
}

std::vector<std::string> words(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream in(s);
    std::string w;
    while (in >> w) out.push_back(w);
    return out;
}

std::string join(const std::vector<std::string>& v, const std::string& sep) {
    std::string out;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) out += sep;
        out += v[i];
    }
    return out;
}

/// Ajoute à `v` les éléments de `extra` absents, en conservant l'ordre.
void append_unique(std::vector<std::string>& v, const std::vector<std::string>& extra) {
    for (const auto& e : extra) {
        if (std::find(v.begin(), v.end(), e) == v.end()) v.push_back(e);
    }
}

unsigned parse_mode(const std::string& m, unsigned fallback) {
    if (m.empty()) return fallback;
    try {
        return static_cast<unsigned>(std::stoul(m, nullptr, 8));
    } catch (...) {
        return fallback;
    }
}

bool is_compilable(const std::string& name) {
    auto ends = [&](const char* s) {
        const std::string suf(s);
        return name.size() > suf.size() && name.compare(name.size() - suf.size(), suf.size(), suf) == 0;
    };
    return ends(".c") || ends(".cpp") || ends(".cc");
}

bool is_cxx(const std::string& name) {
    return !(name.size() > 2 && name.compare(name.size() - 2, 2, ".c") == 0);
}

/// Dépendance d'exécution déduite du shebang d'un script.
std::string shebang_rdepend(const std::string& content) {
    if (content.rfind("#!", 0) != 0) return "";
    const std::string first = content.substr(0, content.find('\n'));
    if (first.find("python") != std::string::npos) return "python3-core";
    if (first.find("bash") != std::string::npos) return "bash";
    if (first.find("perl") != std::string::npos) return "perl";
    return "";
}

/// Bloc SRC_URI multiligne lisible.
std::string src_uri_block(const char* op, const std::vector<std::string>& uris) {
    std::ostringstream o;
    o << "SRC_URI " << op << " \" \\\n";
    for (const auto& u : uris) o << "    " << u << " \\\n";
    o << "    \"\n";
    return o.str();
}

std::string parent_var(const std::string& abs_path) {
    return LayerGenerator::install_path_var(fs::path(abs_path).parent_path().string());
}

} // namespace

// --- Aides statiques ---------------------------------------------------------

std::string LayerGenerator::unpack_var(const std::string& poky_branch) {
    return (poky_branch.empty() || branch_in(poky_branch, kPreUnpackdir)) ? "${WORKDIR}" : "${UNPACKDIR}";
}

std::string LayerGenerator::install_path_var(const std::string& abs_path) {
    // Le plus long préfixe d'abord (/usr/lib avant /usr...).
    static const std::vector<std::pair<std::string, std::string>> kVars = {
        {"/usr/libexec", "${libexecdir}"}, {"/usr/include", "${includedir}"},
        {"/usr/share", "${datadir}"},      {"/usr/sbin", "${sbindir}"},
        {"/usr/bin", "${bindir}"},         {"/usr/lib", "${libdir}"},
        {"/etc", "${sysconfdir}"},         {"/var", "${localstatedir}"},
        {"/sbin", "${base_sbindir}"},      {"/bin", "${base_bindir}"},
        {"/lib", "${base_libdir}"},
    };
    for (const auto& [prefix, var] : kVars) {
        if (abs_path == prefix) return var;
        if (abs_path.rfind(prefix + "/", 0) == 0) return var + abs_path.substr(prefix.size());
    }
    return abs_path;
}

std::string LayerGenerator::git_src_uri(const std::string& url, const std::string& branch) {
    std::string rest = url;
    std::string protocol = "https";
    const auto scheme = url.find("://");
    if (scheme != std::string::npos) {
        protocol = url.substr(0, scheme);
        rest = url.substr(scheme + 3);
    } else if (url.rfind("git@", 0) == 0) {
        // git@host:chemin/depot.git -> git://git@host/chemin/depot.git;protocol=ssh
        protocol = "ssh";
        rest = url;
        const auto colon = rest.find(':');
        if (colon != std::string::npos) rest[colon] = '/';
    }
    if (protocol != "git" && protocol != "https" && protocol != "http" && protocol != "ssh") protocol = "https";
    return "git://" + rest + ";protocol=" + protocol +
           ";branch=" + (branch.empty() ? std::string("main") : branch) + ";destsuffix=git";
}

std::string LayerGenerator::recipe_category(const std::string& recipe) {
    static const std::map<std::string, std::string> kCategories = {
        {"base-files", "recipes-core"},  {"busybox", "recipes-core"},
        {"init-ifupdown", "recipes-core"}, {"sysvinit-inittab", "recipes-core"},
        {"netbase", "recipes-core"},     {"dropbear", "recipes-core"},
        {"systemd", "recipes-core"},     {"u-boot", "recipes-bsp"},
        {"bc", "recipes-extended"},
    };
    if (auto it = kCategories.find(recipe); it != kCategories.end()) return it->second;
    if (recipe.rfind("linux-", 0) == 0) return "recipes-kernel";
    return "recipes-local";
}

std::string LayerGenerator::escape_hash(const std::string& hash) {
    std::string out;
    for (char c : hash) {
        if (c == '$') out += "\\$";
        else out += c;
    }
    return out;
}

std::string derive_salt(const std::string& seed) {
    static const char kAlphabet[] = "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::uint64_t h = 1469598103934665603ULL;  // FNV-1a
    for (unsigned char c : seed) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    std::string salt;
    for (int i = 0; i < 16; ++i) {
        h ^= h << 13;  // xorshift64
        h ^= h >> 7;
        h ^= h << 17;
        salt += kAlphabet[h % 64];
    }
    return salt;
}

std::string openssl_password_hasher(const std::string& password, const std::string& salt) {
    // printf est un builtin de sh : le mot de passe n'apparaît pas dans
    // la liste des processus.
    auto r = run_shell("printf '%s' " + shell_quote(password) +
                       " | openssl passwd -6 -salt " + shell_quote(salt) + " -stdin 2>/dev/null");
    std::string out = r.output;
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    if (r.exit_code != 0 || out.rfind("$6$", 0) != 0) return "";
    return out;
}

// --- Blocs gérés -------------------------------------------------------------

std::string merge_managed_block(const std::string& existing, const std::string& begin,
                                const std::string& end, const std::string& body) {
    std::string block;
    if (!body.empty()) {
        block = begin + "\n" + body;
        if (block.back() != '\n') block += '\n';
        block += end + "\n";
    }

    // Recherche des marqueurs en début de ligne.
    auto find_line = [&](const std::string& marker, size_t from) -> size_t {
        size_t pos = from;
        while ((pos = existing.find(marker, pos)) != std::string::npos) {
            if (pos == 0 || existing[pos - 1] == '\n') return pos;
            pos += marker.size();
        }
        return std::string::npos;
    };

    const size_t b = find_line(begin, 0);
    const size_t e = b == std::string::npos ? std::string::npos : find_line(end, b + begin.size());
    if (b != std::string::npos && e != std::string::npos) {
        size_t after = existing.find('\n', e);
        after = after == std::string::npos ? existing.size() : after + 1;
        return existing.substr(0, b) + block + existing.substr(after);
    }

    if (block.empty()) return existing;
    std::string out = existing;
    if (!out.empty()) {
        if (out.back() != '\n') out += '\n';
        out += '\n';
    }
    return out + block;
}

std::string render_managed_edit(const ManagedEdit& edit, const std::string& existing) {
    if (!edit.is_bblayers) {
        return merge_managed_block(existing, kLocalConfBegin, kLocalConfEnd, edit.block_body);
    }

    // Couches déjà déclarées hors du bloc (bitbake-layers add-layer
    // manuel, par exemple) : ne pas les répéter.
    const std::string outside = merge_managed_block(existing, kBblayersBegin, kBblayersEnd, "");
    auto declared = [&](const std::string& path) {
        size_t pos = 0;
        while ((pos = outside.find(path, pos)) != std::string::npos) {
            const size_t next = pos + path.size();
            const bool end_ok = next >= outside.size() ||
                                std::string(" \t\n\"\\").find(outside[next]) != std::string::npos;
            const bool start_ok = pos == 0 ||
                                  std::string(" \t\n\"").find(outside[pos - 1]) != std::string::npos;
            if (start_ok && end_ok) {
                // Ignore une occurrence située dans un commentaire.
                const size_t line_start = outside.rfind('\n', pos);
                const size_t ls = line_start == std::string::npos ? 0 : line_start + 1;
                const size_t first = outside.find_first_not_of(" \t", ls);
                if (first == std::string::npos || outside[first] != '#') return true;
            }
            pos = next;
        }
        return false;
    };

    std::vector<std::string> missing;
    for (const auto& p : edit.layer_paths) {
        if (!declared(p)) missing.push_back(p);
    }
    std::string body;
    if (!missing.empty()) {
        body = "BBLAYERS += \" \\\n";
        for (const auto& p : missing) body += "  " + p + " \\\n";
        body += "  \"\n";
    }
    return merge_managed_block(existing, kBblayersBegin, kBblayersEnd, body);
}

// --- Générateur --------------------------------------------------------------

LayerGenerator::LayerGenerator(YoctoConfig config, PasswordHasher hasher)
    : config_(std::move(config)), hasher_(std::move(hasher)) {
    config_.apply_defaults();
}

fs::path LayerGenerator::layer_dir() const {
    return config_.layers_dir() / config_.project.layer_name;
}

std::vector<std::string> LayerGenerator::layer_paths() const {
    std::vector<std::string> out;
    for (const auto& l : config_.project.extra_layers) {
        if (!l.enabled || l.name.empty()) continue;
        const fs::path base = config_.layers_dir() / l.name;
        const auto subs = words(l.sublayers);
        if (subs.empty()) {
            out.push_back(base.string());
        } else {
            for (const auto& s : subs) out.push_back((base / s).string());
        }
    }
    out.push_back(layer_dir().string());
    return out;
}

GenerationPlan LayerGenerator::generate() const {
    GenerationPlan plan;
    plan.layer_dir = layer_dir();
    plan.issues = validate(config_.project, config_.poky_branch);

    gen_layer_conf(plan);
    gen_apps(plan);
    gen_rootfs_and_appends(plan);
    gen_image(plan);
    gen_readme(plan);
    gen_build_conf(plan);
    return plan;
}

void LayerGenerator::gen_layer_conf(GenerationPlan& plan) const {
    const auto& p = config_.project;
    const std::string c = p.collection();
    std::ostringstream o;
    o << kHeader << "\n"
      << "BBPATH .= \":${LAYERDIR}\"\n\n"
      << "BBFILES += \"${LAYERDIR}/recipes-*/*/*.bb \\\n"
      << "            ${LAYERDIR}/recipes-*/*/*.bbappend\"\n\n"
      << "BBFILE_COLLECTIONS += \"" << c << "\"\n"
      << "BBFILE_PATTERN_" << c << " = \"^${LAYERDIR}/\"\n"
      << "BBFILE_PRIORITY_" << c << " = \"" << p.layer_priority << "\"\n\n"
      << "LAYERDEPENDS_" << c << " = \"" << quote_value(p.layer_depends) << "\"\n"
      << "LAYERSERIES_COMPAT_" << c << " = \"" << config_.poky_branch << "\"\n";
    plan.files.push_back({plan.layer_dir / "conf" / "layer.conf", o.str(), 0644, "configuration du layer"});
}

void LayerGenerator::gen_apps(GenerationPlan& plan) const {
    const std::string wd = unpack_var(config_.poky_branch);

    for (const auto& app : config_.project.apps) {
        if (!app.enabled || app.name.empty()) continue;
        const std::string origin = "recette " + app.name;
        const fs::path dir = plan.layer_dir / "recipes-apps" / app.name;

        // --- Fichiers à compiler / installer ---
        std::vector<std::string> compile_lines;
        std::vector<std::string> install_lines;
        std::vector<std::string> install_dirs;
        std::vector<std::string> packaged;
        std::vector<std::string> rdepends = words(app.rdepends);

        for (const auto& f : app.files) {
            if (app.source == SourceKind::Inline) {
                append_unique(rdepends, f.dest.empty() ? std::vector<std::string>{}
                                                       : words(shebang_rdepend(f.content)));
            }
            if (f.dest.empty()) continue;
            const std::string dest_var = install_path_var(f.dest);
            append_unique(install_dirs, {parent_var(f.dest)});
            packaged.push_back(dest_var);

            if (app.kind == AppKind::CFiles && is_compilable(f.name)) {
                const std::string out = fs::path(f.dest).filename().string();
                if (is_cxx(f.name)) {
                    compile_lines.push_back("${CXX} ${CXXFLAGS} ${LDFLAGS} ${S}/" + f.name + " -o ${B}/" + out);
                } else {
                    compile_lines.push_back("${CC} ${CFLAGS} ${LDFLAGS} ${S}/" + f.name + " -o ${B}/" + out);
                }
                install_lines.push_back("install -m 0755 ${B}/" + out + " ${D}" + dest_var);
            } else {
                install_lines.push_back("install -m " + (f.mode.empty() ? std::string("0644") : f.mode) +
                                        " ${S}/" + f.name + " ${D}" + dest_var);
            }
        }

        // --- Recette ---
        std::ostringstream r;
        r << kHeader << "\n";
        r << "SUMMARY = \"" << quote_value(app.summary.empty() ? app.name : app.summary) << "\"\n";
        r << "LICENSE = \"" << quote_value(app.license) << "\"\n";
        if (app.license != "CLOSED") {
            r << "LIC_FILES_CHKSUM = \""
              << (app.lic_files_chksum.empty() ? std::string(kMitChksum) : app.lic_files_chksum) << "\"\n";
        }
        r << "\n";

        switch (app.source) {
            case SourceKind::Inline:
            case SourceKind::HostDir:
                r << "SRC_URI = \"file://" << app.name << "/\"\n";
                r << "S = \"" << wd << "/" << app.name << "\"\n";
                break;
            case SourceKind::Git:
                r << "SRC_URI = \"" << git_src_uri(app.git_url, app.git_branch) << "\"\n";
                r << "SRCREV = \"" << app.git_srcrev << "\"\n";
                r << "PV = \"" << app.version << "+git\"\n";
                r << "S = \"" << wd << "/git\"\n";
                break;
        }
        if (app.kind == AppKind::CFiles) r << "B = \"${WORKDIR}/build\"\n";
        r << "\n";

        if (!app.depends.empty()) r << "DEPENDS += \"" << quote_value(app.depends) << "\"\n";
        if (!rdepends.empty()) r << "RDEPENDS:${PN} += \"" << join(rdepends, " ") << "\"\n";
        if (!app.depends.empty() || !rdepends.empty()) r << "\n";

        std::vector<std::string> inherits;
        if (app.kind == AppKind::CMake) inherits.push_back("cmake");
        if (!app.autostart.empty()) inherits.push_back("update-rc.d");
        if (!inherits.empty()) r << "inherit " << join(inherits, " ") << "\n\n";

        if (!app.autostart.empty()) {
            r << "INITSCRIPT_NAME = \"" << app.autostart << "\"\n";
            r << "INITSCRIPT_PARAMS = \"" << quote_value(app.autostart_params) << "\"\n\n";
        }

        auto write_install_body = [&](std::ostringstream& o) {
            for (const auto& d : install_dirs) o << "    install -d ${D}" << d << "\n";
            for (const auto& l : install_lines) o << "    " << l << "\n";
        };

        switch (app.kind) {
            case AppKind::Scripts:
                r << "do_configure[noexec] = \"1\"\n";
                r << "do_compile[noexec] = \"1\"\n\n";
                r << "do_install() {\n";
                write_install_body(r);
                r << "}\n";
                break;
            case AppKind::CFiles:
                r << "do_configure[noexec] = \"1\"\n";
                if (compile_lines.empty()) {
                    r << "do_compile[noexec] = \"1\"\n\n";
                } else {
                    r << "\n# Compilation croisée avec la chaîne de Yocto (${CC}, ${CFLAGS}, ${LDFLAGS}).\n";
                    r << "do_compile() {\n";
                    for (const auto& l : compile_lines) r << "    " << l << "\n";
                    r << "}\n\n";
                }
                r << "do_install() {\n";
                write_install_body(r);
                r << "}\n";
                break;
            case AppKind::Make:
                r << "# Transmet LDFLAGS aux Makefile qui l'ignorent (QA ldflags).\n";
                r << "TARGET_CC_ARCH += \"${LDFLAGS}\"\n";
                if (!app.extra_conf.empty()) r << "EXTRA_OEMAKE += \"" << quote_value(app.extra_conf) << "\"\n";
                r << "\ndo_compile() {\n    oe_runmake\n}\n\n";
                r << "do_install() {\n";
                r << "    oe_runmake install DESTDIR=${D} prefix=${prefix} bindir=${bindir}\n";
                write_install_body(r);
                r << "}\n";
                break;
            case AppKind::CMake:
                if (!app.extra_conf.empty()) r << "EXTRA_OECMAKE += \"" << quote_value(app.extra_conf) << "\"\n";
                if (!install_lines.empty()) {
                    r << "\ndo_install:append() {\n";
                    write_install_body(r);
                    r << "}\n";
                }
                break;
        }
        if (!packaged.empty()) r << "\nFILES:${PN} += \"" << join(packaged, " ") << "\"\n";

        plan.files.push_back({dir / (app.name + "_" + app.version + ".bb"), r.str(), 0644, origin});

        // --- Sources ---
        if (app.source == SourceKind::Inline) {
            for (const auto& f : app.files) {
                if (f.name.empty()) continue;
                plan.files.push_back({dir / "files" / app.name / f.name, f.content,
                                      parse_mode(f.mode, 0644), origin});
            }
        } else if (app.source == SourceKind::HostDir && !app.source_dir.empty()) {
            plan.copies.push_back({fs::path(app.source_dir), dir / "files" / app.name, origin});
        }
    }
}

void LayerGenerator::gen_rootfs_and_appends(GenerationPlan& plan) const {
    const auto& p = config_.project;
    const std::string wd = unpack_var(config_.poky_branch);

    struct Append {
        std::vector<std::string> uris;
        std::vector<std::string> dirs;
        std::vector<std::string> installs;
    };
    std::map<std::string, Append> appends;
    std::vector<const RootfsFile*> own;

    auto files_dir = [&](const std::string& recipe) {
        return plan.layer_dir / recipe_category(recipe) / recipe / "files";
    };

    for (const auto& f : p.rootfs_files) {
        if (!f.enabled || f.path.size() < 2 || f.path.front() != '/') continue;
        const std::string rel = "rootfs" + f.path;
        if (f.owner_recipe.empty()) {
            own.push_back(&f);
            continue;
        }
        auto& a = appends[f.owner_recipe];
        a.uris.push_back("file://" + rel);
        append_unique(a.dirs, {parent_var(f.path)});
        a.installs.push_back("install -m " + f.mode + " " + wd + "/" + rel + " ${D}" + install_path_var(f.path));
        plan.files.push_back({files_dir(f.owner_recipe) / rel, f.content, 0644,
                              "fichier " + f.path + " (" + f.owner_recipe + ")"});
    }

    for (const auto& f : p.fragments) {
        if (!f.enabled || f.name.empty() || f.recipe.empty()) continue;
        appends[f.recipe].uris.push_back("file://" + f.name);
        std::string content = f.content;
        if (!content.empty() && content.back() != '\n') content += '\n';
        plan.files.push_back({files_dir(f.recipe) / f.name, content, 0644,
                              "fragment " + f.name + " (" + f.recipe + ")"});
    }

    for (const auto& pa : p.patches) {
        if (!pa.enabled || pa.method != PatchMethod::PatchFile || pa.name.empty() || pa.recipe.empty()) continue;
        appends[pa.recipe].uris.push_back("file://" + pa.name);
        std::string content = pa.patch_content;
        // Sans Upstream-Status, la QA « patch-status » échoue (scarthgap+).
        // Le texte placé avant le diff est ignoré par patch : on l'ajoute
        // en tête si besoin.
        if (content.find("Upstream-Status:") == std::string::npos) {
            content = "Upstream-Status: " + pa.upstream_status + "\n\n" + content;
        }
        if (!content.empty() && content.back() != '\n') content += '\n';
        plan.files.push_back({files_dir(pa.recipe) / pa.name, content, 0644,
                              "patch " + pa.name + " (" + pa.recipe + ")"});
    }

    for (const auto& [recipe, a] : appends) {
        std::ostringstream o;
        o << kHeader << "\n"
          << "FILESEXTRAPATHS:prepend := \"${THISDIR}/files:\"\n\n"
          << src_uri_block("+=", a.uris);
        if (!a.installs.empty()) {
            o << "\ndo_install:append() {\n";
            for (const auto& d : a.dirs) o << "    install -d ${D}" << d << "\n";
            for (const auto& l : a.installs) o << "    " << l << "\n";
            o << "}\n";
        }
        const fs::path path = plan.layer_dir / recipe_category(recipe) / recipe / (recipe + "_%.bbappend");
        plan.files.push_back({path, o.str(), 0644, "bbappend " + recipe});
    }

    if (own.empty()) return;

    // Fichiers sans recette propriétaire : une recette dédiée.
    const std::string name = p.rootfs_recipe_name();
    const fs::path dir = plan.layer_dir / "recipes-core" / name;
    std::vector<std::string> uris;
    std::vector<std::string> dirs;
    std::vector<std::string> installs;
    std::vector<std::string> packaged;
    std::vector<std::string> conffiles;
    for (const RootfsFile* f : own) {
        uris.push_back("file://rootfs" + f->path);
        append_unique(dirs, {parent_var(f->path)});
        installs.push_back("install -m " + f->mode + " ${S}" + f->path + " ${D}" + install_path_var(f->path));
        packaged.push_back(install_path_var(f->path));
        if (f->path.rfind("/etc/", 0) == 0) conffiles.push_back(install_path_var(f->path));
        plan.files.push_back({dir / "files" / ("rootfs" + f->path), f->content, 0644, "fichier " + f->path});
    }

    std::ostringstream o;
    o << kHeader << "\n"
      << "SUMMARY = \"Fichiers Linux de la cible (projet " << p.layer_name << ")\"\n"
      << "LICENSE = \"MIT\"\n"
      << "LIC_FILES_CHKSUM = \"" << kMitChksum << "\"\n\n"
      << src_uri_block("=", uris)
      << "S = \"" << wd << "/rootfs\"\n\n"
      << "do_configure[noexec] = \"1\"\n"
      << "do_compile[noexec] = \"1\"\n\n"
      << "do_install() {\n";
    for (const auto& d : dirs) o << "    install -d ${D}" << d << "\n";
    for (const auto& l : installs) o << "    " << l << "\n";
    o << "}\n\n"
      << "FILES:${PN} += \"" << join(packaged, " ") << "\"\n";
    if (!conffiles.empty()) o << "CONFFILES:${PN} += \"" << join(conffiles, " ") << "\"\n";
    plan.files.push_back({dir / (name + "_1.0.bb"), o.str(), 0644, "recette " + name});
}

void LayerGenerator::gen_image(GenerationPlan& plan) const {
    const auto& p = config_.project;
    const auto& img = p.image;
    if (img.name.empty()) return;

    static const std::map<std::string, std::string> kBaseDirs = {
        {"core-image-minimal", "recipes-core/images"},
        {"core-image-minimal-dev", "recipes-core/images"},
        {"core-image-base", "recipes-core/images"},
        {"core-image-full-cmdline", "recipes-extended/images"},
        {"core-image-sato", "recipes-sato/images"},
        {"core-image-weston", "recipes-graphics/images"},
        {"core-image-x11", "recipes-graphics/images"},
    };

    std::ostringstream o;
    o << kHeader << "\n"
      << "SUMMARY = \"" << quote_value(img.summary.empty() ? "Image " + img.name : img.summary) << "\"\n"
      << "LICENSE = \"MIT\"\n\n";
    if (auto it = kBaseDirs.find(img.base); it != kBaseDirs.end()) {
        o << "require " << it->second << "/" << img.base << ".bb\n\n";
    } else {
        o << "# Image de base « " << img.base << " » inconnue de yocto-tui : on part de core-image.\n"
          << "inherit core-image\n\n";
    }

    std::vector<std::string> packages = words(img.extra_packages);
    for (const auto& a : p.apps) {
        if (a.enabled && a.in_image && !a.name.empty()) append_unique(packages, {a.name});
    }
    const bool has_own_rootfs = std::any_of(p.rootfs_files.begin(), p.rootfs_files.end(), [](const RootfsFile& f) {
        return f.enabled && f.owner_recipe.empty() && f.path.size() > 1 && f.path.front() == '/';
    });
    if (has_own_rootfs) append_unique(packages, {p.rootfs_recipe_name()});
    if (!packages.empty()) o << "IMAGE_INSTALL:append = \" " << join(packages, " ") << "\"\n";

    std::vector<std::string> features = words(img.features);
    if (img.root_password.empty() && img.empty_root_password) {
        if (branch_in(config_.poky_branch, kPreSplitDebugTweaks)) {
            append_unique(features, {"debug-tweaks"});
        } else {
            append_unique(features, {"empty-root-password", "allow-empty-password", "allow-root-login"});
        }
    }
    if (!features.empty()) o << "IMAGE_FEATURES:append = \" " << join(features, " ") << "\"\n";

    // --- Comptes ---
    std::vector<std::string> user_cmds;
    auto hash_for = [&](const std::string& who, const std::string& pw) -> std::string {
        const std::string h = hasher_ ? hasher_(pw, derive_salt(p.layer_name + "/" + who + "/" + pw)) : "";
        if (h.empty()) {
            plan.issues.push_back({Issue::Level::Error, "utilisateur " + who,
                                   "hash du mot de passe impossible (openssl introuvable ?)"});
        }
        return escape_hash(h);
    };
    for (const auto& u : img.users) {
        if (u.name.empty()) continue;
        std::string cmd = "useradd";
        if (!u.password.empty()) cmd += " -p '" + hash_for(u.name, u.password) + "'";
        if (!u.groups.empty()) cmd += " -G " + u.groups;
        user_cmds.push_back(cmd + " " + u.name + ";");
    }
    if (!img.root_password.empty()) {
        user_cmds.push_back("usermod -p '" + hash_for("root", img.root_password) + "' root;");
    }
    if (!user_cmds.empty()) {
        o << "\n# Mots de passe hashés en SHA-512 (« $ » échappés pour extrausers).\n"
          << "inherit extrausers\n"
          << "EXTRA_USERS_PARAMS = \"\\\n";
        for (const auto& c : user_cmds) o << "    " << c << " \\\n";
        o << "    \"\n";
    }

    plan.files.push_back({plan.layer_dir / "recipes-core" / "images" / (img.name + ".bb"), o.str(), 0644,
                          "recette d'image " + img.name});
}

void LayerGenerator::gen_readme(GenerationPlan& plan) const {
    const auto& p = config_.project;
    std::ostringstream o;
    o << "Layer " << p.layer_name << "\n"
      << std::string(6 + p.layer_name.size(), '=') << "\n\n"
      << "Généré par yocto-tui depuis la configuration « " << config_.name << " ».\n"
      << "Branche Yocto : " << config_.poky_branch << " ; machine : " << config_.machine << ".\n\n"
      << "Image : " << p.image.name << " (bitbake " << p.image.name << ")\n\n"
      << "Applications :\n";
    bool any = false;
    for (const auto& a : p.apps) {
        if (!a.enabled) continue;
        any = true;
        o << "  - " << a.name << " " << a.version << " (" << to_string(a.kind) << ", "
          << to_string(a.source) << ")" << (a.in_image ? "" : " — hors image") << "\n";
    }
    if (!any) o << "  (aucune)\n";
    o << "\nLes fichiers listés dans .yocto-tui-manifest sont régénérés à chaque\n"
         "application : modifiez le projet dans yocto-tui plutôt que ces fichiers.\n"
         "Les fichiers ajoutés à la main (ou par devtool finish) ne sont jamais supprimés.\n";
    plan.files.push_back({plan.layer_dir / "README", o.str(), 0644, "documentation"});
}

void LayerGenerator::gen_build_conf(GenerationPlan& plan) const {
    const auto& p = config_.project;

    std::ostringstream body;
    if (!p.image.hostname.empty()) body << "hostname:pn-base-files = \"" << quote_value(p.image.hostname) << "\"\n";
    if (p.rm_work) body << "INHERIT += \"rm_work\"\n";
    if (!p.local_conf_extra.empty()) {
        body << p.local_conf_extra;
        if (p.local_conf_extra.back() != '\n') body << "\n";
    }
    ManagedEdit local;
    local.path = config_.local_conf_path();
    local.block_body = body.str();
    local.origin = "local.conf (bloc yocto-tui)";
    plan.edits.push_back(local);

    ManagedEdit layers;
    layers.path = config_.build_dir_path() / "conf" / "bblayers.conf";
    layers.is_bblayers = true;
    layers.layer_paths = layer_paths();
    layers.origin = "bblayers.conf (couches du projet)";
    plan.edits.push_back(layers);
}

} // namespace yocto
