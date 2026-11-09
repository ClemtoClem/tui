/* apps/yocto-tui/config/ConfigManager.hpp */
#pragma once

#include "YoctoConfig.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace yocto {

/// Gère le répertoire `configs/` : liste, charge et sauvegarde les
/// fichiers `config_yocto_<nom>.ini`. Le répertoire est résolu dans
/// l'ordre : $YOCTO_TUI_CONFIG_DIR, puis ./configs/ relatif au CWD.
class ConfigManager {
public:
    ConfigManager();

    [[nodiscard]] const std::filesystem::path& config_dir() const { return config_dir_; }

    /// Noms des configs existantes (sans le préfixe/suffixe). Triés.
    [[nodiscard]] std::vector<std::string> list() const;

    /// Charge `config_yocto_<name>.ini`. Applique apply_defaults().
    [[nodiscard]] std::optional<YoctoConfig> load(const std::string& name, std::string& error) const;

    /// Écrit `config_yocto_<name>.ini` de façon atomique (fichier
    /// temporaire + rename), pour ne jamais laisser un fichier tronqué
    /// si l'écriture est interrompue.
    bool save(const YoctoConfig& config, std::string& error) const;

    /// Vrai si un fichier existe déjà pour ce nom.
    [[nodiscard]] bool exists(const std::string& name) const;

    /// Supprime `config_yocto_<name>.ini`.
    bool remove(const std::string& name, std::string& error) const;

    /// Nom valide : [A-Za-z0-9_-]+ (pas d'espace, pas de '/', pas de '.').
    [[nodiscard]] static bool is_valid_name(const std::string& name);

    /// Chemin absolu d'une config par son nom.
    [[nodiscard]] std::filesystem::path path_for(const std::string& name) const;

    /// Nom de la dernière configuration ouverte (persisté dans
    /// <config_dir>/last_config). Retourne "" si aucune ou si le
    /// fichier correspondant a disparu. Permet de rouvrir la session
    /// sur la config en cours.
    [[nodiscard]] std::string last_used() const;

    /// Mémorise `name` comme dernière config ouverte. Silencieux en
    /// cas d'échec d'écriture (simple confort, pas critique).
    void set_last_used(const std::string& name) const;

private:
    [[nodiscard]] std::filesystem::path last_used_path() const;

    std::filesystem::path config_dir_;
};

} // namespace yocto