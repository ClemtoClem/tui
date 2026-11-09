/* @file project/ProjectSpec.cpp */

#include "project/ProjectSpec.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <sstream>
#include <utility>

namespace yocto {

namespace {

bool parse_bool(const std::string& v) {
    std::string s;
    for (char c : v) s += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s == "true" || s == "yes" || s == "on" || s == "1";
}

const char* bool_str(bool b) { return b ? "true" : "false"; }

/// Découpe "a|b|c|d" en au plus `n` champs : le dernier garde le reste.
/// Les '|' des valeurs sont échappés par ini_escape(), le découpage sur
/// le caractère brut est donc sûr.
std::vector<std::string> split_fields(const std::string& s, size_t n) {
    std::vector<std::string> out;
    size_t start = 0;
    while (out.size() + 1 < n) {
        const size_t bar = s.find('|', start);
        if (bar == std::string::npos) break;
        out.push_back(s.substr(start, bar - start));
        start = bar + 1;
    }
    out.push_back(s.substr(start));
    return out;
}

bool is_octal_mode(const std::string& m) {
    if (m.size() < 3 || m.size() > 4) return false;
    return std::all_of(m.begin(), m.end(), [](char c) { return c >= '0' && c <= '7'; });
}

bool is_safe_relative(const std::string& p) {
    if (p.empty() || p.front() == '/') return false;
    if (p.find('|') != std::string::npos) return false;
    std::istringstream in(p);
    std::string part;
    while (std::getline(in, part, '/')) {
        if (part == ".." || part.empty()) return false;
    }
    return true;
}

bool is_safe_absolute(const std::string& p) {
    return p.size() > 1 && p.front() == '/' && is_safe_relative(p.substr(1));
}

bool is_valid_user_name(const std::string& n) {
    if (n.empty() || n.size() > 32) return false;
    if (!(std::islower(static_cast<unsigned char>(n[0])) || n[0] == '_')) return false;
    return std::all_of(n.begin(), n.end(), [](char c) {
        return std::islower(static_cast<unsigned char>(c)) ||
               std::isdigit(static_cast<unsigned char>(c)) || c == '_' || c == '-';
    });
}

bool has_suffix(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

} // namespace

// --- Enums <-> texte -------------------------------------------------------

const char* to_string(AppKind k) {
    switch (k) {
        case AppKind::Scripts: return "scripts";
        case AppKind::CFiles:  return "c";
        case AppKind::Make:    return "make";
        case AppKind::CMake:   return "cmake";
    }
    return "c";
}

const char* to_string(SourceKind k) {
    switch (k) {
        case SourceKind::Inline:  return "inline";
        case SourceKind::HostDir: return "hostdir";
        case SourceKind::Git:     return "git";
    }
    return "inline";
}

const char* to_string(PatchMethod m) {
    return m == PatchMethod::PatchFile ? "patchfile" : "devtool";
}

AppKind app_kind_from_string(const std::string& s) {
    if (s == "scripts") return AppKind::Scripts;
    if (s == "make") return AppKind::Make;
    if (s == "cmake") return AppKind::CMake;
    return AppKind::CFiles;
}

SourceKind source_kind_from_string(const std::string& s) {
    if (s == "hostdir") return SourceKind::HostDir;
    if (s == "git") return SourceKind::Git;
    return SourceKind::Inline;
}

PatchMethod patch_method_from_string(const std::string& s) {
    return s == "patchfile" ? PatchMethod::PatchFile : PatchMethod::Devtool;
}

// --- Échappement -------------------------------------------------------

std::string ini_escape(const std::string& s) {
    // Le parseur INI supprime les espaces de début et de fin : on les
    // échappe (\s) pour préserver, par exemple, l'indentation d'un motd.
    size_t lead = 0;
    while (lead < s.size() && s[lead] == ' ') ++lead;
    size_t trail = 0;
    while (trail < s.size() - lead && s[s.size() - 1 - trail] == ' ') ++trail;

    std::string out;
    out.reserve(s.size() + 8);
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == ' ' && (i < lead || i >= s.size() - trail)) { out += "\\s"; continue; }
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '|':  out += "\\p"; break;
            default:   out += c; break;
        }
    }
    return out;
}

std::string ini_unescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\' || i + 1 >= s.size()) { out += s[i]; continue; }
        const char n = s[++i];
        switch (n) {
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 's': out += ' '; break;
            case 'p': out += '|'; break;
            case '\\': out += '\\'; break;
            default:  out += '\\'; out += n; break;
        }
    }
    return out;
}

// --- Noms dérivés ------------------------------------------------------

std::string ProjectSpec::collection() const {
    std::string c = layer_name;
    if (c.rfind("meta-", 0) == 0) c = c.substr(5);
    if (c.empty()) c = "custom";
    return c;
}

std::string ProjectSpec::rootfs_recipe_name() const {
    return collection() + "-rootfs-files";
}

// --- Sérialisation -----------------------------------------------------

std::string ProjectSpec::to_ini() const {
    std::ostringstream o;
    auto kv = [&o](const char* k, const std::string& v) { o << k << " = " << ini_escape(v) << "\n"; };

    o << "\n[project]\n";
    kv("layer_name", layer_name);
    o << "layer_priority = " << layer_priority << "\n";
    kv("layer_depends", layer_depends);
    kv("local_conf_extra", local_conf_extra);
    o << "rm_work = " << bool_str(rm_work) << "\n";

    o << "\n[image]\n";
    kv("name", image.name);
    kv("base", image.base);
    kv("summary", image.summary);
    kv("hostname", image.hostname);
    kv("extra_packages", image.extra_packages);
    kv("features", image.features);
    o << "empty_root_password = " << bool_str(image.empty_root_password) << "\n";
    kv("root_password", image.root_password);

    for (const auto& u : image.users) {
        o << "\n[user]\n";
        kv("name", u.name);
        kv("password", u.password);
        kv("groups", u.groups);
    }

    for (const auto& l : extra_layers) {
        o << "\n[extra_layer]\n";
        kv("name", l.name);
        kv("url", l.url);
        kv("branch", l.branch);
        kv("sublayers", l.sublayers);
        o << "enabled = " << bool_str(l.enabled) << "\n";
    }

    for (const auto& a : apps) {
        o << "\n[app]\n";
        kv("name", a.name);
        kv("version", a.version);
        kv("summary", a.summary);
        kv("license", a.license);
        kv("lic_files_chksum", a.lic_files_chksum);
        o << "kind = " << to_string(a.kind) << "\n";
        o << "source = " << to_string(a.source) << "\n";
        kv("source_dir", a.source_dir);
        kv("git_url", a.git_url);
        kv("git_branch", a.git_branch);
        kv("git_srcrev", a.git_srcrev);
        kv("depends", a.depends);
        kv("rdepends", a.rdepends);
        kv("extra_conf", a.extra_conf);
        kv("autostart", a.autostart);
        kv("autostart_params", a.autostart_params);
        o << "in_image = " << bool_str(a.in_image) << "\n";
        o << "enabled = " << bool_str(a.enabled) << "\n";
        for (const auto& f : a.files) {
            o << "file = " << ini_escape(f.name) << "|" << ini_escape(f.dest) << "|"
              << ini_escape(f.mode) << "|" << ini_escape(f.content) << "\n";
        }
    }

    for (const auto& f : rootfs_files) {
        o << "\n[rootfs_file]\n";
        kv("path", f.path);
        kv("mode", f.mode);
        kv("owner_recipe", f.owner_recipe);
        kv("content", f.content);
        o << "enabled = " << bool_str(f.enabled) << "\n";
    }

    for (const auto& f : fragments) {
        o << "\n[fragment]\n";
        kv("recipe", f.recipe);
        kv("name", f.name);
        kv("content", f.content);
        o << "enabled = " << bool_str(f.enabled) << "\n";
    }

    for (const auto& p : patches) {
        o << "\n[patch]\n";
        kv("recipe", p.recipe);
        kv("name", p.name);
        o << "method = " << to_string(p.method) << "\n";
        kv("file", p.file);
        kv("search", p.search);
        kv("replace", p.replace);
        o << "replace_all = " << bool_str(p.replace_all) << "\n";
        kv("message", p.message);
        kv("upstream_status", p.upstream_status);
        kv("patch_content", p.patch_content);
        o << "enabled = " << bool_str(p.enabled) << "\n";
    }
    return o.str();
}

bool ProjectSpec::owns_section(const std::string& s) {
    static const std::set<std::string> kSections = {
        "project", "image", "user", "extra_layer", "app", "rootfs_file", "fragment", "patch",
    };
    return kSections.count(s) > 0;
}

void ProjectSpec::begin_section(const std::string& s) {
    if (s == "user")             image.users.emplace_back();
    else if (s == "extra_layer") extra_layers.emplace_back();
    else if (s == "app")         apps.emplace_back();
    else if (s == "rootfs_file") rootfs_files.emplace_back();
    else if (s == "fragment")    fragments.emplace_back();
    else if (s == "patch")       patches.emplace_back();
}

void ProjectSpec::set_value(const std::string& s, const std::string& key, const std::string& raw) {
    const std::string v = ini_unescape(raw);

    if (s == "project") {
        if (key == "layer_name") layer_name = v;
        else if (key == "layer_priority") layer_priority = std::atoi(v.c_str());
        else if (key == "layer_depends") layer_depends = v;
        else if (key == "local_conf_extra") local_conf_extra = v;
        else if (key == "rm_work") rm_work = parse_bool(v);
    } else if (s == "image") {
        if (key == "name") image.name = v;
        else if (key == "base") image.base = v;
        else if (key == "summary") image.summary = v;
        else if (key == "hostname") image.hostname = v;
        else if (key == "extra_packages") image.extra_packages = v;
        else if (key == "features") image.features = v;
        else if (key == "empty_root_password") image.empty_root_password = parse_bool(v);
        else if (key == "root_password") image.root_password = v;
    } else if (s == "user" && !image.users.empty()) {
        auto& u = image.users.back();
        if (key == "name") u.name = v;
        else if (key == "password") u.password = v;
        else if (key == "groups") u.groups = v;
    } else if (s == "extra_layer" && !extra_layers.empty()) {
        auto& l = extra_layers.back();
        if (key == "name") l.name = v;
        else if (key == "url") l.url = v;
        else if (key == "branch") l.branch = v;
        else if (key == "sublayers") l.sublayers = v;
        else if (key == "enabled") l.enabled = parse_bool(v);
    } else if (s == "app" && !apps.empty()) {
        auto& a = apps.back();
        if (key == "file") {
            // Le champ brut est découpé AVANT déséchappement.
            auto f = split_fields(raw, 4);
            while (f.size() < 4) f.emplace_back();
            a.files.push_back(ProjectFile{ini_unescape(f[0]), ini_unescape(f[1]),
                                          ini_unescape(f[2]), ini_unescape(f[3])});
        }
        else if (key == "name") a.name = v;
        else if (key == "version") a.version = v;
        else if (key == "summary") a.summary = v;
        else if (key == "license") a.license = v;
        else if (key == "lic_files_chksum") a.lic_files_chksum = v;
        else if (key == "kind") a.kind = app_kind_from_string(v);
        else if (key == "source") a.source = source_kind_from_string(v);
        else if (key == "source_dir") a.source_dir = v;
        else if (key == "git_url") a.git_url = v;
        else if (key == "git_branch") a.git_branch = v;
        else if (key == "git_srcrev") a.git_srcrev = v;
        else if (key == "depends") a.depends = v;
        else if (key == "rdepends") a.rdepends = v;
        else if (key == "extra_conf") a.extra_conf = v;
        else if (key == "autostart") a.autostart = v;
        else if (key == "autostart_params") a.autostart_params = v;
        else if (key == "in_image") a.in_image = parse_bool(v);
        else if (key == "enabled") a.enabled = parse_bool(v);
    } else if (s == "rootfs_file" && !rootfs_files.empty()) {
        auto& f = rootfs_files.back();
        if (key == "path") f.path = v;
        else if (key == "mode") f.mode = v;
        else if (key == "owner_recipe") f.owner_recipe = v;
        else if (key == "content") f.content = v;
        else if (key == "enabled") f.enabled = parse_bool(v);
    } else if (s == "fragment" && !fragments.empty()) {
        auto& f = fragments.back();
        if (key == "recipe") f.recipe = v;
        else if (key == "name") f.name = v;
        else if (key == "content") f.content = v;
        else if (key == "enabled") f.enabled = parse_bool(v);
    } else if (s == "patch" && !patches.empty()) {
        auto& p = patches.back();
        if (key == "recipe") p.recipe = v;
        else if (key == "name") p.name = v;
        else if (key == "method") p.method = patch_method_from_string(v);
        else if (key == "file") p.file = v;
        else if (key == "search") p.search = v;
        else if (key == "replace") p.replace = v;
        else if (key == "replace_all") p.replace_all = parse_bool(v);
        else if (key == "message") p.message = v;
        else if (key == "upstream_status") p.upstream_status = v;
        else if (key == "patch_content") p.patch_content = v;
        else if (key == "enabled") p.enabled = parse_bool(v);
    }
}

// --- Aides ---------------------------------------------------------------

bool is_valid_recipe_name(const std::string& n) {
    if (n.empty()) return false;
    if (!(std::islower(static_cast<unsigned char>(n[0])) || std::isdigit(static_cast<unsigned char>(n[0])))) {
        return false;
    }
    return std::all_of(n.begin(), n.end(), [](char c) {
        return std::islower(static_cast<unsigned char>(c)) ||
               std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '+' || c == '.';
    });
}

std::string suggested_owner_recipe(const std::string& path) {
    static const std::vector<std::pair<std::string, std::string>> kOwners = {
        {"/etc/motd", "base-files"},
        {"/etc/issue", "base-files"},
        {"/etc/issue.net", "base-files"},
        {"/etc/hostname", "base-files"},
        {"/etc/fstab", "base-files"},
        {"/etc/profile", "base-files"},
        {"/etc/shells", "base-files"},
        {"/etc/host.conf", "base-files"},
        {"/etc/network/interfaces", "init-ifupdown"},
        {"/etc/inittab", "sysvinit-inittab"},
        {"/etc/hosts", "netbase"},
        {"/etc/protocols", "netbase"},
        {"/etc/services", "netbase"},
        {"/etc/busybox.links", "busybox"},
        {"/etc/syslog.conf", "busybox"},
        {"/etc/default/dropbear", "dropbear"},
    };
    for (const auto& [p, owner] : kOwners) {
        if (p == path) return owner;
    }
    return "";
}

bool has_errors(const std::vector<Issue>& issues) {
    return std::any_of(issues.begin(), issues.end(),
                       [](const Issue& i) { return i.level == Issue::Level::Error; });
}

// --- Validation ----------------------------------------------------------

std::vector<Issue> validate(const ProjectSpec& spec, const std::string& poky_branch) {
    std::vector<Issue> out;
    auto err = [&out](const std::string& w, const std::string& m) {
        out.push_back(Issue{Issue::Level::Error, w, m});
    };
    auto warn = [&out](const std::string& w, const std::string& m) {
        out.push_back(Issue{Issue::Level::Warning, w, m});
    };

    // --- Layer ---
    if (spec.layer_name.empty()) {
        err("layer", "nom de layer vide");
    } else {
        if (spec.layer_name.rfind("meta-", 0) != 0) warn("layer", "par convention, le nom d'un layer commence par « meta- »");
        const bool ok = std::all_of(spec.layer_name.begin(), spec.layer_name.end(), [](char c) {
            return std::islower(static_cast<unsigned char>(c)) ||
                   std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '_';
        });
        if (!ok) err("layer", "nom de layer invalide (minuscules, chiffres, - et _)");
    }
    if (spec.layer_priority < 1 || spec.layer_priority > 99) err("layer", "priorité hors de [1, 99]");
    if (poky_branch == "master") {
        warn("layer", "branche master : LAYERSERIES_COMPAT vaudra « master », à ajuster");
    }

    // --- Image ---
    if (!is_valid_recipe_name(spec.image.name)) err("image", "nom de recette d'image invalide : « " + spec.image.name + " »");
    if (spec.image.base.empty()) err("image", "image de base vide (ex : core-image-minimal)");
    std::set<std::string> user_names;
    for (const auto& u : spec.image.users) {
        const std::string w = "utilisateur " + (u.name.empty() ? std::string("?") : u.name);
        if (!is_valid_user_name(u.name)) err(w, "nom d'utilisateur invalide");
        else if (!user_names.insert(u.name).second) err(w, "utilisateur en double");
        if (u.name == "root") err(w, "utilisez « mot de passe root » de l'image plutôt qu'un compte root");
        if (u.password.empty()) warn(w, "sans mot de passe, le compte sera verrouillé");
    }

    // --- Couches externes ---
    std::set<std::string> layer_names;
    for (const auto& l : spec.extra_layers) {
        const std::string w = "couche " + (l.name.empty() ? std::string("?") : l.name);
        if (!l.enabled) continue;
        if (l.name.empty() || !is_safe_relative(l.name) || l.name.find('/') != std::string::npos) {
            err(w, "nom de répertoire invalide");
        } else if (!layer_names.insert(l.name).second) {
            err(w, "couche en double");
        }
        if (l.name == spec.layer_name) err(w, "porte le même nom que le layer du projet");
        if (l.url.empty()) err(w, "URL git vide");
        std::istringstream subs(l.sublayers);
        std::string sub;
        while (subs >> sub) {
            if (!is_safe_relative(sub)) err(w, "sous-couche invalide : " + sub);
        }
    }

    // --- Applications ---
    std::set<std::string> recipe_names = {spec.image.name, spec.rootfs_recipe_name()};
    for (const auto& a : spec.apps) {
        if (!a.enabled) continue;
        const std::string w = "app " + (a.name.empty() ? std::string("?") : a.name);
        if (!is_valid_recipe_name(a.name)) err(w, "nom de recette invalide (minuscules, chiffres, + . -)");
        else if (!recipe_names.insert(a.name).second) err(w, "nom déjà utilisé par une autre recette du projet");
        if (a.version.empty() || a.version.find('_') != std::string::npos) err(w, "version vide ou contenant « _ »");
        if (a.license != "MIT" && a.license != "CLOSED" && a.lic_files_chksum.empty()) {
            err(w, "LIC_FILES_CHKSUM requis pour la licence « " + a.license + " »");
        }

        switch (a.source) {
            case SourceKind::Inline:
                if (a.files.empty()) err(w, "aucun fichier source");
                break;
            case SourceKind::HostDir: {
                std::error_code ec;
                if (a.source_dir.empty()) err(w, "répertoire source vide");
                else if (!std::filesystem::is_directory(a.source_dir, ec)) err(w, "répertoire introuvable : " + a.source_dir);
                break;
            }
            case SourceKind::Git:
                if (a.git_url.empty()) err(w, "URL git vide");
                if (a.git_srcrev.empty()) err(w, "SRCREV vide (hash de commit ou ${AUTOREV})");
                else if (a.git_srcrev == "${AUTOREV}") warn(w, "${AUTOREV} : build non reproductible, figez un commit");
                break;
        }

        std::set<std::string> names;
        std::set<std::string> dests;
        bool compiles = false;
        bool has_makefile = false;
        bool has_cmakelists = false;
        for (const auto& f : a.files) {
            const std::string fw = w + " / " + (f.name.empty() ? std::string("?") : f.name);
            if (!is_safe_relative(f.name)) err(fw, "nom de fichier invalide (relatif, sans « .. » ni « | »)");
            else if (!names.insert(f.name).second) err(fw, "fichier en double");
            if (!f.dest.empty()) {
                if (!is_safe_absolute(f.dest)) err(fw, "destination invalide (chemin absolu attendu)");
                else if (!dests.insert(f.dest).second) err(fw, "destination en double : " + f.dest);
            }
            if (!is_octal_mode(f.mode)) err(fw, "mode invalide : « " + f.mode + " » (ex : 0755)");
            const std::string base = std::filesystem::path(f.name).filename().string();
            if (base == "Makefile" || base == "makefile" || base == "GNUmakefile") has_makefile = true;
            if (base == "CMakeLists.txt") has_cmakelists = true;
            if (!f.dest.empty() && (has_suffix(f.name, ".c") || has_suffix(f.name, ".cpp") ||
                                    has_suffix(f.name, ".cc"))) {
                compiles = true;
            }
        }
        if (a.kind == AppKind::CFiles && !compiles) {
            warn(w, "type « fichiers C » : aucun .c/.cpp avec une destination, rien ne sera compilé");
        }
        if (a.kind == AppKind::Make && a.source == SourceKind::Inline && !has_makefile) {
            err(w, "type Makefile : aucun fichier Makefile");
        }
        if (a.kind == AppKind::CMake && a.source == SourceKind::Inline && !has_cmakelists) {
            err(w, "type CMake : aucun fichier CMakeLists.txt");
        }
        if (!a.autostart.empty() && dests.count("/etc/init.d/" + a.autostart) == 0) {
            warn(w, "démarrage auto : aucun fichier installé dans /etc/init.d/" + a.autostart);
        }
    }

    // --- Fichiers Linux de la cible ---
    std::set<std::string> rootfs_paths;
    for (const auto& f : spec.rootfs_files) {
        if (!f.enabled) continue;
        const std::string w = "fichier " + (f.path.empty() ? std::string("?") : f.path);
        if (!is_safe_absolute(f.path)) err(w, "chemin invalide (absolu, sans « .. »)");
        else if (!rootfs_paths.insert(f.path).second) err(w, "fichier en double");
        if (!is_octal_mode(f.mode)) err(w, "mode invalide : « " + f.mode + " »");
        if (!f.owner_recipe.empty() && !is_valid_recipe_name(f.owner_recipe)) err(w, "recette propriétaire invalide");
        const std::string owner = suggested_owner_recipe(f.path);
        if (f.owner_recipe.empty() && !owner.empty()) {
            warn(w, "déjà fourni par « " + owner + " » : renseignez-le comme recette, sinon conflit dans le rootfs");
        }
    }

    // --- Fragments ---
    std::set<std::string> fragment_keys;
    for (const auto& f : spec.fragments) {
        if (!f.enabled) continue;
        const std::string w = "fragment " + (f.name.empty() ? std::string("?") : f.name);
        if (!is_valid_recipe_name(f.recipe)) err(w, "recette cible invalide");
        if (!is_safe_relative(f.name) || f.name.find('/') != std::string::npos) err(w, "nom de fichier invalide");
        else if (!has_suffix(f.name, ".cfg")) warn(w, "un fragment Kconfig porte en principe l'extension .cfg");
        if (!fragment_keys.insert(f.recipe + "/" + f.name).second) err(w, "fragment en double");
        std::istringstream in(f.content);
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            if (line.rfind("CONFIG_", 0) != 0 && line.rfind("# CONFIG_", 0) != 0) {
                warn(w, "ligne inattendue : « " + line + " »");
                break;
            }
        }
    }

    // --- Patchs ---
    for (const auto& p : spec.patches) {
        if (!p.enabled) continue;
        const std::string w = "patch " + (p.name.empty() ? std::string("?") : p.name);
        if (!is_valid_recipe_name(p.recipe)) err(w, "recette à patcher invalide");
        if (p.method == PatchMethod::Devtool) {
            if (p.search.empty()) err(w, "texte à rechercher vide");
            if (p.message.empty()) err(w, "message de commit vide (il devient l'en-tête du patch)");
            if (!p.file.empty() && !is_safe_relative(p.file)) err(w, "fichier cible invalide");
        } else {
            if (p.name.empty() || !is_safe_relative(p.name) || p.name.find('/') != std::string::npos) {
                err(w, "nom de fichier .patch invalide");
            }
            if (p.patch_content.empty()) err(w, "contenu du patch vide");
            else if (p.patch_content.find("@@") == std::string::npos) warn(w, "ne ressemble pas à un diff unifié (aucun « @@ »)");
        }
    }
    return out;
}

} // namespace yocto
