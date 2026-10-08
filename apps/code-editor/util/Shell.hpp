#pragma once

#include <string>
#include <vector>

namespace ide {

struct ProcessResult {
    int exit_code = -1;
    std::string output; // stdout+stderr fusionnés
};

/// Échappe `s` pour une insertion sûre dans une commande shell (guillemets
/// simples, avec l'échappement standard des guillemets simples internes).
/// Utilisé pour CHAQUE fragment interpolé (chemin, message de commit,
/// nom de branche...) avant construction d'une commande - jamais de
/// concaténation brute de texte utilisateur dans la ligne de commande.
[[nodiscard]] std::string shell_quote(const std::string& s);

/// Exécute `command` (déjà construite, arguments déjà shell_quote()-és)
/// via /bin/sh -c, dans `working_dir`. Bloquant - réservé aux commandes
/// courtes (git status/log/diff/add/commit...), pas à un processus
/// longue durée (voir plutôt le widget Terminal pour ça).
[[nodiscard]] ProcessResult run_command(const std::string& command, const std::string& working_dir);

/// Construit une commande à partir de tokens déjà séparés, en
/// shell_quote()-ant chacun - évite d'avoir à le faire manuellement à
/// chaque appel de run_command().
[[nodiscard]] std::string build_command(const std::vector<std::string>& tokens);

/// Découpe `s` en lignes sur '\n' (sans les conserver), sans ligne finale
/// vide superflue si `s` se termine déjà par '\n'.
[[nodiscard]] std::vector<std::string> split_lines(const std::string& s);

} // namespace ide
