/* @file vcs/Backup.cpp */

#include "vcs/Backup.hpp"

#include "core/Process.hpp"
#include "core/Term.hpp"
#include "core/Util.hpp"

#include <cstdlib>
#include <ctime>
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

std::string capture_one(const fs::path& root, const std::vector<std::string>& args) {
    auto lines = capture_lines(root, args);
    return lines.empty() ? std::string() : util::trim(lines.front());
}

std::string capture_full(const fs::path& root, const std::vector<std::string>& args) {
    std::vector<std::string> cmd = {"git", "-C", root.string()};
    cmd.insert(cmd.end(), args.begin(), args.end());
    auto r = proc::capture(cmd);
    return r.exit_code == 0 ? r.output : std::string();
}

} // namespace

bool working_tree_dirty(const fs::path& root) {
    return !capture_lines(root, {"status", "--porcelain"}).empty();
}

// GCC 15 émet un faux positif -Wfree-nonheap-object sur le retour d'OpResult
// (chaîne SSO) inliné dans backup_capture ; le code est valide (vérifié sous
// valgrind). On le désactive ponctuellement.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfree-nonheap-object"
#endif
OpResult backup_capture(const fs::path& root, bool include_ignored) {
    std::string branch = capture_one(root, {"rev-parse", "--abbrev-ref", "HEAD"});
    if (branch.empty() || branch == "HEAD") branch = "(détaché)";
    std::string commit = capture_one(root, {"rev-parse", "--short", "HEAD"});
    std::string msg = "cpp-maker backup: " + branch + " @ " + commit;

    std::vector<std::string> args = {"stash", "push"};
    if (include_ignored) args.emplace_back("--all");
    else args.emplace_back("--include-untracked");
    args.emplace_back("-m");
    args.emplace_back(msg);

    auto r = run(root, args);
    if (!r.ok) return r;

    // Référence privée pointant vers le commit de stash (protection GC, C.5).
    std::string hash = capture_one(root, {"rev-parse", "refs/stash"});
    if (hash.empty()) return OpResult{false, "impossible de lire refs/stash"};
    std::string id = std::to_string(std::time(nullptr)) + "-" + hash.substr(0, 7);
    return run(root, {"update-ref", "refs/cpp-maker/backups/" + id, hash});
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

std::vector<BackupEntry> backup_list(const fs::path& root) {
    std::vector<BackupEntry> r;
    // %(refname:lstrip=3) : « refs/cpp-maker/backups/<id> » → « <id> ».
    for (auto& line : capture_lines(root, {"for-each-ref", "refs/cpp-maker/backups",
                                           "--format=%(refname:lstrip=3)%00%(subject)%00%(committerdate:short)",
                                           "--sort=-committerdate"})) {
        auto f = util::split(line, '\0', true);
        if (f.size() < 1 || f[0].empty()) continue;
        BackupEntry e;
        e.id = f[0];
        e.ref = "refs/cpp-maker/backups/" + f[0];
        // f[1] = « On <branche>: <message> ».
        if (f.size() > 1) {
            e.message = f[1];
            if (f[1].starts_with("On ")) {
                auto colon = f[1].find(':');
                if (colon != std::string::npos) {
                    e.branch = f[1].substr(3, colon - 3);
                    e.message = util::trim(f[1].substr(colon + 1));
                }
            }
        }
        if (f.size() > 2) e.date = f[2];
        r.push_back(std::move(e));
    }
    return r;
}

std::string backup_preview(const fs::path& root, const std::string& ref) {
    return capture_full(root, {"stash", "show", "--stat", "-p", ref});
}

OpResult backup_restore(const fs::path& root, const std::string& ref) {
    return run(root, {"stash", "apply", "--index", ref});
}

OpResult backup_drop(const fs::path& root, const std::string& ref) {
    // Supprime la référence privée ; le stash correspondant, s'il existe encore,
    // reste (il sera nettoyé par l'utilisateur ou le prochain gc).
    auto del = run(root, {"update-ref", "-d", ref});
    return del;
}

} // namespace vcs
