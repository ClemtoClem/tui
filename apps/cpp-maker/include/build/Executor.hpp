/* @file build/Executor.hpp
 *
 * Tâches de build et exécution parallèle du DAG.
 *
 * Une tâche est à jour si ses sorties existent et si son empreinte
 * (FNV-1a de la version de l'outil, de la ligne de commande et de chaque
 * entrée : chemin, taille, mtime — les dépendances #include du fichier .d
 * remplaçant les entrées pour les compilations) est égale à celle stockée
 * dans <sortie>.stamp lors de la dernière exécution réussie.
 */
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace build {

struct Task {
    std::string kind;    ///< CC, CXX, AS, AR, LINK, SOLINK
    std::string label;   ///< affiché (chemin relatif à la racine)
    std::vector<std::string> cmd;
    std::vector<fs::path> outputs, inputs;
    fs::path source;     ///< compilations uniquement
    fs::path depfile, stamp;
    std::vector<size_t> deps;   ///< indices des tâches préalables
    std::string tool_version;
    bool remove_outputs_first = false;
    [[nodiscard]] bool is_compile() const { return !source.empty(); }
};

std::vector<fs::path> parse_depfile(const fs::path& p);
std::string fingerprint(const Task& t);
bool up_to_date(const Task& t);
/// Tâches qui seraient exécutées (périmées ou dépendant d'une tâche périmée).
std::vector<char> predict_dirty(const std::vector<Task>& tasks);

/// Workers : au plus (cœurs - 1), au moins 1 ; `requested` 0 = automatique.
int parallel_jobs(int requested = 0);

struct ExecOptions {
    int jobs = 0;
    bool verbose = false;
    bool keep_going = false;
    fs::path root;   ///< pour l'affichage des chemins
};

struct ExecStats {
    int ran = 0, uptodate = 0, failed = 0, skipped = 0;
};

ExecStats execute(const std::vector<Task>& tasks, const ExecOptions& o);

} // namespace build
