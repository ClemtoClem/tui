/* @file vcs/Stash.cpp */

#include "vcs/Stash.hpp"

#include "core/Process.hpp"
#include "core/Term.hpp"
#include "core/Util.hpp"

#include <vector>

namespace vcs {

namespace {

OpResult run(const fs::path& root, const std::vector<std::string>& args) {
    std::vector<std::string> cmd = {"git", "-C", root.string()};
    cmd.insert(cmd.end(), args.begin(), args.end());
    auto r = proc::capture(cmd);
    if (r.exit_code == 0) return {};
    OpResult res;
    res.ok = false;
    res.error = util::trim(term::strip_ansi(r.output));
    if (res.error.empty()) res.error = "échec (code " + std::to_string(r.exit_code) + ")";
    return res;
}

std::vector<std::string> capture_lines(const fs::path& root, const std::vector<std::string>& args) {
    std::vector<std::string> cmd = {"git", "-C", root.string()};
    cmd.insert(cmd.end(), args.begin(), args.end());
    auto r = proc::capture(cmd);
    if (r.exit_code != 0) return {};
    return util::split(r.output, '\n', false);
}

} // namespace

std::vector<StashEntry> stash_list(const fs::path& root) {
    std::vector<StashEntry> r;
    // `git stash list` utilise les formats %xHH (comme git log) ; %gd = « stash@{0} ».
    for (auto& line : capture_lines(root, {"stash", "list", "--format=%gd%x00%h%x00%gs%x00%ci"})) {
        auto f = util::split(line, '\0', true);
        if (f.size() < 1 || f[0].empty()) continue;
        StashEntry e;
        e.ref = f[0];
        if (f.size() > 1) e.hash = f[1];
        if (f.size() > 2) {
            e.message = f[2];
            // « On <branche>: <message> » → ne garder que le message.
            if (f[2].starts_with("On ")) {
                auto colon = f[2].find(':');
                if (colon != std::string::npos) e.message = util::trim(f[2].substr(colon + 1));
            }
        }
        if (f.size() > 3) e.date = f[3];
        r.push_back(std::move(e));
    }
    return r;
}

OpResult stash_push(const fs::path& root, const std::string& message) {
    std::string msg = message.empty() ? "cpp-maker stash" : message;
    return run(root, {"stash", "push", "--include-untracked", "-m", msg});
}

OpResult stash_pop(const fs::path& root, const std::string& ref) {
    return run(root, {"stash", "pop", ref});
}

OpResult stash_apply(const fs::path& root, const std::string& ref) {
    return run(root, {"stash", "apply", "--index", ref});
}

OpResult stash_drop(const fs::path& root, const std::string& ref) {
    return run(root, {"stash", "drop", ref});
}

std::string stash_show(const fs::path& root, const std::string& ref) {
    std::vector<std::string> cmd = {"git", "-C", root.string(), "stash", "show", "--stat", "-p", ref};
    auto r = proc::capture(cmd);
    return r.exit_code == 0 ? r.output : std::string();
}

} // namespace vcs
