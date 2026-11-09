/* apps/yocto-tui/runners/ProjectRunner.hpp */
#pragma once

#include "../config/YoctoConfig.hpp"
#include "../project/LayerGenerator.hpp"
#include "SetupRunner.hpp"

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace yocto {

enum class ProjectStepId {
    FetchLayers,     // git clone / checkout des couches externes
    WriteLayer,      // écriture du layer (recettes, bbappends, image)
    ConfigureBuild,  // blocs gérés de local.conf et bblayers.conf
    CheckLayers,     // bitbake-layers show-layers (vérification)
    ApplyPatches,    // devtool modify -> remplacement -> commit -> finish
};

struct ProjectStep {
    ProjectStepId id;
    std::string label;
    std::string description;
};

/// Applique un projet (ProjectSpec) au lab : équivalent des commandes
/// layers / config / patch-bc de tp-yocto.sh. Chaque étape est
/// idempotente et peut être relancée seule.
///
/// En mode simulation (dry_run), aucune commande n'est exécutée et aucun
/// fichier n'est écrit : le journal décrit ce qui serait fait. Utile
/// pour tout vérifier visuellement sans lancer BitBake.
///
/// Méthodes bloquantes, à appeler depuis un thread de travail ; les
/// callbacks sont émis depuis ce thread (marshaler avec App::post()).
class ProjectRunner {
public:
    using OutputCallback = std::function<void(const std::string&)>;
    using StatusCallback = std::function<void(ProjectStepId, StepStatus, const std::string&)>;

    ProjectRunner(YoctoConfig config, bool dry_run, PasswordHasher hasher);

    [[nodiscard]] const std::vector<ProjectStep>& steps() const { return steps_; }

    void set_on_output(OutputCallback cb) { on_output_ = std::move(cb); }
    void set_on_status(StatusCallback cb) { on_status_ = std::move(cb); }

    /// Lance les étapes de `enabled` dans l'ordre ; arrêt à la première
    /// erreur. Les autres sont marquées « sautées ».
    void run(const std::vector<ProjectStepId>& enabled);
    bool run_step(ProjectStepId id);

    /// Interrompt la séquence avant l'étape suivante.
    void request_stop() { stop_requested_ = true; }

private:
    bool step_fetch_layers(std::string& msg);
    bool step_write_layer(std::string& msg);
    bool step_configure_build(std::string& msg);
    bool step_check_layers(std::string& msg);
    bool step_apply_patches(std::string& msg);

    bool patch_recipe(const std::string& recipe, const std::vector<const PatchSpec*>& patches,
                      std::string& msg);

    void log(const std::string& line);
    void emit(ProjectStepId id, StepStatus s, const std::string& msg = {});

    /// Exécute une commande (ou la journalise en simulation).
    bool exec(const std::string& cmd, std::string& err);
    /// Idem, dans l'environnement BitBake (oe-init-build-env sourcé).
    bool exec_env(const std::string& cmd, std::string& err);

    YoctoConfig config_;
    bool dry_run_;
    PasswordHasher hasher_;
    std::vector<ProjectStep> steps_;
    std::atomic<bool> stop_requested_{false};
    OutputCallback on_output_;
    StatusCallback on_status_;
};

/// Recherche le premier fichier texte de `root` (hors .git) contenant
/// `needle` ; chemin vide si aucun. Exposé pour les tests.
[[nodiscard]] std::filesystem::path find_file_containing(const std::filesystem::path& root,
                                                         const std::string& needle);

/// Remplace la première (ou toutes les) occurrence(s) de `search` ;
/// retourne le nombre de remplacements.
size_t replace_in_string(std::string& text, const std::string& search, const std::string& replace,
                         bool all);

} // namespace yocto
