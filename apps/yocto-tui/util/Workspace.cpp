// util/Workspace.cpp
#include "Workspace.hpp"
#include <fstream>
#include <sstream>

namespace yocto {

std::optional<WorkspaceInfo> Workspace::detect(const std::filesystem::path& start) {
    namespace fs = std::filesystem;
    fs::path dir = fs::absolute(start);

    // Remonte les parents jusqu'à trouver conf/local.conf
    while (!dir.empty() && dir != dir.root_path()) {
        fs::path local_conf = dir / "conf" / "local.conf";
        fs::path bblayers = dir / "conf" / "bblayers.conf";
        if (fs::exists(local_conf) && fs::exists(bblayers)) {
            WorkspaceInfo info;
            info.build_dir = dir;
            // Le répertoire parent contient généralement les sources poky
            if (fs::exists(dir.parent_path() / "oe-init-build-env")) {
                info.source_dir = dir.parent_path();
            }
            auto distro = read_local_conf_var(dir, "DISTRO");
            auto machine = read_local_conf_var(dir, "MACHINE");
            auto image = read_local_conf_var(dir, "IMAGE_INSTALL_append");
            info.distro = distro.value_or("(non défini)");
            info.machine = machine.value_or("(non défini)");
            info.image_recipe = "core-image-minimal"; // défaut
            info.valid = true;
            return info;
        }
        dir = dir.parent_path();
    }
    return std::nullopt;
}

std::optional<std::string> Workspace::read_local_conf_var(
    const std::filesystem::path& build_dir, const std::string& key) {
    std::ifstream f(build_dir / "conf" / "local.conf");
    if (!f) return std::nullopt;

    std::string line;
    std::string prefix = key + " =";
    std::string prefix2 = key + " ?=";
    std::string prefix3 = key + " ??=";
    while (std::getline(f, line)) {
        auto trim = [](std::string& s) {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(0, 1);
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.pop_back();
        };
        trim(line);
        for (const auto& p : {prefix, prefix2, prefix3}) {
            if (line.rfind(p, 0) == 0) {
                std::string val = line.substr(p.size());
                // Retire les guillemets
                if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
                    val = val.substr(1, val.size() - 2);
                trim(val);
                return val;
            }
        }
    }
    return std::nullopt;
}

bool Workspace::write_local_conf_var(const std::filesystem::path& build_dir,
                                      const std::string& key, const std::string& value) {
    auto path = build_dir / "conf" / "local.conf";
    std::ifstream in(path);
    if (!in) return false;

    std::vector<std::string> lines;
    std::string line;
    bool replaced = false;
    std::string prefix = key + " =";
    while (std::getline(in, line)) {
        if (!replaced && line.rfind(prefix, 0) == 0) {
            lines.push_back(key + " = \"" + value + "\"");
            replaced = true;
        } else {
            lines.push_back(line);
        }
    }
    in.close();

    if (!replaced) {
        lines.push_back("");
        lines.push_back("# Ajouté par yocto-tui");
        lines.push_back(key + " = \"" + value + "\"");
    }

    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;
    for (const auto& l : lines) out << l << "\n";
    return true;
}

} // namespace yocto