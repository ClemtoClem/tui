/* @file vcs/Diff.cpp */

#include "vcs/Diff.hpp"

#include "core/Process.hpp"
#include "core/Util.hpp"

#include <cstdlib>
#include <ctime>
#include <vector>

namespace vcs {

namespace {

std::string capture(const fs::path& root, const std::vector<std::string>& args) {
    std::vector<std::string> cmd = {"git", "-C", root.string()};
    cmd.insert(cmd.end(), args.begin(), args.end());
    auto r = proc::capture(cmd);
    return r.exit_code == 0 ? r.output : std::string();
}

OpResult run(const fs::path& root, const std::vector<std::string>& args) {
    std::vector<std::string> cmd = {"git", "-C", root.string()};
    cmd.insert(cmd.end(), args.begin(), args.end());
    auto r = proc::capture(cmd);
    if (r.exit_code == 0) return {};
    OpResult res;
    res.ok = false;
    res.error = util::trim(r.output);
    if (res.error.empty()) res.error = "échec (code " + std::to_string(r.exit_code) + ")";
    return res;
}

} // namespace

std::string diff(const fs::path& root, const std::string& from, const std::string& to) {
    std::vector<std::string> args = {"diff", "--stat", "--patch"};
    if (!from.empty() || !to.empty()) {
        if (!from.empty()) args.push_back(from);
        if (!to.empty()) args.push_back(to);
    }
    return capture(root, args);
}

std::string diff_working(const fs::path& root, bool staged) {
    std::vector<std::string> args = {"diff", "--stat", "--patch"};
    if (staged) args.push_back("--cached");
    return capture(root, args);
}

std::string show_commit(const fs::path& root, const std::string& commit) {
    return capture(root, {"show", "--stat", "--patch", commit});
}

std::string file_diff(const fs::path& root, const std::string& file, bool staged) {
    std::vector<std::string> args = {"diff", "--patch"};
    if (staged) args.push_back("--cached");
    args.push_back("--");
    args.push_back(file);
    return capture(root, args);
}

FilePatch diff_hunks(const fs::path& root, const std::string& file, bool staged) {
    FilePatch fp;
    auto lines = util::split(file_diff(root, file, staged), '\n', true);
    // En-tête de fichier : tout ce qui précède le premier « @@ ».
    size_t i = 0;
    while (i < lines.size() && !lines[i].starts_with("@@")) {
        fp.header += lines[i] + "\n";
        ++i;
    }
    Hunk cur;
    while (i < lines.size()) {
        if (lines[i].starts_with("@@")) {
            if (!cur.header.empty()) { fp.hunks.push_back(std::move(cur)); cur = Hunk{}; }
            cur.header = lines[i];
        } else if (lines[i].starts_with("diff --git")) {
            break; // fichier suivant (ne devrait pas arriver pour un seul fichier)
        } else {
            cur.body += lines[i] + "\n";
        }
        ++i;
    }
    if (!cur.header.empty()) fp.hunks.push_back(std::move(cur));
    return fp;
}

OpResult apply_hunks(const fs::path& root, const FilePatch& patch,
                     const std::vector<size_t>& selected, bool reverse) {
    std::string p = patch.header;
    for (size_t idx : selected)
        if (idx < patch.hunks.size()) p += patch.hunks[idx].header + "\n" + patch.hunks[idx].body;
    if (p == patch.header) return OpResult{false, "aucun hunk sélectionné"};

    fs::path tmp = fs::temp_directory_path() / ("cpp-maker-patch-" + std::to_string(std::time(nullptr)) + ".patch");
    util::write_file(tmp, p);
    std::vector<std::string> args = {"apply", "--cached"};
    if (reverse) args.push_back("-R");
    args.push_back(tmp.string());
    auto r = run(root, args);
    std::error_code ec;
    fs::remove(tmp, ec);
    return r;
}

} // namespace vcs

