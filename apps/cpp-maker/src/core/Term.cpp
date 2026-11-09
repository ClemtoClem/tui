/* @file core/Term.cpp */

#include "core/Term.hpp"

#include <cstdlib>
#include <unistd.h>

namespace term {

namespace {
int& color_state() {
    // -1 = pas encore déterminé
    static int s = -1;
    return s;
}
} // namespace

bool color_enabled() {
    int& s = color_state();
    if (s < 0) {
        const char* force = std::getenv("CLICOLOR_FORCE");
        if (force && *force && std::string_view(force) != "0") s = 1;
        else if (std::getenv("NO_COLOR")) s = 0;
        else s = ::isatty(STDOUT_FILENO) ? 1 : 0;
    }
    return s == 1;
}

void set_color(bool on) { color_state() = on ? 1 : 0; }

std::string strip_ansi(std::string_view s) {
    std::string r;
    r.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\033' && i + 1 < s.size() && s[i + 1] == '[') {
            size_t j = i + 2;
            while (j < s.size() && !(s[j] >= '@' && s[j] <= '~')) ++j;
            i = j;
        } else {
            r += s[i];
        }
    }
    return r;
}

std::string paint(std::string_view code, std::string_view text) {
    if (!color_enabled()) return std::string(text);
    return std::format("\033[{}m{}\033[0m", code, text);
}

void write_out(std::string_view s) {
    if (color_enabled()) std::cout << s;
    else std::cout << strip_ansi(s);
}

void write_err(std::string_view s) {
    std::cout.flush();
    if (color_enabled()) std::cerr << s;
    else std::cerr << strip_ansi(s);
}

bool confirm(const std::string& question, bool assume_yes) {
    if (assume_yes) return true;
    write_out(question + " [o/N] ");
    std::cout.flush();
    std::string line;
    if (!std::getline(std::cin, line)) return false;
    return !line.empty() && (line[0] == 'o' || line[0] == 'O' || line[0] == 'y' || line[0] == 'Y');
}

} // namespace term
