/* @file vcs/History.cpp */

#include "vcs/History.hpp"

#include "core/Process.hpp"
#include "core/Util.hpp"

#include <cstdlib>
#include <map>

namespace vcs {

namespace {

/// Découpe la décoration %D (« HEAD -> main, origin/main, tag: v1.0 ») et
/// remplit branches/tags/head de `c`.
void parse_decorations(const std::string& deco, CommitRef& c) {
    if (deco.empty()) return;
    for (auto& item : util::split(deco, ',', false)) {
        std::string s = util::trim(item);
        if (s.empty()) continue;
        if (s.starts_with("HEAD -> ")) {
            c.head = true;
            c.head_ref = s.substr(8);
        } else if (s == "HEAD") {
            c.head = true; // HEAD détaché
        } else if (s.starts_with("tag: ")) {
            c.tags.push_back(s.substr(5));
        } else {
            c.branches.push_back(s);
        }
    }
}

void parse_decorations(const std::string& deco, CommitDetails& c) {
    if (deco.empty()) return;
    for (auto& item : util::split(deco, ',', false)) {
        std::string s = util::trim(item);
        if (s.empty()) continue;
        if (s.starts_with("HEAD -> ")) {
            c.head = true;
        } else if (s == "HEAD") {
            c.head = true;
        } else if (s.starts_with("tag: ")) {
            c.tags.push_back(s.substr(5));
        } else {
            c.branches.push_back(s);
        }
    }
}

std::string capture_one(const fs::path& root, const std::vector<std::string>& args) {
    std::vector<std::string> cmd = {"git", "-C", root.string()};
    cmd.insert(cmd.end(), args.begin(), args.end());
    auto r = proc::capture(cmd);
    return r.exit_code == 0 ? r.output : std::string();
}

std::vector<std::string> capture_lines(const fs::path& root, const std::vector<std::string>& args) {
    std::vector<std::string> cmd = {"git", "-C", root.string()};
    cmd.insert(cmd.end(), args.begin(), args.end());
    auto r = proc::capture(cmd);
    if (r.exit_code != 0) return {};
    return util::split(r.output, '\n', false);
}

} // namespace

History history(const fs::path& root, int max_count) {
    History h;
    if (max_count <= 0) max_count = 1000;

    // Une seule invocation : hachage, parents, auteur, date, sujet, décoration.
    auto out = proc::capture({"git", "-C", root.string(), "log", "--all", "--topo-order",
                              "--date=short",
                              "--pretty=format:%H%x01%h%x01%P%x01%an%x01%ad%x01%s%x01%D",
                              "-n", std::to_string(max_count)});
    if (out.exit_code != 0) {
        h.error = util::trim(out.output);
        return h;
    }

    std::vector<CommitRef> commits;
    for (auto& line : util::split(out.output, '\n', false)) {
        // keep_empty=true : le champ %D (décorations) peut être vide en fin de
        // ligne, il doit rester présent pour que l'indexation soit stable.
        auto fields = util::split(line, '\x01', true);
        if (fields.size() < 7) continue; // ligne malformée : ignorée
        CommitRef c;
        c.hash = fields[0];
        c.short_hash = fields[1];
        c.parents = util::split_ws(fields[2]);
        c.author = fields[3];
        c.date = fields[4];
        c.subject = fields[5];
        parse_decorations(fields[6], c);
        if (c.head) {
            h.head = c.short_hash;
            h.branch = c.head_ref;
        }
        commits.push_back(std::move(c));
    }

    h.commits = std::move(commits);
    return h;
}

CommitDetails commit_details(const fs::path& root, const std::string& commit) {
    CommitDetails d;
    // Métadonnées (une ligne, avec décorations).
    std::string meta = capture_one(root, {"log", "-1", "--decorate=short",
                                          "--format=%H%x01%an%x01%ae%x01%ad%x01%cn%x01%ce%x01%cd%x01%P%x01%D", commit});
    auto f = util::split(meta, '\x01', true);
    if (f.size() >= 9) {
        d.hash = f[0];
        d.author_name = f[1];
        d.author_email = f[2];
        d.author_date = f[3];
        d.committer_name = f[4];
        d.committer_email = f[5];
        d.committer_date = f[6];
        d.parents = util::split_ws(f[7]);
        parse_decorations(f[8], d);
    }
    d.short_hash = d.hash.empty() ? std::string() : d.hash.substr(0, 7);

    // Message complet (titre + corps).
    std::string msg = util::trim(capture_one(root, {"show", "-s", "--format=%B", commit}));
    auto nl = msg.find('\n');
    d.subject = nl == std::string::npos ? msg : util::trim(msg.substr(0, nl));
    d.body = nl == std::string::npos ? std::string() : util::trim(msg.substr(nl + 1));

    // Enfants : carte inverse parents → enfants sur tout l'historique.
    std::map<std::string, std::vector<std::string>> children_of;
    for (auto& line : capture_lines(root, {"log", "--all", "--format=%H%x01%P"})) {
        auto cf = util::split(line, '\x01', true);
        if (cf.size() < 1 || cf[0].empty()) continue;
        for (auto& parent : util::split_ws(cf[1]))
            children_of[parent].push_back(cf[0]);
    }
    d.children = children_of[d.hash];

    return d;
}

} // namespace vcs
