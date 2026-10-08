/* apps/yocto-tui/runner/BuildRunner.cpp */
#include "BuildRunner.hpp"

#include "../util/Shell.hpp"

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <csignal>

namespace yocto {

BuildRunner::BuildRunner(YoctoConfig config) : config_(std::move(config)) {
    config_.apply_defaults();
}

BuildRunner::~BuildRunner() {
    // Ne pas laisser un bitbake orphelin derrière nous.
    const pid_t pid = child_pid_.load();
    if (pid > 0) {
        kill(-pid, SIGKILL);
    }
    if (reader_.joinable()) {
        reader_.join();
    }
}

void BuildRunner::set_config(YoctoConfig config) {
    config_ = std::move(config);
    config_.apply_defaults();
}

bool BuildRunner::start(const std::string& target) {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return false;  // un build tourne déjà
    }

    // Le lecteur du build précédent est terminé (running_ était false)
    // mais pas encore joiné : le joindre avant de réutiliser reader_,
    // sinon std::thread::operator= appelle std::terminate().
    if (reader_.joinable()) {
        reader_.join();
    }

    const std::string poky = config_.poky_dir().string();
    const std::string bdir = config_.build_dir_path().string();

    // Séquence équivalente à celle du lab (blaess.fr, I.2) :
    //   $ cd <lab>/layers/poky
    //   $ source oe-init-build-env <lab>/builds/build-<machine>
    //   $ bitbake <cible>
    const std::string cmd =
        "cd " + shell_quote(poky) + " && "
        "source oe-init-build-env " + shell_quote(bdir) + " >/dev/null && "
        "exec bitbake " + shell_quote(target);

    int fds[2];
    if (pipe(fds) != 0) {
        running_ = false;
        return false;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        running_ = false;
        return false;
    }

    if (pid == 0) {
        // Fils : nouveau groupe de processus (setsid) pour annuler via
        // kill(-pid) sans toucher au TUI ; stdout+stderr vers le pipe ;
        // stdin sur /dev/null pour que bitbake ne lise pas le clavier.
        setsid();
        dup2(fds[1], STDOUT_FILENO);
        dup2(fds[1], STDERR_FILENO);
        close(fds[0]);
        close(fds[1]);
        const int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            close(devnull);
        }
        execl("/bin/bash", "bash", "-c", cmd.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }

    close(fds[1]);
    child_pid_ = pid;
    cancel_requested_ = false;

    const int out_fd = fds[0];
    reader_ = std::thread([this, out_fd] {
        std::string pending;
        char buf[4096];
        ssize_t n;
        while ((n = read(out_fd, buf, sizeof buf)) > 0) {
            for (ssize_t i = 0; i < n; ++i) {
                const char c = buf[i];
                if (c == '\n') {
                    if (on_output_) on_output_(pending);
                    pending.clear();
                } else if (c != '\r') {
                    pending += c;
                }
            }
        }
        close(out_fd);

        int status = 0;
        waitpid(child_pid_.load(), &status, 0);
        child_pid_ = -1;

        const bool exited = WIFEXITED(status);
        const int code = exited ? WEXITSTATUS(status) : -1;
        const bool ok = exited && code == 0;
        running_ = false;

        if (on_done_) {
            std::string msg;
            if (ok) {
                msg = "build terminé avec succès";
            } else if (cancel_requested_.load()) {
                msg = "build annulé (état conservé, relancez pour reprendre)";
            } else {
                msg = "build échoué (code " + std::to_string(code) + ")";
            }
            on_done_(ok, code, msg);
        }
    });
    return true;
}

bool BuildRunner::cancel() {
    const pid_t pid = child_pid_.load();
    if (pid <= 0) return false;
    cancel_requested_ = true;
    // SIGINT sur tout le groupe : bitbake s'arrête proprement et
    // conserve l'état des tâches déjà réalisées.
    kill(-pid, SIGINT);
    return true;
}

} // namespace yocto