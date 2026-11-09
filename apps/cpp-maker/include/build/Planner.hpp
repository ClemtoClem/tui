/* @file build/Planner.hpp
 *
 * Construction du plan de build (DAG de tâches) pour un triplet
 * (toolchain, mode, cibles sélectionnées) :
 *   1. ordre topologique des cibles et de leurs dépendances ;
 *   2. découverte des sources, résolution pkg-config, chemins d'artefacts ;
 *   3. tâches de compilation puis d'archivage / édition de liens ;
 *   4. (option) tâches des exécutables de test.
 *
 * Arborescence produite sous <build>/<toolchain>/<mode>/ :
 *   obj/…  bin/<app>  lib/lib<module>.a|.so  tests/<cible>-tests  support/
 */
#pragma once

#include "build/Executor.hpp"
#include "model/Workspace.hpp"
#include "toolchain/Toolchain.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace build {

enum class Lang { C, CXX, ASM };
std::optional<Lang> lang_of(const fs::path& p);

struct Source {
    fs::path path;
    Lang lang;
};

struct TargetPlan {
    const model::TargetConfig* cfg = nullptr;
    std::vector<Source> sources;
    std::optional<Source> main;
    std::vector<Source> tests;
    std::vector<std::string> closure;   ///< dépendances transitives, dépendances d'abord
    std::vector<std::string> pc_cflags, pc_libs;
    bool has_cxx = false, has_c = false;
    fs::path artifact, import_lib;
    std::optional<size_t> link_task;
    std::vector<fs::path> objects;
    std::vector<size_t> object_tasks;
    std::optional<size_t> main_task;
    fs::path main_object;
    fs::path test_exe;
    std::optional<size_t> test_task;
    std::string framework;   ///< framework de test effectif
};

struct Plan {
    const model::Workspace* ws = nullptr;
    const toolchain::Toolchain* tc = nullptr;
    const model::ModeConfig* mode = nullptr;
    fs::path build_dir;
    std::vector<Task> tasks;
    std::vector<std::string> order;      ///< cibles du plan (dépendances d'abord)
    std::vector<std::string> selected;   ///< cibles demandées
    std::map<std::string, TargetPlan> targets;
    bool with_tests = false;
    std::optional<size_t> test_main_task;
    fs::path test_main_object;
    fs::path support_dir;
};

fs::path build_dir_for(const model::Workspace& ws, const std::string& tc, const std::string& mode);
bool glob_match(std::string_view pattern, std::string_view s);
std::vector<Source> discover_sources(const model::Workspace& ws, const model::TargetConfig& t,
                                     const std::vector<std::string>& dirs);
bool platform_matches(const std::string& key, const std::string& os);
std::vector<std::string> pkg_query(const toolchain::Toolchain& t, const std::string& pkg, const std::string& what);
std::string artifact_name(const toolchain::Toolchain& t, const model::TargetConfig& m);

Plan make_plan(const model::Workspace& ws, const toolchain::Toolchain& tc, const model::ModeConfig& mode,
               const std::vector<std::string>& selected, bool with_tests);

} // namespace build
