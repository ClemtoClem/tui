/* @file tests/test_git_rules.cpp — règles pre-commit : lecture, références, mise à jour. */

#include "core/Util.hpp"
#include "model/Editor.hpp"
#include "model/Scaffold.hpp"
#include "vcs/Git.hpp"

#include <gtest/gtest.h>

#include <unistd.h>

namespace {

class GitRulesTest : public ::testing::Test {
protected:
    fs::path root;

    void SetUp() override {
        root = fs::temp_directory_path() / ("cpp-maker-rules-" + std::to_string(::getpid()) + "-" +
                                            ::testing::UnitTest::GetInstance()->current_test_info()->name());
        fs::remove_all(root);
        scaffold::ProjectOptions o;
        o.name = "demo";
        o.dir = root;
        scaffold::create_project(o);
        auto ws = load();
        model::Transaction tx(root);
        auto& p = tx.doc(ws.project.file);
        p.set({"git", "pre_commit", "tests"}, "run", toml::Value("test {targets}"));
        p.set({"git", "pre_commit", "check"}, "run", toml::Value("check --modes asan,tsan -m core.demo"));
        p.set({"git", "pre_commit", "todo"}, "command", toml::Value("tools/todo.sh libs/core/demo/src"));
        p.set({"git", "pre_commit", "todo"}, "when", toml::Value(std::vector<std::string>{"libs/core/demo/**"}));
        tx.doc(ws.find("core.demo")->file).set({"git", "pre_commit", "release"}, "run", toml::Value("build release {targets}"));
        tx.commit();
    }
    void TearDown() override { fs::remove_all(root); }

    model::Workspace load() { return model::Workspace::load_root(root, {.resolve_git = false, .update_git = false, .validate = true}); }
};

} // namespace

TEST_F(GitRulesTest, LectureEtPortees) {
    auto ws = load();
    auto rules = vcs::all_rules(ws);
    ASSERT_EQ(rules.size(), 4u);
    EXPECT_EQ(rules[0].scope, "projet");
    EXPECT_EQ(rules.back().rule->name, "release");
    EXPECT_EQ(rules.back().target, "core.demo");
}

TEST_F(GitRulesTest, ReferencesDeCibles) {
    EXPECT_EQ(vcs::run_selectors("check --modes asan,tsan -m core.demo,net.http"),
              (std::vector<std::string>{"core.demo", "net.http"}));
    EXPECT_EQ(vcs::run_selectors("--output x.log test release core.log -j 4"), (std::vector<std::string>{"release", "core.log"}));
    EXPECT_TRUE(vcs::run_selectors("doctor --fix").empty());
    EXPECT_TRUE(vcs::run_selectors("test {targets}").empty());
}

TEST_F(GitRulesTest, RenommageEtSuppression) {
    auto ws = load();
    {
        model::Transaction tx(root);
        auto report = vcs::update_rules(
            tx, ws, [](const std::string& i) -> std::optional<std::string> {
                if (i == "core.demo") return std::string("core.base");
                return std::nullopt;
            },
            [](const std::string& p) -> std::optional<std::string> {
                if (p == "libs/core/demo" || p.starts_with("libs/core/demo/")) return "libs/core/base" + p.substr(14);
                return std::nullopt;
            },
            false);
        EXPECT_EQ(report.size(), 2u);   // check (run) et todo (when + command)
        tx.commit();
    }
    auto d = toml::Document::load(root / "project.toml");
    EXPECT_EQ(d.get({"git", "pre_commit", "check"}, "run")->as_string(), "check --modes asan,tsan -m core.base");
    EXPECT_EQ(d.get({"git", "pre_commit", "todo"}, "when")->as_strings(), (std::vector<std::string>{"libs/core/base/**"}));
    EXPECT_EQ(d.get({"git", "pre_commit", "todo"}, "command")->as_string(), "tools/todo.sh libs/core/base/src");

    // Suppression : la règle qui ne visait que la cible disparaît, les autres restent.
    ws = load();
    model::Transaction tx(root);
    vcs::update_rules(tx, ws, [](const std::string& i) -> std::optional<std::string> {
        return i == "core.base" ? std::optional<std::string>(std::string()) : std::nullopt;
    }, nullptr, true);
    tx.commit();
    d = toml::Document::load(root / "project.toml");
    EXPECT_FALSE(d.has_table({"git", "pre_commit", "check"}));
    EXPECT_TRUE(d.has_table({"git", "pre_commit", "tests"}));
}

TEST_F(GitRulesTest, ValidationDesRegles) {
    auto ws = load();
    model::Transaction tx(root);
    auto& p = tx.doc(ws.project.file);
    p.set({"git", "pre_commit", "double"}, "run", toml::Value("build"));
    p.set({"git", "pre_commit", "double"}, "command", toml::Value("true"));
    EXPECT_THROW((void)tx.commit(), util::Error);   // run ET command
}
