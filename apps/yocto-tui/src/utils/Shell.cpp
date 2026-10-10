/* @file utils/Shell.cpp */
#include "utils/Shell.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <sys/wait.h>

namespace yocto {

std::string shell_quote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}

std::string build_command(const std::vector<std::string>& tokens) {
    std::string out;
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (i > 0) out += ' ';
        out += shell_quote(tokens[i]);
    }
    return out;
}

bool command_exists(const std::string& name) {
    std::string cmd = "command -v " + shell_quote(name) + " >/dev/null 2>&1";
    return std::system(cmd.c_str()) == 0;
}

namespace {

ShellResult run_shell_impl(const std::string& full_cmd,
                           std::function<void(const std::string&)> on_line) {
    ShellResult result;
    result.command = full_cmd;

    FILE* pipe = popen(full_cmd.c_str(), "r");
    if (!pipe) return result;

    std::string current;
    std::array<char, 4096> buf{};
    size_t n = 0;
    while ((n = fread(buf.data(), 1, buf.size(), pipe)) > 0) {
        for (size_t i = 0; i < n; ++i) {
            char c = buf[i];
            if (c == '\n') {
                result.output += current + "\n";
                if (on_line) on_line(current);
                current.clear();
            } else {
                current += c;
            }
        }
    }
    if (!current.empty()) {
        result.output += current;
        if (on_line) on_line(current);
    }
    int status = pclose(pipe);
    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return result;
}

} // namespace

ShellResult run_shell(const std::string& cmd, std::function<void(const std::string&)> on_line) {
    return run_shell_impl(cmd, std::move(on_line));
}

ShellResult run_shell_root(const std::string& cmd, std::function<void(const std::string&)> on_line) {
    return run_shell_impl("sudo -n " + cmd, std::move(on_line));
}

bool sudo_is_ready() {
    return std::system("sudo -n true >/dev/null 2>&1") == 0;
}

std::string sudo_refresh_command() {
    return "sudo -v";
}

} // namespace yocto