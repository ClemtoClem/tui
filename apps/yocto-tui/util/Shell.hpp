/* example/yocto-tui/util/Shell.hpp */
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace yocto {

struct ShellResult {
    int exit_code = -1;
    std::string output;   // stdout+stderr fusionnés
    std::string command;  // commande réellement exécutée
};

/// Échappe `s` pour insertion sûre dans une commande shell.
[[nodiscard]] std::string shell_quote(const std::string& s);

/// Construit une commande à partir de tokens déjà séparés.
[[nodiscard]] std::string build_command(const std::vector<std::string>& tokens);

/// Vrai si `name` est un exécutable trouvé dans le PATH.
[[nodiscard]] bool command_exists(const std::string& name);

/// Exécute `cmd` via /bin/sh -c, capture stdout+stderr, streame chaque
/// ligne complète à `on_line` si fourni. Bloquant.
[[nodiscard]] ShellResult run_shell(const std::string& cmd,
                                    std::function<void(const std::string&)> on_line = nullptr);

/// Idem, mais préfixe par `sudo -n` (non-interactif). L'appelant doit
/// avoir préalablement rafraîchi les droits sudo (voir SetupPanel).
[[nodiscard]] ShellResult run_shell_root(const std::string& cmd,
                                         std::function<void(const std::string&)> on_line = nullptr);

/// Vrai si l'utilisateur peut exécuter sudo sans mot de passe (dans la
/// fenêtre courante). Utilise `sudo -n true`.
[[nodiscard]] bool sudo_is_ready();

/// Rafraîchit les droits sudo en demandant le mot de passe (interactif :
/// à lancer UNIQUEMENT depuis le Terminal intégré, jamais en popen).
[[nodiscard]] std::string sudo_refresh_command();

} // namespace yocto