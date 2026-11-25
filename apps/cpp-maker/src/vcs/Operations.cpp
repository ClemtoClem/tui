/* @file vcs/Operations.cpp */

#include "vcs/Operations.hpp"

#include "core/Process.hpp"
#include "core/Term.hpp"
#include "core/Util.hpp"

#include <ctime>
#include <set>

namespace vcs {

namespace {

/// Exécute `git -C <root> <args…>` et capture stdout+stderr. Succès = ok ; en
/// cas d'échec, `error` contient la sortie (nettoyée des séquences ANSI).
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

/// Sortie d'une commande git réussie (stdout), découpée en lignes non vides.
std::vector<std::string> capture_lines(const fs::path& root, const std::vector<std::string>& args) {
    std::vector<std::string> cmd = {"git", "-C", root.string()};
    cmd.insert(cmd.end(), args.begin(), args.end());
    auto r = proc::capture(cmd);
    if (r.exit_code != 0) return {};
    return util::split(r.output, '\n', false);
}

/// Sortie d'une commande git réussie (stdout), première ligne, sans les blancs.
std::string capture_one(const fs::path& root, const std::vector<std::string>& args) {
    auto lines = capture_lines(root, args);
    return lines.empty() ? std::string() : util::trim(lines.front());
}

} // namespace

OpResult checkout(const fs::path& root, const std::string& ref) {
    return run(root, {"checkout", ref});
}

OpResult cherry_pick(const fs::path& root, const std::string& commit) {
    return run(root, {"cherry-pick", commit});
}

OpResult revert(const fs::path& root, const std::string& commit) {
    return run(root, {"revert", "--no-edit", commit});
}

OpResult reset(const fs::path& root, const std::string& commit, bool hard) {
    std::vector<std::string> args = {"reset"};
    if (hard) args.push_back("--hard");
    args.push_back(commit);
    return run(root, args);
}

OpResult amend_message(const fs::path& root, const std::string& message) {
    return run(root, {"commit", "--amend", "-m", message});
}

OpResult create_branch(const fs::path& root, const std::string& name, const std::string& start, bool checkout_new) {
    std::vector<std::string> args = {checkout_new ? "checkout" : "branch", checkout_new ? "-b" : name};
    if (checkout_new) args.push_back(name);
    if (!start.empty()) args.push_back(start);
    return run(root, args);
}

OpResult delete_branch(const fs::path& root, const std::string& name, bool force) {
    std::vector<std::string> args = {"branch"};
    if (force) args.push_back("-D");
    else args.push_back("-d");
    args.push_back(name);
    return run(root, args);
}

OpResult rename_branch(const fs::path& root, const std::string& old_name, const std::string& new_name) {
    return run(root, {"branch", "-m", old_name, new_name});
}

OpResult merge(const fs::path& root, const std::string& branch) {
    return run(root, {"merge", branch});
}

OpResult rebase(const fs::path& root, const std::string& branch) {
    return run(root, {"rebase", branch});
}

std::vector<std::string> local_branches(const fs::path& root) {
    std::vector<std::string> r;
    for (auto& line : capture_lines(root, {"branch", "--format=%(refname:short)"})) {
        if (!line.empty()) r.push_back(line);
    }
    return r;
}

OpResult create_tag(const fs::path& root, const std::string& name, const std::string& message,
                    const std::string& commit, bool annotated) {
    std::vector<std::string> args = {"tag"};
    if (annotated) {
        args.push_back("-a");
        args.push_back(name);
        args.push_back("-m");
        args.push_back(message);
    } else {
        args.push_back(name);
    }
    if (!commit.empty()) args.push_back(commit);
    return run(root, args);
}

OpResult delete_tag(const fs::path& root, const std::string& name) {
    return run(root, {"tag", "-d", name});
}

OpResult rename_tag(const fs::path& root, const std::string& old_name, const std::string& new_name) {
    // Un tag annoté doit être recréé (git tag -a -m) pour conserver son message.
    std::vector<std::string> cmd = {"git", "-C", root.string(), "for-each-ref", "refs/tags/" + old_name,
                                    "--format=%(objecttype)%00%(contents)"};
    auto r = proc::capture(cmd);
    bool annotated = r.exit_code == 0 && r.output.starts_with("tag");
    std::string msg;
    if (r.exit_code == 0) {
        auto nul = r.output.find('\0');
        if (nul != std::string::npos) msg = util::trim(r.output.substr(nul + 1));
    }
    auto del = run(root, {"tag", "-d", old_name});
    if (!del.ok) return del;
    if (annotated && !msg.empty()) return run(root, {"tag", "-a", new_name, "-m", msg});
    return run(root, {"tag", new_name});
}

std::vector<std::string> tags(const fs::path& root) {
    return capture_lines(root, {"tag", "--list"});
}

std::string upstream(const fs::path& root) {
    return capture_one(root, {"rev-parse", "--abbrev-ref", "@{upstream}"});
}

std::vector<std::string> remotes(const fs::path& root) {
    return capture_lines(root, {"remote"});
}

std::vector<std::string> remote_branches(const fs::path& root) {
    // Filtre les refs symboliques (origin/HEAD) : seules les branches réelles
    // (symref vide) sont listées.
    std::vector<std::string> r;
    for (auto& line : capture_lines(root, {"for-each-ref", "refs/remotes", "--format=%(refname:short)%00%(symref)"})) {
        auto f = util::split(line, '\0', true);
        if (f.size() >= 2 && f[1].empty() && !f[0].empty()) r.push_back(f[0]);
    }
    return r;
}

OpResult push(const fs::path& root, const std::string& ref) {
    auto rems = remotes(root);
    std::string remote = rems.empty() ? "origin" : rems.front();
    return run(root, {"push", remote, ref});
}

OpResult push_delete_branch(const fs::path& root, const std::string& branch) {
    auto rems = remotes(root);
    std::string remote = rems.empty() ? "origin" : rems.front();
    return run(root, {"push", remote, "--delete", branch});
}

OpResult push_delete_tag(const fs::path& root, const std::string& tag) {
    auto rems = remotes(root);
    std::string remote = rems.empty() ? "origin" : rems.front();
    return run(root, {"push", remote, "--delete", tag});
}

bool branch_exists(const fs::path& root, const std::string& name) {
    return !capture_one(root, {"rev-parse", "--verify", "--quiet", "refs/heads/" + name}).empty() ||
           !capture_one(root, {"rev-parse", "--verify", "--quiet", "refs/remotes/" + name}).empty();
}

bool tag_exists(const fs::path& root, const std::string& name) {
    return !capture_one(root, {"rev-parse", "--verify", "--quiet", "refs/tags/" + name}).empty();
}

std::string remote_url(const fs::path& root, const std::string& name) {
    return capture_one(root, {"remote", "get-url", name});
}

OpResult add_remote(const fs::path& root, const std::string& name, const std::string& url) {
    return run(root, {"remote", "add", name, url});
}

OpResult remove_remote(const fs::path& root, const std::string& name) {
    return run(root, {"remote", "remove", name});
}

OpResult rename_remote(const fs::path& root, const std::string& old_name, const std::string& new_name) {
    return run(root, {"remote", "rename", old_name, new_name});
}

OpResult set_remote_url(const fs::path& root, const std::string& name, const std::string& url) {
    return run(root, {"remote", "set-url", name, url});
}

OpResult fetch(const fs::path& root) {
    auto rems = remotes(root);
    std::string remote = rems.empty() ? "origin" : rems.front();
    return run(root, {"fetch", remote});
}

OpResult prune(const fs::path& root) {
    auto rems = remotes(root);
    std::string remote = rems.empty() ? "origin" : rems.front();
    return run(root, {"fetch", remote, "--prune"});
}

OpResult pull(const fs::path& root) {
    return run(root, {"pull"});
}

OpResult set_upstream(const fs::path& root, const std::string& branch, const std::string& upstream) {
    return run(root, {"branch", "--set-upstream-to=" + upstream, branch});
}

std::vector<std::string> merged_branches(const fs::path& root) {
    // `git branch --merged` (sans --format) : chaque ligne est « * branche »
    // (courante) ou «   branche ».
    std::vector<std::string> r;
    for (auto& line : capture_lines(root, {"branch", "--merged"})) {
        std::string b = util::trim(line);
        if (b.empty()) continue;
        if (b[0] == '*') b = util::trim(b.substr(1));
        if (!b.empty()) r.push_back(b);
    }
    return r;
}

bool branch_has_upstream(const fs::path& root, const std::string& branch) {
    return !capture_one(root, {"rev-parse", "--abbrev-ref", branch + "@{upstream}"}).empty();
}

std::vector<std::string> conflicted_files(const fs::path& root) {
    return capture_lines(root, {"diff", "--name-only", "--diff-filter=U"});
}

std::string operation_in_progress(const fs::path& root) {
    if (!capture_one(root, {"rev-parse", "-q", "--verify", "MERGE_HEAD"}).empty()) return "merge";
    if (!capture_one(root, {"rev-parse", "-q", "--verify", "CHERRY_PICK_HEAD"}).empty()) return "cherry-pick";
    if (!capture_one(root, {"rev-parse", "-q", "--verify", "REVERT_HEAD"}).empty()) return "revert";
    std::string gitdir = capture_one(root, {"rev-parse", "--git-dir"});
    if (!gitdir.empty()) {
        fs::path gd = fs::absolute(gitdir);
        if (fs::exists(gd / "rebase-merge") || fs::exists(gd / "rebase-apply")) return "rebase";
    }
    return "";
}

OpResult resolve_ours(const fs::path& root, const std::string& file) {
    return run(root, {"checkout", "--ours", "--", file});
}

OpResult resolve_theirs(const fs::path& root, const std::string& file) {
    return run(root, {"checkout", "--theirs", "--", file});
}

OpResult mark_resolved(const fs::path& root, const std::string& file) {
    return run(root, {"add", "--", file});
}

OpResult abort_operation(const fs::path& root) {
    std::string op = operation_in_progress(root);
    if (op == "merge") return run(root, {"merge", "--abort"});
    if (op == "rebase") return run(root, {"rebase", "--abort"});
    if (op == "cherry-pick") return run(root, {"cherry-pick", "--abort"});
    if (op == "revert") return run(root, {"revert", "--abort"});
    return OpResult{false, "aucune opération en cours"};
}

std::vector<ReflogEntry> reflog(const fs::path& root, int max_count) {
    std::vector<ReflogEntry> r;
    for (auto& line : capture_lines(root, {"reflog", "--format=%gd%x00%h%x00%gs%x00%cd", "-n", std::to_string(max_count)})) {
        auto f = util::split(line, '\0', true);
        if (f.size() < 2) continue;
        ReflogEntry e;
        e.selector = f[0];
        e.hash = f[1];
        if (f.size() > 2) e.message = f[2];
        if (f.size() > 3) e.date = f[3];
        r.push_back(std::move(e));
    }
    return r;
}

// --- Réécriture d'historique (squash / fixup / drop) --------------------------

bool is_ancestor(const fs::path& root, const std::string& ancestor, const std::string& descendant) {
    std::vector<std::string> cmd = {"git", "-C", root.string(), "merge-base", "--is-ancestor", ancestor, descendant};
    auto r = proc::capture(cmd);
    return r.exit_code == 0;
}

std::string head_subject(const fs::path& root) {
    return capture_one(root, {"log", "-1", "--format=%s"});
}

std::string commit_subject(const fs::path& root, const std::string& commit) {
    return capture_one(root, {"log", "-1", "--format=%s", commit});
}

OpResult commit(const fs::path& root, const std::string& message) {
    return run(root, {"commit", "-m", message});
}

OpResult squash_onto(const fs::path& root, const std::string& commit, const std::string& message) {
    // Ne jamais écraser du travail non commité : un reset soft préserve l'arbre
    // et l'index, mais on refuse quand même si l'arbre de travail est sale, afin
    // de ne pas mélanger des modifications extérieures au squash.
    if (!capture_one(root, {"status", "--porcelain"}).empty()) {
        return OpResult{false, "arbre de travail non vide : validez ou remisez d'abord"};
    }
    // Garde un repère pour revenir en arrière si le commit échoue (aucune perte).
    std::string head_before = capture_one(root, {"rev-parse", "HEAD"});
    auto r = run(root, {"reset", "--soft", commit});
    if (!r.ok) return r;
    r = run(root, {"commit", "-m", message});
    if (!r.ok) {
        // Restaure l'ancien HEAD (soft) : le travail est conservé dans l'index.
        run(root, {"reset", "--soft", head_before.empty() ? "HEAD" : head_before});
    }
    return r;
}

OpResult fixup_last(const fs::path& root) {
    return run(root, {"commit", "--amend", "--no-edit"});
}

OpResult drop_last(const fs::path& root) {
    return run(root, {"reset", "--soft", "HEAD~1"});
}

std::vector<std::string> commits_in_range(const fs::path& root, const std::string& base) {
    return capture_lines(root, {"rev-list", "--reverse", base + "..HEAD"});
}

OpResult drop_commit(const fs::path& root, const std::string& commit) {
    if (!capture_one(root, {"status", "--porcelain"}).empty())
        return OpResult{false, "arbre de travail non vide : validez ou remisez d'abord"};
    if (!is_ancestor(root, commit, "HEAD"))
        return OpResult{false, "le commit n'est pas un ancêtre de HEAD"};
    if (capture_one(root, {"rev-parse", "HEAD"}) == commit)
        return OpResult{false, "impossible de supprimer HEAD ainsi (utilisez « Annuler le dernier commit »)"};

    // Refuse les commits de fusion (plus d'un parent) : `rev-list --parents` = "<c> <p1> [<p2>…]".
    std::string parents = capture_one(root, {"rev-list", "--parents", "-n", "1", commit});
    if (!parents.empty()) {
        auto f = util::split_ws(parents);
        if (f.size() > 2) return OpResult{false, "suppression d'un commit de fusion non prise en charge"};
    }
    // Refuse si la plage à rebaser contient des fusions (elles seraient aplanies).
    if (!capture_one(root, {"rev-list", "--merges", "-n", "1", commit + "..HEAD"}).empty())
        return OpResult{false, "historique non linéaire : suppression risquée (fusions présentes)"};

    auto r = run(root, {"rebase", "--onto", commit + "^", commit});
    if (!r.ok) {
        // Nettoie l'état de rebase éventuel et restaure l'historique d'origine.
        run(root, {"rebase", "--abort"});
    }
    return r;
}

OpResult reorder_commits(const fs::path& root, const std::string& base,
                         const std::vector<std::string>& order) {
    if (!capture_one(root, {"status", "--porcelain"}).empty())
        return OpResult{false, "arbre de travail non vide : validez ou remisez d'abord"};
    // Refuse si la plage contient des fusions (le rebase -i les aplanirait).
    if (!capture_one(root, {"rev-list", "--merges", "-n", "1", base + "..HEAD"}).empty())
        return OpResult{false, "historique non linéaire : réordonnancement risqué (fusions présentes)"};

    // `order` doit être une permutation exacte de base..HEAD.
    auto range = commits_in_range(root, base);
    if (range.size() != order.size())
        return OpResult{false, "ordre invalide (nombre de commits différent)"};
    std::set<std::string> in_range(range.begin(), range.end());
    for (auto& h : order)
        if (!in_range.count(h)) return OpResult{false, "ordre invalide (commit hors plage)"};

    // Construit le todo du rebase interactif et l'écrit dans un fichier temp.
    std::string todo;
    for (auto& h : order) todo += "pick " + h + "\n";
    fs::path tmp = fs::temp_directory_path() / ("cpp-maker-reorder-" + std::to_string(std::time(nullptr)) + ".todo");
    util::write_file(tmp, todo);

    // GIT_SEQUENCE_EDITOR remplace le todo par le nôtre (chemin fixe, aucune
    // donnée utilisateur passée au shell : les hashes sont dans le fichier).
    std::vector<std::string> cmd = {"git", "-C", root.string(), "rebase", "-i", base};
    proc::Options o;
    o.capture = true;
    o.env = {{"GIT_SEQUENCE_EDITOR", "cp " + tmp.string()}};
    auto r = proc::run(cmd, o);

    std::error_code ec;
    fs::remove(tmp, ec);

    if (r.exit_code != 0) {
        run(root, {"rebase", "--abort"});
        OpResult res;
        res.ok = false;
        res.error = util::trim(term::strip_ansi(r.output));
        if (res.error.empty()) res.error = "échec du rebase";
        return res;
    }
    return {};
}

} // namespace vcs
