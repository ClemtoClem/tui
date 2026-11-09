// util/Workspace.hpp
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace yocto {

struct WorkspaceInfo {
    std::filesystem::path build_dir;
    std::filesystem::path source_dir;
    std::string distro;
    std::string machine;
    std::string image_recipe;
    bool valid = false;
};

class Workspace {
public:
    /// Détecte un workspace Yocto dans `start_dir` en cherchant
    /// conf/local.conf, conf/bblayers.conf et le script oe-init-build-env.
    static std::optional<WorkspaceInfo> detect(const std::filesystem::path& start_dir);

    /// Lit une variable depuis local.conf (parseur simple clé = "valeur").
    static std::optional<std::string> read_local_conf_var(
        const std::filesystem::path& build_dir, const std::string& key);

    /// Écrit ou remplace une variable dans local.conf (append si absente).
    static bool write_local_conf_var(const std::filesystem::path& build_dir,
                                     const std::string& key, const std::string& value);
};

} // namespace yocto