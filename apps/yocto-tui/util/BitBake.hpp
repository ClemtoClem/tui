// util/BitBake.hpp
#pragma once

#include <functional>
#include <string>
#include <vector>
#include <filesystem>

namespace yocto {

struct BitBakeResult {
    int exit_code = -1;
    std::string output;   // stdout+stderr fusionnés
    std::string command;  // commande exacte exécutée
};

class BitBake {
public:
    explicit BitBake(std::filesystem::path build_dir);

    /// Exécute une commande bitbake dans le répertoire de build.
    /// Bloquant ; retourne le résultat complet.
    BitBakeResult run(const std::vector<std::string>& args);

    /// Exécute une commande bitbake avec callback de progression ligne par ligne.
    /// Utile pour parser les tâches "NOTE: Running task X of Y".
    BitBakeResult run_streaming(const std::vector<std::string>& args,
                                 std::function<void(const std::string&)> on_line);

    /// Raccourcis
    BitBakeResult build_image(const std::string& image);
    BitBakeResult build_recipe(const std::string& recipe);
    BitBakeResult menuconfig(const std::string& recipe);
    BitBakeResult list_layers();
    BitBakeResult show_recipe_info(const std::string& recipe);
    BitBakeResult devtool_modify(const std::string& recipe);

    [[nodiscard]] const std::filesystem::path& build_dir() const { return build_dir_; }

    /// Exécute une commande shell arbitraire dans le même environnement
    /// que run() (répertoire de build, oe-init-build-env sourcé). Utile
    /// pour bitbake-layers, devtool, bitbake-getvar, runqemu...
    BitBakeResult run_shell(const std::string& command);

private:
    std::filesystem::path build_dir_;
    std::string quote(const std::string& s) const;
    std::string build_command(const std::vector<std::string>& args) const;
    
    /// Construit "cd <build_dir> && source ../poky/oe-init-build-env . && <inner>"
    [[nodiscard]] std::string wrap_env(const std::string& inner) const;
};

} // namespace yocto