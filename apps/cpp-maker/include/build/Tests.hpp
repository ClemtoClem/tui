/* @file build/Tests.hpp
 *
 * Compilation et exécution des tests : un exécutable <cible>-tests par
 * cible, code 0 = réussite, 77 = tout ignoré, autre = échec. Rapport JUnit
 * optionnel.
 */
#pragma once

#include "build/Session.hpp"

#include <string>
#include <vector>

namespace build {

struct TestResult {
    std::string suite;    ///< <toolchain>/<mode>
    std::string name;     ///< cible
    std::string status;   ///< pass | fail | skip | timeout
    double seconds = 0;
    std::string output;
    int code = 0;
};

struct TestOptions {
    std::string filter;
    std::string junit;
    int timeout = -1;   ///< -1 = [tests].timeout
    std::vector<std::string> passthrough;
};

/// Compile et exécute les tests de `mode` pour chaque toolchain demandée ;
/// ajoute les résultats à `out`. Renvoie le nombre d'échecs.
int run_tests(const model::Workspace& ws, const Request& r, const std::vector<std::string>& targets,
              const std::string& mode, const TestOptions& o, std::vector<TestResult>& out);

void write_junit(const fs::path& file, const std::vector<TestResult>& results);
/// Affiche le résumé ; renvoie le nombre d'échecs.
int print_test_summary(const std::vector<TestResult>& results);

} // namespace build
