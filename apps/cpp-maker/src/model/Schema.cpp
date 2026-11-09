/* @file model/Schema.cpp */

#include "model/Schema.hpp"

#include "core/Util.hpp"
#include "io/TomlDocument.hpp"
#include "model/Model.hpp"

#include <algorithm>
#include <format>

namespace schema {

std::string_view file_name(FileKind k) {
    switch (k) {
    case FileKind::Project: return model::kProjectFile;
    case FileKind::Layer: return model::kLayerFile;
    case FileKind::Module: return model::kModuleFile;
    case FileKind::Application: return model::kApplicationFile;
    }
    return "";
}

std::string_view kind_label(FileKind k) {
    switch (k) {
    case FileKind::Project: return "projet";
    case FileKind::Layer: return "layer";
    case FileKind::Module: return "module";
    case FileKind::Application: return "application";
    }
    return "";
}

namespace {

using FT = FieldType;

const std::vector<std::string> kLang = {"c++", "c"};
const std::vector<std::string> kOs = {"", "linux", "windows", "macos", "android", "freebsd", "baremetal"};
const std::vector<std::string> kFramework = {"builtin", "gtest", "none"};

std::vector<std::string> cxx_std_choices() {
    std::vector<std::string> r = {""};
    for (auto& s : model::cxx_standards()) r.push_back(s);
    return r;
}
std::vector<std::string> c_std_choices() {
    std::vector<std::string> r = {""};
    for (auto& s : model::c_standards()) r.push_back(s);
    return r;
}

struct Spec {
    std::vector<Section> sections;
    std::vector<Field> fields;
};

void add_flags(Spec& s, const std::string& sec, bool with_warnings) {
    s.fields.push_back({sec, "defines", FT::StringList, "Defines", "Macros -D pour cette cible (NOM ou NOM=valeur).", {}, ""});
    s.fields.push_back({sec, "public_defines", FT::StringList, "Defines publics", "Macros propagées aux dépendants.", {}, ""});
    s.fields.push_back({sec, "cflags", FT::StringList, "Options C", "Options de compilation C supplémentaires.", {}, ""});
    s.fields.push_back({sec, "cxxflags", FT::StringList, "Options C++", "Options de compilation C++ supplémentaires.", {}, ""});
    s.fields.push_back({sec, "ldflags", FT::StringList, "Options d'édition de liens", "Options passées à l'éditeur de liens.", {}, ""});
    if (with_warnings)
        s.fields.push_back({sec, "warnings", FT::StringList, "Avertissements", "Avertissements ajoutés à ceux du projet.", {}, ""});
}

void add_git_rules(Spec& s, const std::string& scope) {
    const std::string sec = "git.pre_commit.*";
    s.sections.push_back({sec, "Règle pre-commit",
                          "Exécutée par le crochet git pre-commit quand un fichier indexé " + scope + " est concerné."});
    s.fields.push_back({sec, "description", FT::String, "Description", "", {}, ""});
    s.fields.push_back({sec, "run", FT::String, "cpp-maker", "Arguments de cpp-maker ({targets} = cibles touchées, {files}, {root}).", {}, "test {targets}"});
    s.fields.push_back({sec, "command", FT::String, "Commande shell", "Commande exécutée depuis la racine (à la place de run).", {}, ""});
    s.fields.push_back({sec, "when", FT::StringList, "Fichiers concernés", "Motifs (*, **, ?) relatifs à la racine ; vide = toute la portée.", {}, ""});
    s.fields.push_back({sec, "env", FT::Map, "Environnement", "Variables d'environnement de la règle.", {}, ""});
    s.fields.push_back({sec, "on_failure", FT::Enum, "En cas d'échec", "block : le commit est refusé ; warn : simple avertissement.", {"", "block", "warn"}, "block"});
    s.fields.push_back({sec, "enabled", FT::Boolean, "Active", "", {}, "true"});
}

void add_platform(Spec& s, const std::string& base) {
    std::string sec = base + ".*";
    s.sections.push_back({sec, "Plate-forme", "Surcharges par OS cible (linux, windows, macos, android, freebsd, baremetal, unix)."});
    s.fields.push_back({sec, "sources", FT::StringList, "Sources", "Sources ajoutées pour cette plate-forme.", {}, ""});
    s.fields.push_back({sec, "defines", FT::StringList, "Defines", "Macros ajoutées pour cette plate-forme.", {}, ""});
    s.fields.push_back({sec, "system", FT::StringList, "Bibliothèques système", "Bibliothèques -l ajoutées.", {}, ""});
    s.fields.push_back({sec, "cflags", FT::StringList, "Options C", "", {}, ""});
    s.fields.push_back({sec, "cxxflags", FT::StringList, "Options C++", "", {}, ""});
    s.fields.push_back({sec, "ldflags", FT::StringList, "Options d'édition de liens", "", {}, ""});
}

void add_target_common(Spec& s, bool app) {
    s.sections.push_back({"sources", "Sources", "Découverte des fichiers sources."});
    s.fields.push_back({"sources", "dirs", FT::StringList, "Dossiers", "Dossiers parcourus récursivement.", {}, "src"});
    s.fields.push_back({"sources", "files", FT::StringList, "Fichiers", "Sources supplémentaires hors dossiers.", {}, ""});
    s.fields.push_back({"sources", "exclude", FT::StringList, "Exclusions", "Motifs exclus (*, **, ?).", {}, ""});

    s.sections.push_back({"include", "En-têtes", "Chemins d'inclusion."});
    if (!app)
        s.fields.push_back({"include", "public", FT::StringList, "Publics", "Propagés aux dépendants.", {}, "include"});
    s.fields.push_back({"include", "private", FT::StringList, "Privés", "Visibles de cette cible seulement.", {}, app ? "include, src" : "src"});

    s.sections.push_back({"dependencies", "Dépendances", "Modules, paquets pkg-config et bibliothèques système."});
    s.fields.push_back({"dependencies", "modules", FT::StringList, "Modules", "Références « layer.module », « module » ou « layer.* ».", {}, ""});
    s.fields.push_back({"dependencies", "pkg_config", FT::StringList, "pkg-config", "Paquets résolus par pkg-config.", {}, ""});
    s.fields.push_back({"dependencies", "system", FT::StringList, "Système", "Bibliothèques -l (m, pthread…).", {}, ""});
    s.fields.push_back({"dependencies", "frameworks", FT::StringList, "Frameworks", "Frameworks macOS.", {}, ""});

    s.sections.push_back({"dependencies.git.*", "Dépendance Git", "Projet cpp-maker distant ; ses modules sont importés sous « <alias>.<module> »."});
    s.fields.push_back({"dependencies.git.*", "url", FT::String, "URL", "Dépôt Git (obligatoire).", {}, ""});
    s.fields.push_back({"dependencies.git.*", "version", FT::String, "Version", "Tag, branche ou commit.", {}, "HEAD"});
    s.fields.push_back({"dependencies.git.*", "module", FT::String, "Module", "Module à utiliser (défaut : tous).", {}, ""});
    s.fields.push_back({"dependencies.git.*", "subdir", FT::String, "Sous-dossier", "Sous-dossier du dépôt contenant project.toml.", {}, ""});

    s.sections.push_back({"flags", "Options de compilation", ""});
    add_flags(s, "flags", false);

    s.sections.push_back({"tests", "Tests", "Tests de la cible (un exécutable <cible>-tests)."});
    s.fields.push_back({"tests", "dirs", FT::StringList, "Dossiers", "Dossiers des sources de test.", {}, "tests"});
    s.fields.push_back({"tests", "framework", FT::Enum, "Framework", "builtin (fourni par cpp-maker), gtest, none.", {"", "builtin", "gtest", "none"}, ""});
    s.fields.push_back({"tests", "timeout", FT::Integer, "Délai (s)", "Délai de l'exécutable de test (défaut : [tests].timeout du projet).", {}, ""});

    add_platform(s, "platform");
    add_git_rules(s, app ? "de l'application" : "du module");
}

Spec make_project() {
    Spec s;
    s.sections = {
        {"project", "Projet", "Identité et standards par défaut."},
        {"paths", "Arborescence", "Dossiers du projet (relatifs à la racine)."},
        {"layers", "Layers", "Ordre des couches d'architecture (de la plus basse à la plus haute)."},
        {"dependencies", "Dépendances communes", "Héritées par toutes les cibles."},
        {"build", "Compilation", "Options communes de compilation."},
        {"build.modes.*", "Mode de compilation", "Jeu d'options (release, debug, asan… ou personnalisé)."},
        {"toolchains.*", "Toolchain", "Compilateurs et outils d'une cible (hôte ou croisée)."},
        {"tests", "Tests", "Exécution des tests."},
        {"package", "Paquets", "Production des archives (package)."},
        {"deploy.*", "Déploiement", "Cible de déploiement (deploy <nom>)."},
        {"generate.*", "Génération", "Étape de génération de code exécutée avant le build."},
    };
    auto F = [&](std::string sec, std::string key, FT t, std::string label, std::string help,
                 std::vector<std::string> choices = {}, std::string ph = {}) {
        s.fields.push_back({std::move(sec), std::move(key), t, std::move(label), std::move(help), std::move(choices), std::move(ph)});
    };
    F("project", "name", FT::String, "Nom", "Nom du projet (paquets, en-têtes).");
    F("project", "version", FT::String, "Version", "Version du projet.", {}, "0.1.0");
    F("project", "description", FT::String, "Description", "");
    F("project", "language", FT::Enum, "Langage", "Langage par défaut des nouvelles cibles.", kLang);
    F("project", "cxx_standard", FT::Enum, "Standard C++", "Standard C++ par défaut.", cxx_std_choices(), "c++23");
    F("project", "c_standard", FT::Enum, "Standard C", "Standard C par défaut.", c_std_choices(), "c17");
    F("project", "minimum_cpp_maker_version", FT::String, "cpp-maker minimal", "Version minimale de cpp-maker requise.");

    F("paths", "layers", FT::StringList, "Layers", "Dossier(s) des layers : \"libs\" ou [\"libs\", \"vendor\"].", {}, "libs");
    F("paths", "applications", FT::StringList, "Applications", "Dossier(s) des applications : \"apps\" ou [\"apps\", \"examples\"].", {}, "apps");
    F("paths", "build", FT::StringList, "Build", "Dossier des artefacts (un seul : chaîne ou tableau d'un élément).", {}, "build");
    F("paths", "out", FT::StringList, "Journaux", "Dossier des fichiers --output/--stdout/--stderr donnés sans dossier.", {}, ".out");
    F("paths", "dist", FT::StringList, "Distribution", "Dossier des paquets (un seul : chaîne ou tableau d'un élément).", {}, "dist");

    F("layers", "order", FT::StringList, "Ordre", "Couches de la plus basse à la plus haute ; un module ne peut dépendre que de sa couche ou d'une couche inférieure.");

    F("dependencies", "modules", FT::StringList, "Modules", "Modules liés à toutes les cibles.");
    F("dependencies", "pkg_config", FT::StringList, "pkg-config", "Paquets pkg-config communs.");
    F("dependencies", "system", FT::StringList, "Système", "Bibliothèques système communes.");

    F("build", "default_mode", FT::String, "Mode par défaut", "Mode utilisé sans argument.", {"debug", "release", "relwithdebinfo", "minsize", "asan", "tsan", "ubsan"}, "debug");
    F("build", "default_toolchain", FT::String, "Toolchain par défaut", "", {"host", "gcc", "clang"}, "host");
    F("build", "jobs", FT::Integer, "Parallélisme", "0 = nombre de cœurs - 1.", {}, "0");
    F("build", "warnings", FT::StringList, "Avertissements", "Options d'avertissement communes.", {},
      util::join(model::ProjectConfig{}.warnings, ", "));
    F("build", "static_pic", FT::Boolean, "PIC statique", "Compiler les bibliothèques statiques avec -fPIC.", {}, "true");
    F("build", "defines", FT::StringList, "Defines", "Macros communes.");
    F("build", "cflags", FT::StringList, "Options C", "");
    F("build", "cxxflags", FT::StringList, "Options C++", "");
    F("build", "ldflags", FT::StringList, "Options d'édition de liens", "");

    F("build.modes.*", "base", FT::String, "Hérite de", "Mode de base (release, debug…).", {"release", "debug", "relwithdebinfo", "minsize", "asan", "tsan", "ubsan", "msan", "lsan", "valgrind"});
    F("build.modes.*", "flags", FT::StringList, "Options", "Options de compilation (-O2, -g…).");
    F("build.modes.*", "link_flags", FT::StringList, "Options de lien", "");
    F("build.modes.*", "defines", FT::StringList, "Defines", "");
    F("build.modes.*", "sanitizer", FT::String, "Sanitizers", "Liste séparée par des virgules (address,undefined…).");
    F("build.modes.*", "runner", FT::Enum, "Lanceur", "Lanceur des exécutables (valgrind).", {"", "valgrind"});
    F("build.modes.*", "runner_args", FT::StringList, "Arguments du lanceur", "");

    F("toolchains.*", "prefix", FT::String, "Préfixe", "Préfixe des outils croisés (arm-linux-gnueabihf-).");
    F("toolchains.*", "cc", FT::String, "Compilateur C", "auto, none ou chemin.", {}, "auto");
    F("toolchains.*", "cxx", FT::String, "Compilateur C++", "auto, none ou chemin.", {}, "auto");
    F("toolchains.*", "ar", FT::String, "ar", "");
    F("toolchains.*", "strip", FT::String, "strip", "");
    F("toolchains.*", "pkg_config", FT::String, "pkg-config", "");
    F("toolchains.*", "family", FT::Enum, "Famille", "", {"auto", "gcc", "clang"});
    F("toolchains.*", "os", FT::Enum, "OS cible", "Vide = déduit du triplet.", kOs);
    F("toolchains.*", "target", FT::String, "Triplet (clang)", "--target=…");
    F("toolchains.*", "sysroot", FT::String, "Sysroot", "--sysroot=…");
    F("toolchains.*", "cflags", FT::StringList, "Options C", "");
    F("toolchains.*", "cxxflags", FT::StringList, "Options C++", "");
    F("toolchains.*", "ldflags", FT::StringList, "Options de lien", "");
    F("toolchains.*", "runner", FT::StringList, "Émulateur", "Préfixe d'exécution (qemu-arm -L …) ; [] désactive.");

    F("tests", "timeout", FT::Integer, "Délai (s)", "Délai maximal par exécutable de test.", {}, "300");
    F("tests", "framework", FT::Enum, "Framework", "builtin (cpp-maker/test.hpp), gtest ou none.", kFramework, "builtin");
    F("tests", "env", FT::Map, "Environnement", "Variables d'environnement ({root} = racine).");

    F("package", "formats", FT::StringList, "Formats", "tar.gz, tar.xz, zip, deb.", {"tar.gz", "tar.xz", "zip", "deb"}, "tar.gz");
    F("package", "strip", FT::Boolean, "strip", "Retirer les symboles (sauf mode avec -g).", {}, "true");
    F("package", "maintainer", FT::String, "Mainteneur", "Nom <email>.");
    F("package", "description", FT::String, "Description", "");
    F("package", "homepage", FT::String, "Site", "");
    F("package", "license", FT::String, "Licence", "");
    F("package", "deb_depends", FT::StringList, "Dépendances Debian", "");
    F("package", "deb_prefix", FT::String, "Préfixe Debian", "", {}, "/usr");
    F("package", "extra_files", FT::StringList, "Fichiers additionnels", "Copiés dans share/doc/<nom>.");
    F("package", "checksums", FT::Boolean, "Sommes SHA-256", "", {}, "true");

    F("deploy.*", "method", FT::Enum, "Méthode", "", {"rsync", "scp", "copy", "script"});
    F("deploy.*", "host", FT::String, "Hôte", "");
    F("deploy.*", "user", FT::String, "Utilisateur", "");
    F("deploy.*", "port", FT::Integer, "Port", "", {}, "22");
    F("deploy.*", "path", FT::String, "Chemin", "Variables : {name} {version} {toolchain} {mode} {host}.");
    F("deploy.*", "toolchain", FT::String, "Toolchain", "");
    F("deploy.*", "mode", FT::String, "Mode", "", {}, "release");
    F("deploy.*", "artifact", FT::Enum, "Artefact", "", {"stage", "tar.gz", "tar.xz", "zip", "deb"});
    F("deploy.*", "targets", FT::StringList, "Cibles", "Cibles à déployer (défaut : toutes).");
    F("deploy.*", "ssh_options", FT::StringList, "Options SSH", "");
    F("deploy.*", "before", FT::StringList, "Avant (local)", "");
    F("deploy.*", "after", FT::StringList, "Après (distant)", "");
    F("deploy.*", "after_local", FT::StringList, "Après (local)", "");
    F("deploy.*", "script", FT::String, "Script", "Méthode script : $CPP_MAKER_DIST = dossier de staging.");
    F("deploy.*", "delete", FT::Boolean, "rsync --delete", "");

    F("generate.*", "command", FT::String, "Commande", "Commande shell (exécutée depuis la racine).");
    F("generate.*", "inputs", FT::StringList, "Entrées", "");
    F("generate.*", "outputs", FT::StringList, "Sorties", "");
    F("generate.*", "run_before", FT::StringList, "Avant", "build, run, test, check, package… ou all.", {}, "build");
    F("generate.*", "always", FT::Boolean, "Toujours", "Relancer à chaque fois.");
    add_git_rules(s, "du projet");
    return s;
}

Spec make_layer() {
    Spec s;
    s.sections = {
        {"layer", "Layer", "Identité du layer."},
        {"dependencies", "Dépendances héritées", "Ajoutées à chaque module du layer."},
        {"flags", "Options héritées", "Ajoutées à chaque module du layer."},
        {"forbid", "Interdictions", "Paquets interdits dans la fermeture des dépendances."},
    };
    auto F = [&](std::string sec, std::string key, FT t, std::string label, std::string help,
                 std::vector<std::string> choices = {}, std::string ph = {}) {
        s.fields.push_back({std::move(sec), std::move(key), t, std::move(label), std::move(help), std::move(choices), std::move(ph)});
    };
    F("layer", "name", FT::String, "Nom", "Identique au nom du dossier.");
    F("layer", "version", FT::String, "Version", "", {}, "0.1.0");
    F("layer", "description", FT::String, "Description", "");
    F("layer", "modules", FT::StringList, "Modules", "Liste explicite (vide = découverte automatique des sous-dossiers).");
    F("dependencies", "modules", FT::StringList, "Modules", "");
    F("dependencies", "pkg_config", FT::StringList, "pkg-config", "");
    F("dependencies", "system", FT::StringList, "Système", "");
    add_flags(s, "flags", true);
    F("forbid", "pkg_config", FT::StringList, "pkg-config interdits", "");
    add_platform(s, "platform");
    add_git_rules(s, "du layer");
    return s;
}

Spec make_module() {
    Spec s;
    s.sections = {{"module", "Module", "Bibliothèque du layer."}};
    auto F = [&](std::string sec, std::string key, FT t, std::string label, std::string help,
                 std::vector<std::string> choices = {}, std::string ph = {}) {
        s.fields.push_back({std::move(sec), std::move(key), t, std::move(label), std::move(help), std::move(choices), std::move(ph)});
    };
    F("module", "name", FT::String, "Nom", "Identique au nom du dossier.");
    F("module", "version", FT::String, "Version", "", {}, "0.1.0");
    F("module", "description", FT::String, "Description", "");
    F("module", "type", FT::Enum, "Type", "static, shared ou header-only.", {"static", "shared", "header-only"}, "static");
    F("module", "language", FT::Enum, "Langage", "", kLang);
    F("module", "cxx_standard", FT::Enum, "Standard C++", "Vide = standard du projet.", cxx_std_choices());
    F("module", "c_standard", FT::Enum, "Standard C", "Vide = standard du projet.", c_std_choices());
    F("module", "artifact", FT::String, "Nom de l'artefact", "lib<nom>.a (défaut : nom du module).");
    F("module", "install", FT::Boolean, "Installé", "Inclus par install/package.", {}, "true");
    add_target_common(s, false);
    return s;
}

Spec make_application() {
    Spec s;
    s.sections = {{"application", "Application", "Programme exécutable."},
                  {"run", "Exécution", "Paramètres de « cpp-maker run »."}};
    auto F = [&](std::string sec, std::string key, FT t, std::string label, std::string help,
                 std::vector<std::string> choices = {}, std::string ph = {}) {
        s.fields.push_back({std::move(sec), std::move(key), t, std::move(label), std::move(help), std::move(choices), std::move(ph)});
    };
    F("application", "name", FT::String, "Nom", "Identique au nom du dossier.");
    F("application", "version", FT::String, "Version", "", {}, "0.1.0");
    F("application", "description", FT::String, "Description", "");
    F("application", "language", FT::Enum, "Langage", "", kLang);
    F("application", "cxx_standard", FT::Enum, "Standard C++", "Vide = standard du projet.", cxx_std_choices());
    F("application", "c_standard", FT::Enum, "Standard C", "Vide = standard du projet.", c_std_choices());
    F("application", "main", FT::String, "Source principale", "Fichier contenant main() (défaut : détection).", {}, "src/main.cpp");
    F("application", "artifact", FT::String, "Nom de l'exécutable", "Défaut : nom de l'application.");
    F("application", "install", FT::Boolean, "Installée", "", {}, "true");
    F("run", "args", FT::StringList, "Arguments", "Arguments par défaut de « run ».");
    F("run", "cwd", FT::String, "Répertoire", "Répertoire de travail (relatif à la racine).");
    F("run", "env", FT::Map, "Environnement", "Variables d'environnement.");
    add_target_common(s, true);
    return s;
}

const Spec& spec(FileKind k) {
    static const Spec p = make_project(), l = make_layer(), m = make_module(), a = make_application();
    switch (k) {
    case FileKind::Project: return p;
    case FileKind::Layer: return l;
    case FileKind::Module: return m;
    case FileKind::Application: return a;
    }
    return p;
}

bool section_matches(const std::string& pattern, const std::vector<std::string>& section) {
    auto parts = toml::split_path(pattern);
    if (parts.size() != section.size()) return false;
    for (size_t i = 0; i < parts.size(); ++i)
        if (parts[i] != "*" && parts[i] != section[i]) return false;
    return true;
}

} // namespace

const std::vector<Section>& sections(FileKind k) { return spec(k).sections; }

std::vector<const Field*> fields(FileKind k, const std::string& section_pattern) {
    std::vector<const Field*> r;
    for (auto& f : spec(k).fields)
        if (f.section == section_pattern) r.push_back(&f);
    return r;
}

const Field* find(FileKind k, const std::vector<std::string>& section, const std::string& key) {
    for (auto& f : spec(k).fields)
        if (f.key == key && section_matches(f.section, section)) return &f;
    return nullptr;
}

const Section* find_section(FileKind k, const std::vector<std::string>& section) {
    for (auto& s : spec(k).sections)
        if (section_matches(s.path, section)) return &s;
    return nullptr;
}

toml::Type toml_type(const Field& f) {
    switch (f.type) {
    case FT::String:
    case FT::Enum: return toml::Type::String;
    case FT::Integer: return toml::Type::Integer;
    case FT::Boolean: return toml::Type::Boolean;
    case FT::StringList: return toml::Type::Array;
    case FT::Map: return toml::Type::Table;
    }
    return toml::Type::String;
}

std::string check(const Field& f, const toml::Value& v) {
    switch (f.type) {
    case FT::String:
        if (!v.is_string()) return std::format("{} attendu, {} trouvé", toml::type_name(toml::Type::String), toml::type_name(v.type()));
        return {};
    case FT::Enum:
        if (!v.is_string()) return "chaîne attendue";
        if (!f.choices.empty() && !util::contains(f.choices, v.as_string()))
            return std::format("valeur '{}' invalide (choix : {})", v.as_string(), util::join(f.choices, ", "));
        return {};
    case FT::Integer:
        if (!v.is_integer()) return "entier attendu";
        return {};
    case FT::Boolean:
        if (!v.is_bool()) return "booléen attendu (true/false)";
        return {};
    case FT::StringList: {
        bool ok = false;
        if (v.is_string()) return {};   // une chaîne seule vaut liste d'un élément
        (void)v.as_strings(&ok);
        if (!ok) return "tableau de chaînes attendu";
        return {};
    }
    case FT::Map:
        if (!v.is_table()) return "table attendue ({ CLE = \"valeur\" })";
        for (size_t i = 0; i < v.size(); ++i)
            if (!v.value_at(i).is_string()) return std::format("'{}' : chaîne attendue", v.keys()[i]);
        return {};
    }
    return {};
}

toml::Value parse_input(const Field& f, const std::string& text) {
    std::string err;
    auto v = toml::value_from_text(text, toml_type(f), &err);
    if (!v) util::fail("{}.{} : {}", f.section, f.key, err);
    if (auto e = check(f, *v); !e.empty()) util::fail("{}.{} : {}", f.section, f.key, e);
    return *v;
}

namespace {

void lint_table(FileKind k, const toml::Value& t, std::vector<std::string>& prefix,
                const std::string& file, std::vector<std::string>& out) {
    for (size_t i = 0; i < t.size(); ++i) {
        const std::string& key = t.keys()[i];
        const toml::Value& v = t.value_at(i);
        if (const Field* f = find(k, prefix, key)) {
            if (auto e = check(*f, v); !e.empty())
                out.push_back(std::format("{}: [{}] {} : {}", file, toml::join_path(prefix), key, e));
            continue;
        }
        if (v.is_table()) {
            prefix.push_back(key);
            bool known = find_section(k, prefix) != nullptr;
            // Préfixe d'une section connue (« build » pour « build.modes.* ») ?
            bool partial = false;
            for (auto& s : sections(k)) {
                auto parts = toml::split_path(s.path);
                if (parts.size() <= prefix.size()) continue;
                bool m = true;
                for (size_t j = 0; j < prefix.size(); ++j)
                    if (parts[j] != "*" && parts[j] != prefix[j]) m = false;
                if (m) partial = true;
            }
            if (known || partial) lint_table(k, v, prefix, file, out);
            else {
                std::vector<std::string> names;
                for (auto& s : sections(k)) names.push_back(s.path);
                std::string hint = util::suggest(toml::join_path(prefix), names);
                out.push_back(std::format("{}: section inconnue [{}]{}", file, toml::join_path(prefix),
                                          hint.empty() ? "" : " (vouliez-vous dire [" + hint + "] ?)"));
            }
            prefix.pop_back();
            continue;
        }
        std::vector<std::string> names;
        for (auto& f : spec(k).fields)
            if (find_section(k, prefix) && section_matches(f.section, prefix)) names.push_back(f.key);
        std::string hint = util::suggest(key, names);
        out.push_back(std::format("{}: clé inconnue '{}' dans [{}]{}", file, key,
                                  prefix.empty() ? "racine" : toml::join_path(prefix),
                                  hint.empty() ? "" : " (vouliez-vous dire '" + hint + "' ?)"));
    }
}

} // namespace

std::vector<std::string> lint(FileKind k, const toml::Value& root, const std::string& file) {
    std::vector<std::string> out;
    std::vector<std::string> prefix;
    lint_table(k, root, prefix, file, out);
    return out;
}

bool split_key(FileKind k, const std::string& dotted, std::vector<std::string>& section, std::string& key) {
    auto parts = toml::split_path(dotted);
    if (parts.size() < 2) return false;
    for (size_t n = parts.size() - 1; n >= 1; --n) {
        std::vector<std::string> sec(parts.begin(), parts.begin() + static_cast<long>(n));
        std::string rest;
        for (size_t i = n; i < parts.size(); ++i) rest += (i > n ? "." : "") + parts[i];
        if (find(k, sec, rest)) {
            section = sec;
            key = rest;
            return true;
        }
    }
    // Clé inconnue : dernière composante = clé.
    section.assign(parts.begin(), parts.end() - 1);
    key = parts.back();
    return false;
}

} // namespace schema
