/* apps/yocto-tui/runner/BuildRunner.hpp */
#pragma once

#include "../config/YoctoConfig.hpp"

#include <atomic>
#include <functional>
#include <string>
#include <sys/types.h>
#include <thread>

namespace yocto {

/// Lance bitbake dans l'environnement du lab (cd <poky> + source
/// oe-init-build-env <build_dir> + bitbake <cible>) dans un processus
/// séparé :
///
///  - la sortie (stdout + stderr) est streamee ligne par ligne vers
///    on_output (appelé depuis le thread lecteur : marshaler vers le
///    thread UI avec App::post) ;
///  - le processus tourne dans son propre groupe (setsid), ce qui
///    permet de l'annuler proprement par SIGINT sans perturber le TUI :
///    bitbake réagit à SIGINT en s'arrêtant en conservant l'état déjà
///    compilé (la reprise du build est ensuite très rapide) ;
///  - on_done est appelé une fois le processus terminé, depuis le
///    thread lecteur.
class BuildRunner {
public:
    using OutputCallback = std::function<void(const std::string&)>;
    using DoneCallback = std::function<void(bool ok, int exit_code, const std::string& msg)>;

    explicit BuildRunner(YoctoConfig config);

    /// Interrompt un éventuel build en cours (SIGKILL sur le groupe)
    /// et attend le thread lecteur.
    ~BuildRunner();

    BuildRunner(const BuildRunner&) = delete;
    BuildRunner& operator=(const BuildRunner&) = delete;

    /// Configuration utilisée au prochain start(). Copie + defaults.
    void set_config(YoctoConfig config);

    void set_on_output(OutputCallback cb) { on_output_ = std::move(cb); }
    void set_on_done(DoneCallback cb) { on_done_ = std::move(cb); }

    /// Lance `bitbake <target>`. Retourne false si un build tourne
    /// déjà ou si le lancement échoue (pipe/fork).
    bool start(const std::string& target);

    /// Demande l'arrêt du build en cours (SIGINT sur le groupe de
    /// processus). Retourne false si aucun build ne tourne.
    bool cancel();

    [[nodiscard]] bool running() const { return running_.load(); }

private:
    YoctoConfig config_;
    std::atomic<bool> running_{false};
    std::atomic<bool> cancel_requested_{false};
    std::atomic<pid_t> child_pid_{-1};
    std::thread reader_;
    OutputCallback on_output_;
    DoneCallback on_done_;
};

} // namespace yocto