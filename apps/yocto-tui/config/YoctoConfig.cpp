/* example/yocto-tui/config/YoctoConfig.cpp */
#include "YoctoConfig.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <thread>

namespace yocto {

namespace {

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string home_dir() {
    const char* h = std::getenv("HOME");
    return h ? h : "/tmp";
}

bool parse_bool(const std::string& v) {
    std::string s;
    for (char c : v) s += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s == "true" || s == "yes" || s == "on" || s == "1";
}

} // namespace

// --- Sérialisation -----------------------------------------------------

std::string YoctoConfig::to_ini() const {
    std::ostringstream o;
    o << "# Config Yocto générée par yocto-tui\n";
    o << "# Fichier : configs/config_yocto_" << name << ".ini\n\n";
    o << "[general]\n";
    o << "name = " << name << "\n";
    o << "workdir = " << workdir << "\n";
    o << "poky_url = " << poky_url << "\n";
    o << "poky_branch = " << poky_branch << "\n";
    o << "yocto_tag = " << yocto_tag << "\n";
    o << "machine = " << machine << "\n";
    o << "threads = " << threads << "\n\n";
    o << "[build]\n";
    o << "build_dir = " << build_dir << "\n";
    o << "dl_dir = " << dl_dir << "\n";
    o << "sstate_dir = " << sstate_dir << "\n";
    o << "image_recipe = " << image_recipe << "\n\n";
    o << "[options]\n";
    o << "install_packages = " << (install_packages ? "true" : "false") << "\n";
    o << "configure_locale = " << (configure_locale ? "true" : "false") << "\n";
    o << "fix_apparmor = " << (fix_apparmor ? "true" : "false") << "\n";
    o << "configure_git = " << (configure_git ? "true" : "false") << "\n";
    o << "first_build = " << (first_build ? "true" : "false") << "\n\n";
    o << "[git]\n";
    o << "user_name = " << git_user_name << "\n";
    o << "user_email = " << git_user_email << "\n";
    return o.str();
}

YoctoConfig YoctoConfig::from_ini(const std::string& content, std::string& error) {
    YoctoConfig cfg;
    std::istringstream in(content);
    std::string section;
    std::string line;
    int lineno = 0;

    while (std::getline(in, line)) {
        ++lineno;
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            continue;
        }
        auto eq = line.find('=');
        if (eq == std::string::npos) {
            error = "ligne " + std::to_string(lineno) + " : '=' manquant";
            return cfg;
        }
        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));

        if (section == "general") {
            if (key == "name") cfg.name = val;
            else if (key == "workdir") cfg.workdir = val;
            else if (key == "poky_url") cfg.poky_url = val;
            else if (key == "poky_branch") cfg.poky_branch = val;
            else if (key == "yocto_tag") cfg.yocto_tag = val;
            else if (key == "machine") cfg.machine = val;
            else if (key == "threads") cfg.threads = std::atoi(val.c_str());
        } else if (section == "build") {
            if (key == "build_dir") cfg.build_dir = val;
            else if (key == "dl_dir") cfg.dl_dir = val;
            else if (key == "sstate_dir") cfg.sstate_dir = val;
            else if (key == "image_recipe") cfg.image_recipe = val;
        } else if (section == "options") {
            if (key == "install_packages") cfg.install_packages = parse_bool(val);
            else if (key == "configure_locale") cfg.configure_locale = parse_bool(val);
            else if (key == "fix_apparmor") cfg.fix_apparmor = parse_bool(val);
            else if (key == "configure_git") cfg.configure_git = parse_bool(val);
            else if (key == "first_build") cfg.first_build = parse_bool(val);
        } else if (section == "git") {
            if (key == "user_name") cfg.git_user_name = val;
            else if (key == "user_email") cfg.git_user_email = val;
        }
    }
    cfg.apply_defaults();
    return cfg;
}

void YoctoConfig::apply_defaults() {
    if (name.empty()) name = "default";
    if (workdir.empty()) workdir = home_dir() + "/yocto-lab";
    if (machine.empty()) machine = "qemux86-64";
    if (threads <= 0) {
        unsigned hc = std::thread::hardware_concurrency();
        threads = hc > 0 ? static_cast<int>(hc) : 4;
    }
    if (build_dir.empty()) build_dir = workdir + "/builds/build-" + machine;
    if (dl_dir.empty()) dl_dir = workdir + "/cache/downloads";
    if (sstate_dir.empty()) sstate_dir = workdir + "/cache/sstate-cache";
    if (poky_branch.empty()) poky_branch = "scarthgap";
    if (image_recipe.empty()) image_recipe = "core-image-minimal";
}

// --- Chemins dérivés ---------------------------------------------------

std::filesystem::path YoctoConfig::poky_dir() const {
    return std::filesystem::path(workdir) / "layers" / "poky";
}
std::filesystem::path YoctoConfig::layers_dir() const {
    return std::filesystem::path(workdir) / "layers";
}
std::filesystem::path YoctoConfig::builds_dir() const {
    return std::filesystem::path(workdir) / "builds";
}
std::filesystem::path YoctoConfig::downloads_dir() const {
    return std::filesystem::path(dl_dir);
}
std::filesystem::path YoctoConfig::sstate_cache_dir() const {
    return std::filesystem::path(sstate_dir);
}
std::filesystem::path YoctoConfig::env_script_path() const {
    return std::filesystem::path(workdir) / "env.sh";
}
std::filesystem::path YoctoConfig::build_dir_path() const {
    return std::filesystem::path(build_dir);
}
std::filesystem::path YoctoConfig::local_conf_path() const {
    return build_dir_path() / "conf" / "local.conf";
}

} // namespace yocto