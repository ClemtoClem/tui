/* @file build/Session.hpp
 *
 * Session de build : toolchain résolue + mode (éventuellement dérivé par
 * --sanitize) + plan + verrou de projet tenu pendant toute la session.
 * Regroupe aussi les exports (compile_commands.json, build.ninja) et les
 * étapes de génération [generate.*].
 */
#pragma once

#include "build/Planner.hpp"
#include "core/Util.hpp"
#include "toolchain/Sanitize.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace build {

/// Options communes des commandes de build (build, run, test, check…).
struct Request {
    std::vector<std::string> modes;        ///< vide = mode par défaut
    std::vector<std::string> toolchains;   ///< vide = toolchain par défaut ; « all »
    std::vector<std::string> selectors;    ///< cibles ; vide = dossier courant / tout
    std::string cc, cxx;
    int jobs = 0;
    bool verbose = false, dry_run = false, keep_going = false, clean = false, tests = false;
    std::string backend = "native";        ///< native | ninja
    sanitize::Request san;
};

class Session {
public:
    const model::Workspace* ws = nullptr;
    std::unique_ptr<toolchain::Toolchain> tc;
    model::ModeConfig mode;
    std::unique_ptr<Plan> plan;
    util::FileLock lock;

    /// Prépare toolchain + mode + plan ; pose le verrou si `with_lock`.
    static Session open(const model::Workspace& ws, const std::string& tc_name, const std::string& mode_name,
                        const std::vector<std::string>& targets, bool with_tests, const Request& r,
                        bool with_lock = true);

    void print_header() const;
    /// Génération + exécution du plan ; affiche le résumé. 0 si succès.
    int build(const Request& r, const std::string& command);
    /// Chaîne de lancement : [valgrind …] [émulateur …] ; nullopt + raison sinon.
    [[nodiscard]] std::optional<std::vector<std::string>> launcher(std::string& why) const;
};

/// Toolchains demandées (« all » = toutes celles du projet + host).
std::vector<std::string> pick_toolchains(const model::Workspace& ws, const Request& r);
/// Modes demandés (vérifiés), défaut du projet sinon.
std::vector<std::string> pick_modes(const model::Workspace& ws, const Request& r);

void write_compdb(const Plan& pl, const fs::path& out);
fs::path write_ninja(const Plan& pl);
int run_ninja(Session& s, const Request& r);

/// Exécute les étapes [generate.*] déclenchées par `command` dont une entrée a changé.
bool run_generate_steps(const model::Workspace& ws, const std::string& command, bool verbose);

} // namespace build
