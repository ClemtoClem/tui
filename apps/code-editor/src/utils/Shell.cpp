#include "utils/Shell.hpp"

#include <array>
#include <cstdio>
#include <sys/wait.h>

namespace ide {

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

std::vector<std::string> split_lines(const std::string& s) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= s.size()) {
        size_t nl = s.find('\n', start);
        if (nl == std::string::npos) {
            if (start < s.size()) lines.push_back(s.substr(start));
            break;
        }
        lines.push_back(s.substr(start, nl - start));
        start = nl + 1;
    }
    return lines;
}

ProcessResult run_command(const std::string& command, const std::string& working_dir) {
    std::string full = "cd " + shell_quote(working_dir) + " && (" + command + ") 2>&1";
    ProcessResult result;

    FILE* pipe = popen(full.c_str(), "r");
    if (!pipe) return result;

    std::array<char, 4096> buf{};
    size_t n = 0;
    while ((n = fread(buf.data(), 1, buf.size(), pipe)) > 0) {
        result.output.append(buf.data(), n);
    }
    int status = pclose(pipe);
    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return result;
}

} // namespace ide
