/* apps/yocto-tui/config/ConfigManager.cpp */
#include "ConfigManager.hpp"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <system_error>
#include <algorithm>

namespace yocto {

namespace {

constexpr const char* kPrefix = "config_yocto_";
constexpr const char* kSuffix = ".ini";

std::filesystem::path resolve_config_dir() {
    if (const char* env = std::getenv("YOCTO_TUI_CONFIG_DIR"); env && *env) {
        return std::filesystem::path(env);
    }
    return std::filesystem::current_path() / "configs";
}

std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

} // namespace

ConfigManager::ConfigManager() : config_dir_(resolve_config_dir()) {
    std::error_code ec;
    std::filesystem::create_directories(config_dir_, ec);
}

bool ConfigManager::is_valid_name(const std::string& name) {
    if (name.empty()) return false;
    for (char c : name) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

std::filesystem::path ConfigManager::path_for(const std::string& name) const {
    return config_dir_ / (std::string(kPrefix) + name + kSuffix);
}

bool ConfigManager::exists(const std::string& name) const {
    std::error_code ec;
    return std::filesystem::exists(path_for(name), ec);
}

std::vector<std::string> ConfigManager::list() const {
    std::vector<std::string> names;
    std::error_code ec;
    if (!std::filesystem::exists(config_dir_, ec)) return names;

    for (const auto& entry : std::filesystem::directory_iterator(config_dir_, ec)) {
        if (!entry.is_regular_file()) continue;
        std::string fname = entry.path().filename().string();
        if (fname.size() <= std::strlen(kPrefix) + std::strlen(kSuffix)) continue;
        if (fname.rfind(kPrefix, 0) != 0) continue;
        if (fname.compare(fname.size() - std::strlen(kSuffix), std::strlen(kSuffix), kSuffix) != 0) {
            continue;
        }
        std::string name = fname.substr(std::strlen(kPrefix),
                                         fname.size() - std::strlen(kPrefix) - std::strlen(kSuffix));
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::optional<YoctoConfig> ConfigManager::load(const std::string& name, std::string& error) const {
    auto path = path_for(name);
    std::ifstream in(path);
    if (!in) {
        error = "impossible d'ouvrir " + path.string();
        return std::nullopt;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    YoctoConfig cfg = YoctoConfig::from_ini(ss.str(), error);
    if (!error.empty()) return std::nullopt;
    // Le nom stocké dans le fichier doit correspondre au nom de fichier
    // (évite les confusions si un fichier a été renommé à la main).
    cfg.name = name;
    cfg.apply_defaults();
    return cfg;
}

bool ConfigManager::save(const YoctoConfig& config, std::string& error) const {
    if (!is_valid_name(config.name)) {
        error = "nom de config invalide (attendu : [A-Za-z0-9_-]+)";
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(config_dir_, ec);

    auto target = path_for(config.name);
    auto tmp = target;
    tmp += ".tmp";

    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) {
            error = "impossible d'écrire " + tmp.string();
            return false;
        }
        out << config.to_ini();
        if (!out) {
            error = "erreur d'écriture sur " + tmp.string();
            return false;
        }
    }
    std::filesystem::rename(tmp, target, ec);
    if (ec) {
        error = "renommage " + tmp.string() + " -> " + target.string() + " : " + ec.message();
        return false;
    }
    return true;
}

bool ConfigManager::remove(const std::string& name, std::string& error) const {
    std::error_code ec;
    if (!std::filesystem::remove(path_for(name), ec)) {
        error = "suppression impossible : " + ec.message();
        return false;
    }
    return true;
}

std::filesystem::path ConfigManager::last_used_path() const {
    return config_dir_ / "last_config";
}

std::string ConfigManager::last_used() const {
    std::ifstream in(last_used_path());
    std::string name;
    if (!in || !std::getline(in, name)) return "";

    name = trim(name);
    if (!is_valid_name(name) || !exists(name)) return "";
    return name;
}

void ConfigManager::set_last_used(const std::string& name) const {
    std::ofstream out(last_used_path(), std::ios::trunc);
    if (out) out << name << "\n";
}

} // namespace yocto