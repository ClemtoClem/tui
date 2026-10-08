// util/BitBake.cpp
#include "BitBake.hpp"
#include <array>
#include <cstdio>
#include <filesystem>
#include <sys/wait.h>

namespace yocto {

BitBake::BitBake(std::filesystem::path build_dir) : build_dir_(std::move(build_dir)) {}

std::string BitBake::quote(const std::string& s) const {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}

std::string BitBake::wrap_env(const std::string& inner) const {
    return "cd " + quote(build_dir_.string()) + " && "
           "source ../poky/oe-init-build-env . >/dev/null 2>&1 && "
           + inner;
}

std::string BitBake::build_command(const std::vector<std::string>& args) const {
    std::string inner = "bitbake";
    for (const auto& a : args) inner += " " + quote(a);
    return wrap_env(inner);
}

BitBakeResult BitBake::run_shell(const std::string& command) {
    BitBakeResult result;
    result.command = wrap_env(command);
    FILE* pipe = popen(result.command.c_str(), "r");
    if (!pipe) return result;
    std::array<char, 4096> buf{};
    size_t n;
    while ((n = fread(buf.data(), 1, buf.size(), pipe)) > 0) {
        result.output.append(buf.data(), n);
    }
    int status = pclose(pipe);
    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return result;
}

BitBakeResult BitBake::run(const std::vector<std::string>& args) {
    BitBakeResult result;
    result.command = build_command(args);

    FILE* pipe = popen(result.command.c_str(), "r");
    if (!pipe) return result;

    std::array<char, 4096> buf{};
    size_t n;
    while ((n = fread(buf.data(), 1, buf.size(), pipe)) > 0) {
        result.output.append(buf.data(), n);
    }
    int status = pclose(pipe);
    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return result;
}

BitBakeResult BitBake::run_streaming(const std::vector<std::string>& args,
                                      std::function<void(const std::string&)> on_line) {
    BitBakeResult result;
    result.command = build_command(args);

    FILE* pipe = popen(result.command.c_str(), "r");
    if (!pipe) return result;

    std::string current_line;
    std::array<char, 4096> buf{};
    size_t n;
    while ((n = fread(buf.data(), 1, buf.size(), pipe)) > 0) {
        for (size_t i = 0; i < n; ++i) {
            char c = buf[i];
            if (c == '\n') {
                result.output += current_line + "\n";
                if (on_line) on_line(current_line);
                current_line.clear();
            } else {
                current_line += c;
            }
        }
    }
    if (!current_line.empty()) {
        result.output += current_line;
        if (on_line) on_line(current_line);
    }

    int status = pclose(pipe);
    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return result;
}

BitBakeResult BitBake::build_image(const std::string& image) {
    return run_streaming({image}, nullptr);
}

BitBakeResult BitBake::build_recipe(const std::string& recipe) {
    return run_streaming({recipe}, nullptr);
}

BitBakeResult BitBake::menuconfig(const std::string& recipe) {
    return run({"-c", "menuconfig", recipe});
}

BitBakeResult BitBake::list_layers() {
    return run({"-e", "BBLAYERS"});
}

BitBakeResult BitBake::show_recipe_info(const std::string& recipe) {
    return run({"-e", recipe});
}

BitBakeResult BitBake::devtool_modify(const std::string& recipe) {
    return run({"-c", "devtool modify", recipe});
}

} // namespace yocto