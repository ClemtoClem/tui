/* @file tests/test_vcs.cpp — backend vcs:: (opérations, blame, stash, worktree).
 *
 * Couvre les composants Git de cpp-maker en isolation, sur des dépôts
 * temporaires créés/détruits à chaque test. Aucune donnée utilisateur n'est
 * touchée : tout se passe sous fs::temp_directory_path().
 */

#include "core/Process.hpp"
#include "core/Util.hpp"
#include "vcs/Backup.hpp"
#include "vcs/Blame.hpp"
#include "vcs/Operations.hpp"
#include "vcs/Stash.hpp"
#include "vcs/Worktree.hpp"

#include <gtest/gtest.h>

#include <unistd.h>

namespace {

int git(const fs::path& root, std::initializer_list<std::string> args) {
    std::vector<std::string> cmd = {"git", "-C", root.string()};
    cmd.insert(cmd.end(), args.begin(), args.end());
    return proc::capture(cmd).exit_code;
}

std::string git_out(const fs::path& root, std::initializer_list<std::string> args) {
    std::vector<std::string> cmd = {"git", "-C", root.string()};
    cmd.insert(cmd.end(), args.begin(), args.end());
    auto r = proc::capture(cmd);
    return r.exit_code == 0 ? r.output : std::string();
}

std::string rev(const fs::path& root, const std::string& ref) {
    auto out = git_out(root, {"rev-parse", ref});
    return util::trim(out);
}

/// Sujets des commits, du plus récent au plus ancien.
std::vector<std::string> subjects(const fs::path& root) {
    std::vector<std::string> r;
    for (auto& l : util::split(git_out(root, {"log", "--format=%s"}), '\n', false)) r.push_back(l);
    return r;
}

/// Dépôt temporaire prêt à l'emploi (utilisateur « Alice » configuré).
class GitRepo : public ::testing::Test {
protected:
    fs::path root;

    void SetUp() override {
        root = fs::temp_directory_path() / ("cpp-maker-vcs-" + std::to_string(::getpid()) + "-" +
                                            ::testing::UnitTest::GetInstance()->current_test_info()->name());
        fs::remove_all(root);
        fs::create_directories(root);
        ASSERT_EQ(git(root, {"init", "-q"}), 0);
        ASSERT_EQ(git(root, {"config", "user.email", "a@a"}), 0);
        ASSERT_EQ(git(root, {"config", "user.name", "Alice"}), 0);
    }
    void TearDown() override { fs::remove_all(root); }

    void commit(const std::string& file, const std::string& content, const std::string& msg) {
        util::write_file(root / file, content);
        ASSERT_EQ(git(root, {"add", file}), 0);
        ASSERT_EQ(git(root, {"commit", "-q", "-m", msg}), 0);
    }
};

} // namespace

// ============================================================================
// Opérations (commit / squash / fixup / drop / reorder)
// ============================================================================

TEST_F(GitRepo, IsAncestorEtSujets) {
    commit("a.txt", "1", "commit 1");
    std::string c1 = rev(root, "HEAD");
    commit("b.txt", "2", "commit 2");
    commit("c.txt", "3", "commit 3");

    EXPECT_TRUE(vcs::is_ancestor(root, c1, "HEAD"));
    EXPECT_EQ(vcs::head_subject(root), "commit 3");
    EXPECT_EQ(vcs::commit_subject(root, c1), "commit 1");
}

TEST_F(GitRepo, CommitEtDropLast) {
    commit("a.txt", "1", "commit 1");
    commit("b.txt", "2", "commit 2");
    ASSERT_EQ(subjects(root).size(), 2u);

    // drop_last : annule le dernier commit en conservant les modifs (soft).
    ASSERT_TRUE(vcs::drop_last(root).ok);
    ASSERT_EQ(subjects(root).size(), 1u);
    EXPECT_EQ(subjects(root)[0], "commit 1");
    // Le fichier de « commit 2 » est encore là (staged) : arbre sale.
    EXPECT_TRUE(vcs::working_tree_dirty(root));
}

TEST_F(GitRepo, FixupLastFusionneSansChangerLeMessage) {
    commit("a.txt", "1", "commit 1");
    util::write_file(root / "a.txt", "1\nplus\n");
    git(root, {"add", "a.txt"});
    ASSERT_TRUE(vcs::fixup_last(root).ok);
    ASSERT_EQ(subjects(root).size(), 1u);
    EXPECT_EQ(subjects(root)[0], "commit 1"); // message inchangé
    EXPECT_EQ(util::trim(git_out(root, {"show", "HEAD:a.txt"})), "1\nplus");
}

TEST_F(GitRepo, SquashOnto) {
    commit("a.txt", "1", "commit 1");
    std::string c1 = rev(root, "HEAD");
    commit("b.txt", "2", "commit 2");
    commit("c.txt", "3", "commit 3");
    ASSERT_EQ(subjects(root).size(), 3u);

    ASSERT_TRUE(vcs::squash_onto(root, c1, "tout fusionne").ok);
    auto s = subjects(root);
    ASSERT_EQ(s.size(), 2u);
    EXPECT_EQ(s[0], "tout fusionne");   // le plus récent = squash
    EXPECT_EQ(s[1], "commit 1");
}

TEST_F(GitRepo, SquashRefuseArbreSale) {
    commit("a.txt", "1", "commit 1");
    std::string c1 = rev(root, "HEAD");
    commit("b.txt", "2", "commit 2");
    util::write_file(root / "a.txt", "modifié");
    auto r = vcs::squash_onto(root, c1, "x");
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(subjects(root).size(), 2u); // rien n'a bougé
}

TEST_F(GitRepo, DropCommitArbitraire) {
    for (int i = 1; i <= 4; ++i) commit("f" + std::to_string(i) + ".txt", std::to_string(i), "commit " + std::to_string(i));
    std::string c3 = rev(root, "HEAD~1");
    ASSERT_TRUE(vcs::drop_commit(root, c3).ok);
    auto s = subjects(root);
    ASSERT_EQ(s.size(), 3u);
    EXPECT_EQ(s[0], "commit 4");
    EXPECT_EQ(s[1], "commit 2");
    EXPECT_EQ(s[2], "commit 1");
}

TEST_F(GitRepo, DropCommitRefuseMerge) {
    commit("a.txt", "1", "commit 1");
    // Branche parallèle fusionnée : crée un commit de fusion (HEAD~1).
    git(root, {"checkout", "-q", "-b", "side"});
    commit("s.txt", "s", "side");
    git(root, {"checkout", "-q", "-"});
    commit("m.txt", "m", "commit 2");
    git(root, {"merge", "-q", "--no-edit", "side"});
    commit("top.txt", "t", "commit 3");
    std::string merge = rev(root, "HEAD~1");
    EXPECT_FALSE(vcs::drop_commit(root, merge).ok);
}

TEST_F(GitRepo, ReorderCommits) {
    for (int i = 1; i <= 3; ++i) commit("f" + std::to_string(i) + ".txt", std::to_string(i), "commit " + std::to_string(i));
    std::string base = rev(root, "HEAD~2");
    std::string c2 = rev(root, "HEAD~1");
    std::string c3 = rev(root, "HEAD");

    // Échange c2 et c3 (ordre souhaité, du plus ancien au plus récent : c3, c2).
    ASSERT_TRUE(vcs::reorder_commits(root, base, {c3, c2}).ok);
    auto s = subjects(root);
    ASSERT_EQ(s.size(), 3u);
    EXPECT_EQ(s[0], "commit 2");
    EXPECT_EQ(s[1], "commit 3");
    EXPECT_EQ(s[2], "commit 1");
}

TEST_F(GitRepo, ReorderValideLaPermutation) {
    for (int i = 1; i <= 3; ++i) commit("f" + std::to_string(i) + ".txt", std::to_string(i), "commit " + std::to_string(i));
    std::string base = rev(root, "HEAD~2");
    std::string c2 = rev(root, "HEAD~1");
    // Il manque un commit dans l'ordre → refusé.
    auto r = vcs::reorder_commits(root, base, {c2});
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(subjects(root).size(), 3u); // inchangé
}

// ============================================================================
// Blame
// ============================================================================

TEST_F(GitRepo, BlameAttribueLesLignes) {
    util::write_file(root / "f.txt", "un\ndeux\ntrois\n");
    git(root, {"add", "f.txt"});
    git(root, {"commit", "-q", "-m", "commit Alice"});
    git(root, {"config", "user.name", "Bob"});
    util::write_file(root / "f.txt", "un\nDEUX\ntrois\n");
    git(root, {"commit", "-qam", "commit Bob"});

    auto b = vcs::blame(root, "f.txt");
    ASSERT_EQ(b.size(), 3u);
    EXPECT_EQ(b[0].author, "Alice");
    EXPECT_EQ(b[0].content, "un");
    EXPECT_EQ(b[1].author, "Bob");
    EXPECT_EQ(b[1].content, "DEUX");
    EXPECT_EQ(b[2].author, "Alice"); // ligne non modifiée → auteur d'origine
    EXPECT_EQ(b[2].content, "trois");
}

TEST_F(GitRepo, BlameFichierAbsent) {
    EXPECT_TRUE(vcs::blame(root, "inexistant.txt").empty());
}

// ============================================================================
// Stash
// ============================================================================

TEST_F(GitRepo, StashPushApplyEtPop) {
    commit("a.txt", "1", "base");
    util::write_file(root / "a.txt", "modifié");
    ASSERT_TRUE(vcs::stash_push(root, "mon stash").ok);
    EXPECT_FALSE(vcs::working_tree_dirty(root));

    auto list = vcs::stash_list(root);
    ASSERT_EQ(list.size(), 1u);
    EXPECT_EQ(list[0].message, "mon stash");

    ASSERT_TRUE(vcs::stash_apply(root, list[0].ref).ok);
    EXPECT_EQ(util::trim(util::read_file_or_throw(root / "a.txt")), "modifié");

    // pop (applique déjà → conflit possible) : on nettoie d'abord.
    git(root, {"checkout", "--", "a.txt"});
    ASSERT_TRUE(vcs::stash_pop(root, list[0].ref).ok);
    EXPECT_TRUE(vcs::stash_list(root).empty());
}

// ============================================================================
// Worktree
// ============================================================================

TEST_F(GitRepo, WorktreeAddListRemove) {
    commit("a.txt", "1", "base");
    ASSERT_EQ(vcs::worktree_list(root).size(), 1u);

    fs::path wt = root.string() + "-feature";
    ASSERT_TRUE(vcs::worktree_add(root, wt.string(), "feature", "").ok);
    auto l = vcs::worktree_list(root);
    ASSERT_EQ(l.size(), 2u);

    // Un worktree sale ne peut pas être supprimé (pas de --force).
    util::write_file(wt / "a.txt", "sale");
    EXPECT_FALSE(vcs::worktree_remove(root, wt.string()).ok);

    git(wt, {"checkout", "--", "a.txt"});
    ASSERT_TRUE(vcs::worktree_remove(root, wt.string()).ok);
    EXPECT_EQ(vcs::worktree_list(root).size(), 1u);
}

// ============================================================================
// Reflog
// ============================================================================

TEST_F(GitRepo, ReflogListeLesAnciensHead) {
    commit("a.txt", "1", "commit 1");
    commit("b.txt", "2", "commit 2");
    auto r = vcs::reflog(root, 10);
    ASSERT_GE(r.size(), 2u);
    EXPECT_EQ(r[0].selector, "HEAD@{0}");
    EXPECT_EQ(r[0].message, "commit: commit 2");
}
