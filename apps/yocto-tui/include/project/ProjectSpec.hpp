/* apps/yocto-tui/project/ProjectSpec.hpp */
#pragma once

#include <string>
#include <vector>

namespace yocto {

/// Façon dont une application custom est construite par sa recette.
enum class AppKind {
    Scripts,  // fichiers installés tels quels (shell, python, données...)
    CFiles,   // chaque .c/.cpp compilé en un exécutable (${CC} ...)
    Make,     // Makefile du projet (oe_runmake / install)
    CMake,    // CMakeLists.txt du projet (inherit cmake)
};

/// Provenance des sources d'une application custom.
enum class SourceKind {
    Inline,   // contenu stocké dans la configuration et édité dans le TUI
    HostDir,  // répertoire de la machine hôte copié dans le layer
    Git,      // dépôt git récupéré par le fetcher BitBake
};

enum class PatchMethod {
    Devtool,    // devtool modify -> remplacement -> commit -> devtool finish
    PatchFile,  // fichier .patch fourni, ajouté au SRC_URI par un bbappend
};

[[nodiscard]] const char* to_string(AppKind k);
[[nodiscard]] const char* to_string(SourceKind k);
[[nodiscard]] const char* to_string(PatchMethod m);
[[nodiscard]] AppKind     app_kind_from_string(const std::string& s);
[[nodiscard]] SourceKind  source_kind_from_string(const std::string& s);
[[nodiscard]] PatchMethod patch_method_from_string(const std::string& s);

/// Fichier d'une application. En mode Inline, `content` est la source ;
/// en mode HostDir/Git, l'entrée ne fait que référencer un fichier du
/// répertoire source (contenu ignoré) pour indiquer quoi compiler ou
/// installer.
struct ProjectFile {
    std::string name;          // chemin relatif dans les sources (ex : src/main.c)
    std::string dest;          // chemin dans la cible (ex : /usr/bin/hello) ; vide = non installé
    std::string mode = "0644";
    std::string content;
};

/// Couche externe à cloner sous <lab>/layers/ (ex : meta-openembedded).
struct ExtraLayer {
    std::string name;          // répertoire sous layers/
    std::string url;
    std::string branch;        // vide -> branche de poky
    std::string sublayers;     // "meta-oe meta-python" ; vide -> le dépôt est le layer
    bool enabled = true;
};

struct CustomApp {
    std::string name;          // nom de recette (minuscules)
    std::string version = "1.0";
    std::string summary;
    std::string license = "MIT";
    std::string lic_files_chksum;  // vide -> calculé pour MIT, requis sinon
    AppKind kind = AppKind::CFiles;
    SourceKind source = SourceKind::Inline;
    std::string source_dir;    // SourceKind::HostDir
    std::string git_url;       // SourceKind::Git
    std::string git_branch = "main";
    std::string git_srcrev = "${AUTOREV}";
    std::string depends;       // DEPENDS (construction)
    std::string rdepends;      // RDEPENDS:${PN} (exécution)
    std::string extra_conf;    // EXTRA_OEMAKE ou EXTRA_OECMAKE
    std::string autostart;     // script /etc/init.d/<x> lancé au démarrage (update-rc.d)
    std::string autostart_params = "defaults 90";
    std::vector<ProjectFile> files;
    bool in_image = true;      // ajouté à IMAGE_INSTALL
    bool enabled = true;
};

/// Fichier Linux déposé dans le système de fichiers de la cible.
struct RootfsFile {
    std::string path;          // chemin absolu dans la cible (ex : /etc/motd)
    std::string mode = "0644";
    std::string owner_recipe;  // recette qui possède déjà ce fichier -> bbappend
                               // vide -> recette <layer>-rootfs-files
    std::string content;
    bool enabled = true;
};

/// Fragment de configuration Kconfig (busybox, linux-yocto, u-boot...).
struct ConfigFragment {
    std::string recipe = "busybox";
    std::string name;          // ex : ntpd.cfg
    std::string content;       // ex : CONFIG_NTPD=y
    bool enabled = true;
};

struct PatchSpec {
    std::string recipe;        // ex : bc
    std::string name;          // identifiant lisible / nom du fichier .patch
    PatchMethod method = PatchMethod::Devtool;
    std::string file;          // fichier à modifier (relatif aux sources) ; vide = recherche
    std::string search;
    std::string replace;
    bool replace_all = false;
    std::string message;       // sujet du commit (devient l'en-tête du patch)
    std::string upstream_status = "Inappropriate [configuration locale]";
    std::string patch_content; // PatchMethod::PatchFile
    bool enabled = true;
};

struct UserAccount {
    std::string name;
    std::string password;      // en clair dans la config, hashé (SHA-512) à la génération
    std::string groups;        // "audio,video"
};

struct ImageSpec {
    std::string name = "custom-image";
    std::string base = "core-image-minimal";
    std::string summary;
    std::string hostname;
    std::string extra_packages;    // paquets existants : "strace lsof bc"
    std::string features;          // IMAGE_FEATURES : "ssh-server-dropbear"
    bool empty_root_password = true;
    std::string root_password;     // prioritaire sur empty_root_password
    std::vector<UserAccount> users;
};

/// Description complète du projet Yocto à générer : un layer
/// personnel, sa recette d'image, les applications custom et la
/// configuration Linux de la cible. Persistée dans le même fichier INI
/// que YoctoConfig (sections [project], [image], [user], [app]...).
struct ProjectSpec {
    std::string layer_name = "meta-custom";
    int layer_priority = 6;
    std::string layer_depends = "core";
    ImageSpec image;
    std::vector<ExtraLayer> extra_layers;
    std::vector<CustomApp> apps;
    std::vector<RootfsFile> rootfs_files;
    std::vector<ConfigFragment> fragments;
    std::vector<PatchSpec> patches;
    std::string local_conf_extra;  // lignes libres ajoutées à local.conf
    bool rm_work = false;

    /// meta-formation -> formation (BBFILE_COLLECTIONS).
    [[nodiscard]] std::string collection() const;
    /// Recette regroupant les fichiers Linux sans recette propriétaire.
    [[nodiscard]] std::string rootfs_recipe_name() const;

    // --- Sérialisation INI ---
    [[nodiscard]] std::string to_ini() const;
    [[nodiscard]] static bool owns_section(const std::string& section);
    /// Appelé à chaque en-tête de section : les sections répétées
    /// ([app], [user], ...) créent un nouvel élément.
    void begin_section(const std::string& section);
    void set_value(const std::string& section, const std::string& key, const std::string& value);
};

// --- Validation ------------------------------------------------------------

struct Issue {
    enum class Level { Warning, Error };
    Level level = Level::Error;
    std::string where;    // ex : "app formation-apps"
    std::string message;
};

/// Vérifie la cohérence du projet avant génération. N'accède au disque
/// que pour vérifier l'existence des répertoires hôte (HostDir).
[[nodiscard]] std::vector<Issue> validate(const ProjectSpec& spec, const std::string& poky_branch);
[[nodiscard]] bool has_errors(const std::vector<Issue>& issues);

/// Nom de recette BitBake valide : [a-z0-9][a-z0-9+.-]* (pas de '_').
[[nodiscard]] bool is_valid_recipe_name(const std::string& name);

/// Recette de poky qui fournit déjà ce fichier de la cible (ex :
/// /etc/motd -> base-files), "" si inconnue. Livrer le même fichier
/// depuis une autre recette provoque un conflit à la création du rootfs.
[[nodiscard]] std::string suggested_owner_recipe(const std::string& rootfs_path);

// --- Échappement des valeurs multilignes dans l'INI ------------------------

[[nodiscard]] std::string ini_escape(const std::string& s);
[[nodiscard]] std::string ini_unescape(const std::string& s);

} // namespace yocto
