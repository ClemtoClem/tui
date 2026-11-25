/* @file vcs/Worktree.cpp */

#include "vcs/Worktree.hpp"

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
    return util::split(r.output, '\n', true);
}

} // namespace

std::vector<WorktreeEntry> worktree_list(const fs::path& root) {
    std::vector<WorktreeEntry> r;
    WorktreeEntry cur;
    bool has = false;
    auto flush = [&] {
        if (has) r.push_back(std::move(cur));
        cur = WorktreeEntry{};
        has = false;
    };
    for (auto& line : capture_lines(root, {"worktree", "list", "--porcelain"})) {
        if (line.empty()) { flush(); continue; }
        if (line.starts_with("worktree ")) {
            flush();
            cur.path = line.substr(9);
            has = true;
        } else if (line.starts_with("HEAD ")) {
            cur.head = line.substr(5);
            if (cur.head.size() > 8) cur.head = cur.head.substr(0, 8);
        } else if (line.starts_with("branch ")) {
            std::string b = line.substr(7);
            const std::string prefix = "refs/heads/";
            if (b.starts_with(prefix)) b = b.substr(prefix.size());
            cur.branch = b;
        } else if (line == "detached") {
            cur.detached = true;
        } else if (line == "bare") {
            cur.bare = true;
        } else if (line.starts_with("prunable")) {
            cur.prunable = true;
        }
    }
    flush();
    return r;
}

OpResult worktree_add(const fs::path& root, const std::string& path,
                      const std::string& branch, const std::string& base) {
    std::vector<std::string> args = {"worktree", "add"};
    if (!branch.empty()) { args.emplace_back("-b"); args.emplace_back(branch); }
    args.emplace_back(path);
    if (!base.empty()) args.emplace_back(base);
    return run(root, args);
}

OpResult worktree_remove(const fs::path& root, const std::string& path) {
    return run(root, {"worktree", "remove", path});
}

OpResult worktree_prune(const fs::path& root) {
    return run(root, {"worktree", "prune"});
}

} // namespace vcs
