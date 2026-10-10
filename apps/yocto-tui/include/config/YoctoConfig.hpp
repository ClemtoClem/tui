/* example/yocto-tui/config/YoctoConfig.hpp */
#pragma once

#include <filesystem>
#include <string>

namespace yocto {

/// Configuration complète d'un projet Yocto, persistée en INI. Les
/// valeurs sont dérivées si vides (voir apply_defaults()).
struct YoctoConfig {
    // [general]
    std::string name = "default";
    std::string workdir;                     // vide -> $HOME/yocto-lab
    std::string poky_url = "https://git.yoctoproject.org/poky";
    std::string poky_branch = "scarthgap";
    std::string yocto_tag;                   // vide -> dernier tag yocto-5.0.x
    std::string machine = "qemux86-64";
    int threads = 0;                         // 0 -> std::thread::hardware_concurrency()

    // [build]
    std::string build_dir;                   // vide -> ${workdir}/builds/build-<machine>
    std::string dl_dir;                      // vide -> ${workdir}/cache/downloads
    std::string sstate_dir;                  // vide -> ${workdir}/cache/sstate-cache
    std::string image_recipe = "core-image-minimal";

    // [options]
    bool install_packages = true;
    bool configure_locale = true;
    bool fix_apparmor = true;
    bool configure_git = true;
    bool first_build = false;

    // [git]
    std::string git_user_name  = "Stagiaire Yocto";
    std::string git_user_email = "stagiaire@example.com";

    // --- Sérialisation ---
    [[nodiscard]] std::string to_ini() const;
    [[nodiscard]] static YoctoConfig from_ini(const std::string& content, std::string& error);

    /// Remplit les valeurs dérivées vides (workdir, build_dir, threads,
    /// dl_dir, sstate_dir). Idempotent.
    void apply_defaults();

    // --- Chemins dérivés (calculés après apply_defaults) ---
    [[nodiscard]] std::filesystem::path poky_dir() const;
    [[nodiscard]] std::filesystem::path layers_dir() const;
    [[nodiscard]] std::filesystem::path builds_dir() const;
    [[nodiscard]] std::filesystem::path downloads_dir() const;
    [[nodiscard]] std::filesystem::path sstate_cache_dir() const;
    [[nodiscard]] std::filesystem::path env_script_path() const;
    [[nodiscard]] std::filesystem::path local_conf_path() const;
    [[nodiscard]] std::filesystem::path build_dir_path() const;
};

} // namespace yocto