/* example/yocto-tui/setup/SetupRunner.hpp */
#pragma once

#include "../config/YoctoConfig.hpp"

#include <functional>
#include <string>
#include <vector>

namespace yocto {

enum class StepId {
    CheckPrereqs,
    InstallPackages,
    ConfigureLocale,
    FixAppArmor,
    ConfigureGit,
    CreateTree,
    ClonePoky,
    CheckoutTag,
    InitBuildDir,
    CreateEnvScript,
    FirstBuild,
};

enum class StepStatus { Pending, Running, Done, Failed, Skipped };

struct SetupStep {
    StepId id;
    std::string label;
    std::string description;
};

/// Orchestre la préparation complète d'un lab Yocto, en miroir du script
/// setup-vm.sh fourni par la formation. Chaque étape est idempotente :
/// relancer la séquence après un échec partiel ne refait pas le travail
/// déjà accompli.
///
/// Toutes les méthodes publiques sont bloquantes et destinées à être
/// exécutées dans un thread de travail. Les callbacks on_output /
/// on_status sont appelés depuis ce thread : l'appelant doit les
/// marshaler sur le thread UI via App::post().
class SetupRunner {
public:
    using OutputCallback = std::function<void(const std::string&)>;
    using StatusCallback = std::function<void(StepId, StepStatus, const std::string&)>;

    explicit SetupRunner(YoctoConfig config);

    [[nodiscard]] const std::vector<SetupStep>& steps() const { return steps_; }
    [[nodiscard]] const YoctoConfig& config() const { return config_; }

    void set_on_output(OutputCallback cb) { on_output_ = std::move(cb); }
    void set_on_status(StatusCallback cb) { on_status_ = std::move(cb); }

    /// Lance toutes les étapes activées, dans l'ordre. S'arrête à la
    /// première étape en échec (comme `set -e` dans le shell).
    void run();

    /// Lance une seule étape (bouton "Réessayer" ciblé).
    bool run_step(StepId id);

    [[nodiscard]] StepStatus status(StepId id) const;

private:
    // Chaque étape retourne true si elle s'est terminée avec succès (ou
    // a été sautée car déjà faite). Le message est affiché à l'UI.
    bool step_check_prereqs(std::string& msg);
    bool step_install_packages(std::string& msg);
    bool step_configure_locale(std::string& msg);
    bool step_fix_apparmor(std::string& msg);
    bool step_configure_git(std::string& msg);
    bool step_create_tree(std::string& msg);
    bool step_clone_poky(std::string& msg);
    bool step_checkout_tag(std::string& msg);
    bool step_init_build_dir(std::string& msg);
    bool step_create_env_script(std::string& msg);
    bool step_first_build(std::string& msg);

    bool is_enabled(StepId id) const;
    void emit_status(StepId id, StepStatus s, const std::string& msg = {});
    void log(const std::string& line);

    /// Exécute `cmd` (avec sudo si root=true), streame sa sortie au
    /// callback on_output_, retourne true ssi exit_code == 0.
    bool exec(const std::string& cmd, bool root, std::string& err_out);

    YoctoConfig config_;
    std::vector<SetupStep> steps_;
    std::vector<StepStatus> statuses_;
    OutputCallback on_output_;
    StatusCallback on_status_;
};

} // namespace yocto