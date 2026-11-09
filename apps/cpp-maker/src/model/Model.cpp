/* @file model/Model.cpp */

#include "model/Model.hpp"

#include <algorithm>

namespace model {

std::vector<ModeConfig> builtin_modes() {
    auto M = [](std::string name, std::vector<std::string> flags, std::vector<std::string> defines,
                std::string sanitizer = {}, std::string runner = {}, std::vector<std::string> rargs = {}) {
        ModeConfig m;
        m.name = std::move(name);
        m.flags = std::move(flags);
        m.defines = std::move(defines);
        m.sanitizer = std::move(sanitizer);
        m.runner = std::move(runner);
        m.runner_args = std::move(rargs);
        m.builtin = true;
        normalize_mode(m);
        return m;
    };
    return {
        M("debug", {"-O0", "-g3"}, {"DEBUG"}),
        M("release", {"-O2"}, {"NDEBUG"}),
        M("relwithdebinfo", {"-O2", "-g"}, {"NDEBUG"}),
        M("minsize", {"-Os"}, {"NDEBUG"}),
        M("asan", {"-O1", "-g3"}, {"DEBUG"}, "address"),
        M("tsan", {"-O1", "-g3"}, {"DEBUG"}, "thread"),
        M("ubsan", {"-O1", "-g3"}, {"DEBUG"}, "undefined"),
        M("msan", {"-O1", "-g3", "-fsanitize-memory-track-origins=2"}, {"DEBUG"}, "memory"),
        M("lsan", {"-O1", "-g3"}, {"DEBUG"}, "leak"),
        M("valgrind", {"-O0", "-g3"}, {"DEBUG"}, "", "valgrind",
          {"--leak-check=full", "--show-leak-kinds=definite,indirect", "--track-origins=yes", "--error-exitcode=1"}),
    };
}

const std::vector<std::string>& cxx_standards() {
    static const std::vector<std::string> s = {"c++11", "c++14", "c++17", "c++20", "c++23", "c++26",
                                               "gnu++11", "gnu++14", "gnu++17", "gnu++20", "gnu++23", "gnu++26",
                                               "c++98", "c++03", "gnu++98", "gnu++03"};
    return s;
}

const std::vector<std::string>& c_standards() {
    static const std::vector<std::string> s = {"c89", "c90", "c99", "c11", "c17", "c18", "c23",
                                               "gnu89", "gnu90", "gnu99", "gnu11", "gnu17", "gnu18", "gnu23"};
    return s;
}

void normalize_mode(ModeConfig& m) {
    std::vector<std::string> flags;
    for (auto& f : m.flags) {
        if (f.starts_with("-D") && f.size() > 2) {
            if (std::find(m.defines.begin(), m.defines.end(), f.substr(2)) == m.defines.end())
                m.defines.push_back(f.substr(2));
        } else {
            flags.push_back(f);
        }
    }
    m.flags = std::move(flags);
    if (!m.sanitizer.empty()) {
        auto has = [](const std::vector<std::string>& v) {
            return std::any_of(v.begin(), v.end(), [](auto& f) { return f.starts_with("-fsanitize="); });
        };
        if (!has(m.flags)) {
            m.flags.push_back("-fsanitize=" + m.sanitizer);
            if (std::find(m.flags.begin(), m.flags.end(), "-fno-omit-frame-pointer") == m.flags.end())
                m.flags.push_back("-fno-omit-frame-pointer");
        }
        if (!has(m.link_flags)) m.link_flags.push_back("-fsanitize=" + m.sanitizer);
    }
}

} // namespace model
