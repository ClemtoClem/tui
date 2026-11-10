/* @file tests/test_review.cpp — workflow d'approbation ([review]). */

#include "core/Util.hpp"
#include "model/Editor.hpp"
#include "model/Scaffold.hpp"
#include "model/Schema.hpp"
#include "model/Workspace.hpp"

#include <gtest/gtest.h>

#include <unistd.h>

namespace {

class ReviewTest : public ::testing::Test {
protected:
    fs::path root;

    void SetUp() override {
        root = fs::temp_directory_path() / ("cpp-maker-review-" + std::to_string(::getpid()) + "-" +
                                            ::testing::UnitTest::GetInstance()->current_test_info()->name());
        fs::remove_all(root);
        scaffold::ProjectOptions o;
        o.name = "demo";
        o.dir = root;
        scaffold::create_project(o);
    }
    void TearDown() override { fs::remove_all(root); }

    model::Workspace load() {
        return model::Workspace::load_root(root, {.resolve_git = false, .update_git = false, .validate = true});
    }
};

} // namespace

TEST_F(ReviewTest, ParDefautNonExaminee) {
    auto ws = load();
    auto* t = ws.find("core.demo");
    ASSERT_NE(t, nullptr);
    EXPECT_EQ(t->review.status, "none");
    EXPECT_FALSE(t->review.is_approved());
    EXPECT_FALSE(t->review.is_set());
}

TEST_F(ReviewTest, ApprobationEcritureEtRechargement) {
    {
        auto ws = load();
        model::Transaction tx(root);
        auto& d = tx.doc(ws.find("core.demo")->file);
        d.set({"review"}, "status", toml::Value(std::string("approved")));
        d.set({"review"}, "reviewer", toml::Value(std::string("alice")));
        d.set({"review"}, "date", toml::Value(std::string("2026-11-10")));
        tx.commit();
    }
    auto ws = load();
    auto* t = ws.find("core.demo");
    EXPECT_EQ(t->review.status, "approved");
    EXPECT_EQ(t->review.reviewer, "alice");
    EXPECT_EQ(t->review.date, "2026-11-10");
    EXPECT_TRUE(t->review.is_approved());
}

TEST_F(ReviewTest, StatutInvalideRefuse) {
    auto ws = load();
    model::Transaction tx(root);
    tx.doc(ws.find("core.demo")->file).set({"review"}, "status", toml::Value(std::string("super")));
    EXPECT_THROW((void)tx.commit(), util::Error);
    EXPECT_EQ(load().find("core.demo")->review.status, "none");
}

TEST_F(ReviewTest, VerrouActivable) {
    auto ws = load();
    EXPECT_FALSE(ws.project.require_approval);
    model::Transaction tx(root);
    tx.doc(ws.project.file).set({"build"}, "require_approval", toml::Value(true));
    ws = tx.commit();
    EXPECT_TRUE(ws.project.require_approval);
}

TEST(ReviewSchema, SectionsEtChamps) {
    auto* status = schema::find(schema::FileKind::Module, {"review"}, "status");
    ASSERT_NE(status, nullptr);
    EXPECT_EQ(status->type, schema::FieldType::Enum);
    EXPECT_NE(schema::find(schema::FileKind::Application, {"review"}, "status"), nullptr);
    EXPECT_NE(schema::find(schema::FileKind::Project, {"build"}, "require_approval"), nullptr);
    EXPECT_NE(schema::find(schema::FileKind::Project, {"build"}, "ccache"), nullptr);
}
