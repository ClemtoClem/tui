/* @file core/Redirect.hpp
 *
 * Options globales de redirection (toutes commandes, avant ou après le nom
 * de la commande, jamais après « -- ») :
 *
 *   --output <f>   stdout + stderr dans <f>
 *   --stdout <f>   stdout dans <f>
 *   --stderr <f>   stderr dans <f>
 *
 * Les trois sont combinables : un flux envoyé vers plusieurs fichiers est
 * dupliqué. Un nom sans dossier (« tests.log ») est rangé dans [paths].out
 * du projet (défaut .out) ; un chemin avec dossier est relatif au dossier
 * courant. Les processus lancés (compilateur, tests…) héritent des
 * redirections : leur sortie est capturée aussi.
 */
#pragma once

#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace redirect {

struct Request {
    std::vector<std::string> output, out, err;   ///< fichiers demandés
    [[nodiscard]] bool any() const { return !output.empty() || !out.empty() || !err.empty(); }
};

/// Retire --output/--stdout/--stderr de `argv` (avant « -- ») ; erreur si valeur manquante.
Request extract(std::vector<std::string>& argv, std::string& error);

/// Dossier des fichiers sans répertoire : [paths].out du projet contenant `cwd`, sinon `cwd`.
std::filesystem::path out_dir(const std::filesystem::path& cwd);

/// Redirection active tant que l'objet vit ; le destructeur vide les flux et restaure.
class Session {
public:
    Session() = default;
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    ~Session();

    /// Ouvre les fichiers et redirige ; message d'erreur (vide si succès).
    std::string start(const Request& r, const std::filesystem::path& cwd);

private:
    struct Stream {
        int fd = -1;            ///< 1 ou 2
        int saved = -1;         ///< copie du descripteur d'origine
        int pipe_read = -1;     ///< tube (plusieurs destinations)
        std::vector<int> files;
        std::thread pump;
    };
    std::vector<int> opened_;
    Stream streams_[2];
    bool active_ = false;

    void stop();
};

} // namespace redirect
