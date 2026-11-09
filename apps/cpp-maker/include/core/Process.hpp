/* @file core/Process.hpp
 *
 * Lancement de processus (posix_spawn) : capture combinée stdout+stderr,
 * répertoire de travail, variables d'environnement additionnelles et
 * délai maximal (le groupe de processus est tué à l'expiration).
 */
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace proc {

struct Result {
    int exit_code = 0;
    bool signaled = false;
    int signal = 0;
    bool timed_out = false;
    std::string output;   ///< stdout+stderr si `capture`
};

struct Options {
    bool capture = false;
    std::optional<std::filesystem::path> cwd;
    int timeout_sec = 0;
    std::vector<std::pair<std::string, std::string>> env{};
};

Result run(const std::vector<std::string>& args, const Options& o = {});

inline Result capture(const std::vector<std::string>& args) {
    Options o;
    o.capture = true;
    return run(args, o);
}

/// Exécute une ligne de commande via /bin/sh -c.
inline Result shell(const std::string& command, const Options& o = {}) {
    return run({"/bin/sh", "-c", command}, o);
}

} // namespace proc
