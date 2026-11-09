/* @file toolchain/Toolchain.hpp
 *
 * Toolchain résolue : compilateurs C/C++ sondés (famille, version),
 * outils (ar, strip, pkg-config), cible (triplet, arch, OS) et lanceur
 * pour exécuter les binaires croisés (qemu-<arch>, wine).
 *
 * Toolchains disponibles sans configuration :
 *   host    $CC/$CXX, sinon g++/gcc, clang++/clang
 *   gcc     force gcc/g++
 *   clang   force clang/clang++
 *   <triplet> ou <préfixe>  ex. arm-linux-gnueabihf, x86_64-w64-mingw32
 *   g++-14, clang++-19…     compilateur précis du PATH
 */
#pragma once

#include "model/Model.hpp"

#include <optional>
#include <string>
#include <vector>

namespace toolchain {

enum class Family { Gcc, Clang, Msvc, Tcc, Unknown };
std::string_view family_name(Family f);

struct Tool {
    std::string exe;
    std::vector<std::string> pre_args;   ///< --target=…, --sysroot=…
    Family family = Family::Unknown;
    std::string version;                 ///< première ligne de --version
};

struct Toolchain {
    std::string name;
    model::ToolchainConfig cfg;
    std::optional<Tool> cc, cxx;
    std::string ar, strip, pkg_config;
    std::string triple, arch, os;
    bool cross = false;
    std::vector<std::string> runner;   ///< préfixe d'exécution (émulateur)
    std::string runner_note;           ///< pourquoi l'exécution est impossible

    [[nodiscard]] bool can_execute() const { return !cross || !runner.empty(); }
    [[nodiscard]] std::string exe_suffix() const { return os == "windows" ? ".exe" : ""; }
    [[nodiscard]] const Tool& c_tool() const;     ///< cc, à défaut cxx
    [[nodiscard]] const Tool& cxx_tool() const;   ///< cxx, à défaut cc
    [[nodiscard]] Family family() const { return cxx_tool().family; }
};

// --- Traduction des options selon la famille de compilateur
std::string include_flag(Family f, const std::string& dir);
std::string define_flag(Family f, const std::string& d);
std::string std_flag(Family f, const std::string& standard);
std::vector<std::string> warning_flags(Family f, const std::vector<std::string>& gcc_style);

// --- Détection
std::optional<Tool> probe(const std::string& exe, const std::vector<std::string>& pre = {});
std::string dumpmachine(const Tool& t);
std::string normalize_arch(std::string a);
std::string os_from_triple(const std::string& t);
std::string host_arch();
std::string host_os();
std::string deb_arch(const Toolchain& t);
std::vector<std::string> host_candidates(bool cxx, const std::string& family);
/// Préfixes de compilateurs croisés présents dans le PATH (« arm-linux-gnueabihf- »).
std::vector<std::string> detect_prefixes();
/// Noms de toolchains connus : projet + intégrées + préfixes détectés.
std::vector<std::string> known_names(const model::ProjectConfig& p);

/// Configuration d'une toolchain par son nom (projet, intégrée ou déduite).
std::optional<model::ToolchainConfig> config_for(const model::ProjectConfig& p, const std::string& name);
/// Résout la toolchain : sonde les compilateurs et la cible.
Toolchain resolve(const model::ProjectConfig& p, const std::string& name,
                  const std::string& cc_override = {}, const std::string& cxx_override = {});

/// Compile (et lie) un programme trivial : vérifie qu'un compilateur fonctionne.
bool smoke_compile(const Tool& tool, const std::string& lang, const std::string& standard,
                   std::string* err_out = nullptr, const std::vector<std::string>& extra = {});

} // namespace toolchain
