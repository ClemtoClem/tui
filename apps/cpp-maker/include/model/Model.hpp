/* @file model/Model.hpp
 *
 * Modèle de configuration d'un projet cpp-maker.
 *
 *   <racine>/project.toml                         ProjectConfig
 *   <racine>/libs/<layer>/layer.toml              LayerConfig
 *   <racine>/libs/<layer>/<module>/module.toml    TargetConfig (Kind::Module)
 *   <racine>/apps/<app>/application.toml          TargetConfig (Kind::Application)
 *
 * Un module est une bibliothèque (static, shared, header-only) rangée dans
 * un layer ; son identifiant est « <layer>.<module> ». Une application est
 * un exécutable ; son identifiant est son nom. Seuls les modules peuvent
 * être des dépendances.
 */
#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace model {

inline constexpr const char* kProjectFile = "project.toml";
inline constexpr const char* kLayerFile = "layer.toml";
inline constexpr const char* kModuleFile = "module.toml";
inline constexpr const char* kApplicationFile = "application.toml";
inline constexpr const char* kLockFile = "cpp-maker.lock";
inline constexpr const char* kBusyLockFile = ".cpp-maker.busy";

struct ModeConfig {
    std::string name;
    std::string base;
    std::vector<std::string> flags, link_flags, defines;
    std::string sanitizer;
    std::string runner;
    std::vector<std::string> runner_args;
    bool builtin = false;   ///< mode prédéfini non redéfini dans project.toml
};

struct ToolchainConfig {
    std::string name;
    std::string prefix;
    std::string cc = "auto", cxx = "auto";
    std::string ar, strip, pkg_config;
    std::string family = "auto";   ///< auto | gcc | clang
    std::string os;                ///< vide = déduit du triplet
    std::string target, sysroot;
    std::vector<std::string> cflags, cxxflags, ldflags;
    std::vector<std::string> runner;
    bool runner_set = false;
    bool from_config = false;
};

struct PlatformOverride {
    std::vector<std::string> sources, defines, system_libs, cflags, cxxflags, ldflags;
};

struct GitDependency {
    std::string alias, url, version = "HEAD", module, subdir;
};

struct Dependencies {
    std::vector<std::string> modules;      ///< références de modules (« core.log », « log », « core.* »)
    std::vector<std::string> pkg_config;
    std::vector<std::string> system;
    std::vector<std::string> frameworks;   ///< macOS
    std::vector<GitDependency> git;
};

struct Flags {
    std::vector<std::string> defines, public_defines, cflags, cxxflags, ldflags, warnings;
};

/// État d'approbation d'une cible ([review] de module.toml / application.toml).
/// Géré par « cpp-maker review » : une cible non approuvée peut être bloquée
/// à la compilation quand [build].require_approval est actif.
struct ReviewConfig {
    std::string status = "none";   ///< none | requested | approved | rejected
    std::string reviewer;          ///< qui a statué (approve/reject)
    std::string date;              ///< date libre (AAAA-MM-JJ)
    std::string notes;
    [[nodiscard]] bool is_approved() const { return status == "approved"; }
    [[nodiscard]] bool is_set() const { return status != "none"; }
};

/// Règle exécutée par le crochet git pre-commit ([git.pre_commit.<nom>]).
/// Portée : tous les fichiers indexés (project.toml), ceux du layer
/// (layer.toml) ou ceux de la cible (module.toml, application.toml).
struct PreCommitRule {
    std::string name;
    std::string description;
    std::string run;       ///< arguments de cpp-maker (« test {targets} »)
    std::string command;   ///< ou commande shell exécutée depuis la racine
    std::vector<std::string> when;   ///< motifs des fichiers indexés (vide = toute la portée)
    std::vector<std::pair<std::string, std::string>> env;
    std::string on_failure = "block";   ///< block | warn
    bool enabled = true;
    fs::path file;   ///< fichier de définition
};

struct LayerConfig {
    std::string name;
    std::string version = "0.1.0";
    std::string description;
    fs::path dir;    ///< relatif à la racine (libs/<layer>)
    fs::path file;   ///< absolu
    bool has_file = false;
    std::vector<std::string> modules;   ///< liste explicite (vide = découverte automatique)
    std::vector<std::string> discovered;   ///< noms des modules effectivement chargés
    Dependencies deps;
    Flags flags;
    std::vector<std::string> forbid_pkg_config;
    std::map<std::string, PlatformOverride> platforms;
    std::vector<PreCommitRule> pre_commit;
};

enum class Kind { Module, Application };

struct TargetConfig {
    Kind kind = Kind::Module;
    std::string id;           ///< « layer.module » ou « app »
    std::string name;
    std::string layer;        ///< modules uniquement
    std::string version = "0.1.0";
    std::string description;
    std::string type = "static";   ///< static | shared | header-only | executable
    std::string language = "c++";  ///< c | c++
    std::string cxx_standard, c_standard;   ///< vide = valeurs du projet
    fs::path dir;    ///< relatif à la racine
    fs::path file;   ///< absolu
    std::string artifact;   ///< nom de base de l'artefact (défaut : name)
    bool install = true;
    bool from_git = false;

    std::vector<std::string> source_dirs = {"src"};
    std::vector<std::string> source_files, exclude;
    std::string main;   ///< applications : source contenant main()
    std::vector<std::string> public_includes = {"include"};
    std::vector<std::string> private_includes;
    bool private_includes_set = false;

    Dependencies deps;
    Flags flags;
    std::vector<std::string> test_dirs = {"tests"};
    std::string test_framework;   ///< vide = [tests].framework du projet
    int test_timeout = -1;        ///< -1 = [tests].timeout du projet
    std::map<std::string, PlatformOverride> platforms;

    std::vector<PreCommitRule> pre_commit;

    ReviewConfig review;

    // [run] (applications)
    std::vector<std::string> run_args;
    std::vector<std::pair<std::string, std::string>> run_env;
    std::string run_cwd;

    // Résolution (Workspace::resolve)
    std::vector<std::string> dep_ids;   ///< modules directs (y compris hérités)
    std::vector<std::string> all_pkg_config, all_system_libs;

    [[nodiscard]] bool is_app() const { return kind == Kind::Application; }
    [[nodiscard]] bool is_module() const { return kind == Kind::Module; }
    [[nodiscard]] bool is_header_only() const { return type == "header-only"; }
    [[nodiscard]] std::string artifact_base() const { return artifact.empty() ? name : artifact; }
    [[nodiscard]] std::string kind_label() const { return is_app() ? "application" : "module"; }
};

struct PackageConfig {
    std::vector<std::string> formats = {"tar.gz"};
    bool strip = true;
    std::string maintainer = "inconnu <inconnu@example.com>";
    std::string description, homepage, license;
    std::vector<std::string> deb_depends;
    std::string deb_prefix = "/usr";
    std::vector<std::string> extra_files;
    bool checksums = true;
};

struct DeployConfig {
    std::string name;
    std::string method = "rsync";   ///< rsync | scp | copy | script
    std::string host, user, path;
    int port = 0;
    std::string toolchain;
    std::string mode = "release";
    std::string artifact = "stage";   ///< stage | tar.gz | tar.xz | zip | deb
    std::vector<std::string> targets;
    std::vector<std::string> ssh_options, before, after, after_local;
    std::string script;
    bool delete_extra = false;
};

struct GenerateStep {
    std::string name, command;
    std::vector<std::string> inputs, outputs;
    std::vector<std::string> run_before;   ///< build, test, run, package… ou all
    bool always = false;
};

struct ProjectConfig {
    fs::path file;   ///< absolu
    std::string name = "project";
    std::string version = "0.1.0";
    std::string description;
    std::string language = "c++";
    std::string cxx_standard = "c++23";
    std::string c_standard = "c17";
    std::string min_version;

    // [paths]
    std::vector<std::string> layers_dirs = {"libs"};   ///< un ou plusieurs dossiers de layers
    std::vector<std::string> apps_dirs = {"apps"};   ///< un ou plusieurs dossiers (apps, examples…)
    std::string build_dir = "build";
    std::string dist_dir = "dist";
    std::string out_dir = ".out";   ///< fichiers de --output/--stdout/--stderr sans dossier

    // [layers]
    std::vector<std::string> layer_order;

    // [dependencies] (héritées par toutes les cibles)
    Dependencies deps;

    // [build]
    std::string default_mode = "debug";
    std::string default_toolchain = "host";
    int jobs = 0;
    std::vector<std::string> warnings = {"-Wall", "-Wextra", "-Wpedantic", "-Wshadow",
                                         "-Wconversion", "-Wsign-conversion", "-Wformat=2",
                                         "-Wundef", "-Wcast-align", "-Wnon-virtual-dtor"};
    bool static_pic = true;
    bool require_approval = false;   ///< bloquer build/test/package tant qu'une cible n'est pas approuvée
    bool ccache = true;              ///< utiliser ccache si présent dans le PATH
    std::vector<std::string> defines, cflags, cxxflags, ldflags;
    std::vector<ModeConfig> modes;
    std::map<std::string, ToolchainConfig> toolchains;

    // [tests]
    int test_timeout = 300;
    std::string test_framework = "builtin";   ///< builtin | gtest | none
    std::vector<std::pair<std::string, std::string>> test_env;

    PackageConfig package;
    std::map<std::string, DeployConfig> deploys;
    std::vector<GenerateStep> generate;
    std::vector<PreCommitRule> pre_commit;

    [[nodiscard]] const ModeConfig* find_mode(const std::string& n) const {
        for (auto& m : modes)
            if (m.name == n) return &m;
        return nullptr;
    }
};

/// Modes prédéfinis (release, debug, asan…).
std::vector<ModeConfig> builtin_modes();
const std::vector<std::string>& cxx_standards();
const std::vector<std::string>& c_standards();
/// « -DX » dans flags → defines ; sanitizer → -fsanitize=…
void normalize_mode(ModeConfig& m);

} // namespace model
