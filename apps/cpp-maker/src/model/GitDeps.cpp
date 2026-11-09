/* @file model/GitDeps.cpp */

#include "model/GitDeps.hpp"

#include "core/Process.hpp"
#include "core/Term.hpp"
#include "core/Util.hpp"
#include "io/toml.hpp"

#include <cstdlib>

namespace gitdeps {

fs::path cache_root() {
    if (const char* e = std::getenv("CPP_MAKER_CACHE"); e && *e) return fs::path(e) / "git";
    if (const char* e = std::getenv("XDG_CACHE_HOME"); e && *e) return fs::path(e) / "cpp-maker" / "git";
    if (const char* e = std::getenv("HOME"); e && *e) return fs::path(e) / ".cache" / "cpp-maker" / "git";
    return fs::temp_directory_path() / "cpp-maker-git";
}

namespace {
std::string url_key(const std::string& url) {
    util::Hasher h;
    h.feed(url);
    return h.hex();
}
} // namespace

Resolved resolve(const std::string& url, const std::string& version, const std::string& locked) {
    Resolved r;
    if (!util::have("git")) {
        r.error = "git introuvable dans le PATH";
        return r;
    }
    fs::path repo = cache_root() / (url_key(url) + ".git");
    util::FileLock lock;
    lock.acquire(repo.string() + ".lock");

    std::error_code ec;
    if (!fs::exists(repo / "HEAD", ec)) {
        fs::create_directories(repo.parent_path(), ec);
        term::info("git clone {} …", url);
        auto c = proc::capture({"git", "clone", "--mirror", "--quiet", url, repo.string()});
        if (c.exit_code != 0) {
            fs::remove_all(repo, ec);
            r.error = "git clone a échoué : " + util::trim(c.output);
            return r;
        }
    }
    auto rev = [&](const std::string& ref) -> std::string {
        auto v = proc::capture({"git", "-C", repo.string(), "rev-parse", "--verify", "-q", ref + "^{commit}"});
        return v.exit_code == 0 ? util::trim(v.output) : std::string{};
    };
    std::string commit = locked.empty() ? "" : rev(locked);
    if (commit.empty()) commit = rev(version);
    if (commit.empty()) {
        auto f = proc::capture({"git", "-C", repo.string(), "remote", "update", "--prune"});
        if (f.exit_code != 0) {
            r.error = "git fetch a échoué : " + util::trim(f.output);
            return r;
        }
        if (!locked.empty()) commit = rev(locked);
        if (commit.empty()) commit = rev(version);
    }
    if (commit.empty()) {
        r.error = std::format("révision '{}' introuvable dans {}", version, url);
        return r;
    }
    fs::path co = cache_root() / url_key(url) / commit.substr(0, 16);
    if (!fs::exists(co / ".cpp-maker-checkout", ec)) {
        fs::remove_all(co, ec);
        fs::create_directories(co.parent_path(), ec);
        (void)proc::capture({"git", "-C", repo.string(), "worktree", "prune"});
        auto w = proc::capture({"git", "-C", repo.string(), "worktree", "add", "--detach", "--force",
                                co.string(), commit});
        if (w.exit_code != 0) {
            r.error = "git worktree add a échoué : " + util::trim(w.output);
            return r;
        }
        util::write_file(co / ".cpp-maker-checkout", commit + "\n");
    }
    r.ok = true;
    r.commit = commit;
    r.path = co;
    return r;
}

std::map<std::string, std::string> read_lock(const fs::path& file) {
    std::map<std::string, std::string> r;
    std::error_code ec;
    if (!fs::exists(file, ec)) return r;
    auto root = toml::parse_file(file);
    if (auto* git = root.find("git"); git && git->is_table()) {
        for (size_t i = 0; i < git->size(); ++i) {
            auto& v = git->value_at(i);
            if (auto* c = v.find("commit"); c && c->is_string()) r[git->keys()[i]] = c->as_string();
        }
    }
    return r;
}

void write_lock(const fs::path& file, const std::map<std::string, std::string>& lock) {
    if (lock.empty()) return;
    std::string s = "# Généré par cpp-maker : versions Git verrouillées.\n"
                    "# Ne pas éditer ; « cpp-maker deps update » rafraîchit ce fichier.\n";
    for (auto& [url, commit] : lock)
        s += std::format("\n[git.{}]\ncommit = {}\n", toml::quote(url), toml::quote(commit));
    util::write_file_if_changed(file, s);
}

} // namespace gitdeps
