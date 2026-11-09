/* @file model/GitDeps.hpp
 *
 * Dépendances Git : miroir « --mirror » mis en cache, puis extraction
 * (worktree détaché) d'un commit précis. Les commits résolus sont
 * verrouillés dans <racine>/cpp-maker.lock (à committer).
 *
 * Cache : $CPP_MAKER_CACHE/git, sinon $XDG_CACHE_HOME/cpp-maker/git,
 * sinon ~/.cache/cpp-maker/git.
 */
#pragma once

#include <filesystem>
#include <map>
#include <string>

namespace gitdeps {

std::filesystem::path cache_root();

struct Resolved {
    bool ok = false;
    std::string commit;
    std::filesystem::path path;
    std::string error;
};

/// Résout `version` (tag, branche, commit) ; `locked` prime s'il existe.
Resolved resolve(const std::string& url, const std::string& version, const std::string& locked = {});

std::map<std::string, std::string> read_lock(const std::filesystem::path& file);
void write_lock(const std::filesystem::path& file, const std::map<std::string, std::string>& lock);

} // namespace gitdeps
