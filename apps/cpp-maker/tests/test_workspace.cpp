/* @file tests/test_workspace.cpp — modèle, schéma, validation et transactions. */

#include "core/Util.hpp"
#include "model/Editor.hpp"
#include "model/Scaffold.hpp"
#include "model/Schema.hpp"
#include "model/Workspace.hpp"

#include <gtest/gtest.h>

#include <unistd.h>

namespace {

/// Projet temporaire : layer core (hello), layer net (http → core.hello), app hello.
class WorkspaceTest : public ::testing::Test {
protected:
    fs::path root;

    void SetUp() override {
        root = fs::temp_directory_path() / ("cpp-maker-test-" + std::to_string(::getpid()) + "-" +
                                            ::testing::UnitTest::GetInstance()->current_test_info()->name());
        fs::remove_all(root);
        scaffold::ProjectOptions o;
        o.name = "demo";
        o.dir = root;
        scaffold::create_project(o);
        auto ws = load();
        model::Transaction tx(root);
        scaffold::add_layer(tx, ws, {"net", "", {}, ""});
        ws = tx.commit();
        model::Transaction tx2(root);
        scaffold::add_module(tx2, ws, {"net", "http", "shared", "", {"core.demo"}, "", true});
        tx2.commit();
    }
    void TearDown() override { fs::remove_all(root); }

    model::Workspace load() { return model::Workspace::load_root(root, {.resolve_git = false, .update_git = false, .validate = true}); }
};

} // namespace

TEST_F(WorkspaceTest, DecouverteEtIdentifiants) {
    auto ws = load();
    ASSERT_EQ(ws.layers.size(), 2u);
    EXPECT_EQ(ws.layers[0].name, "core");   // ordre de [layers].order
    EXPECT_EQ(ws.layers[1].name, "net");
    ASSERT_NE(ws.find("core.demo"), nullptr);
    ASSERT_NE(ws.find("net.http"), nullptr);
    ASSERT_NE(ws.find("demo"), nullptr);
    EXPECT_TRUE(ws.find("demo")->is_app());
    EXPECT_EQ(ws.find("net.http")->dep_ids, (std::vector<std::string>{"core.demo"}));
    EXPECT_EQ(ws.layer_rank("net"), 1);
    EXPECT_TRUE(ws.validate().empty());
}

TEST_F(WorkspaceTest, ReferencesEtSelecteurs) {
    auto ws = load();
    EXPECT_EQ(ws.resolve_module_ref("http"), (std::vector<std::string>{"net.http"}));
    EXPECT_EQ(ws.resolve_module_ref("net.*"), (std::vector<std::string>{"net.http"}));
    EXPECT_EQ(ws.resolve_module_ref("net"), (std::vector<std::string>{"net.http"}));
    EXPECT_THROW((void)ws.resolve_module_ref("demo"), util::Error) << "une application n'est pas une dépendance";
    EXPECT_EQ(ws.resolve_selector("demo"), (std::vector<std::string>{"demo"}));   // l'application prime
    EXPECT_THROW((void)ws.resolve_selector("inconnu"), util::Error);
    EXPECT_EQ(ws.targets_at(root / "libs" / "net" / "http" / "src"), (std::vector<std::string>{"net.http"}));
    auto order = ws.topo_order({"net.http"});
    EXPECT_EQ(order, (std::vector<std::string>{"core.demo", "net.http"}));
}

TEST_F(WorkspaceTest, CouchesEtCyclesRefuses) {
    auto ws = load();
    model::Transaction tx(root);
    tx.doc(ws.find("core.demo")->file).set({"dependencies"}, "modules", toml::Value(std::vector<std::string>{"net.http"}));
    std::string before = util::read_file_or_throw(ws.find("core.demo")->file);
    try {
        (void)tx.commit();
        FAIL() << "dépendance ascendante acceptée";
    } catch (const util::Error& e) {
        std::string m = e.what();
        EXPECT_NE(m.find("circulaire"), std::string::npos);
        EXPECT_NE(m.find("supérieure"), std::string::npos);
    }
    // Restauration complète.
    EXPECT_EQ(util::read_file_or_throw(ws.find("core.demo")->file), before);
}

TEST_F(WorkspaceTest, TransactionRenommageAnnule) {
    auto ws = load();
    {
        model::Transaction tx(root);
        tx.rename(root / "libs" / "net", root / "libs" / "reseau");
        tx.doc(root / "libs" / "reseau" / "layer.toml").set({"layer"}, "name", toml::Value("reseau"));
        // [layers].order n'est pas mis à jour : le projet devient invalide.
        EXPECT_THROW((void)tx.commit(), util::Error);
    }
    EXPECT_TRUE(fs::exists(root / "libs" / "net" / "layer.toml"));
    EXPECT_FALSE(fs::exists(root / "libs" / "reseau"));
    EXPECT_TRUE(load().validate().empty());
}

TEST(Schema, ClesEtLint) {
    std::vector<std::string> sec;
    std::string key;
    EXPECT_TRUE(schema::split_key(schema::FileKind::Project, "build.modes.prof.flags", sec, key));
    EXPECT_EQ(sec, (std::vector<std::string>{"build", "modes", "prof"}));
    EXPECT_EQ(key, "flags");
    auto root = toml::parse("[build]\njbos = 2\n[biuld]\nx = 1\n[project]\nname = 3\n");
    auto w = schema::lint(schema::FileKind::Project, root, "p.toml");
    ASSERT_EQ(w.size(), 3u);
    EXPECT_NE(w[0].find("'jobs'"), std::string::npos);    // suggestion
    EXPECT_NE(w[1].find("[build]"), std::string::npos);   // section inconnue → suggestion
    auto* f = schema::find(schema::FileKind::Module, {"module"}, "type");
    ASSERT_NE(f, nullptr);
    EXPECT_THROW((void)schema::parse_input(*f, "dynamique"), util::Error);
    EXPECT_EQ(schema::parse_input(*f, "shared").as_string(), "shared");
}

TEST_F(WorkspaceTest, CheminsChaineOuTableau) {
    // Chaîne simple (défaut du scaffold) : un seul dossier de layers et d'applications.
    auto ws = load();
    EXPECT_EQ(ws.project.layers_dirs, (std::vector<std::string>{"libs"}));
    EXPECT_EQ(ws.project.apps_dirs, (std::vector<std::string>{"apps"}));
    // Tableaux : plusieurs dossiers, build en tableau d'un seul élément.
    {
        model::Transaction tx(root);
        auto& d = tx.doc(ws.project.file);
        d.set({"paths"}, "layers", toml::Value(std::vector<std::string>{"libs", "vendor"}));
        d.set({"paths"}, "applications", toml::Value(std::vector<std::string>{"apps", "examples"}));
        d.set({"paths"}, "build", toml::Value(std::vector<std::string>{"out"}));
        ws = tx.commit();
    }
    EXPECT_EQ(ws.project.layers_dirs.size(), 2u);
    EXPECT_EQ(ws.project.build_dir, "out");
    model::Transaction tx(root);
    scaffold::add_layer(tx, ws, {"ext", "", {}, "vendor"});
    ws = tx.commit();
    ASSERT_NE(ws.find_layer("ext"), nullptr);
    EXPECT_EQ(ws.find_layer("ext")->dir, fs::path("vendor/ext"));
    // build/dist : un seul dossier.
    model::Transaction bad(root);
    bad.doc(ws.project.file).set({"paths"}, "dist", toml::Value(std::vector<std::string>{"a", "b"}));
    EXPECT_THROW((void)bad.commit(), util::Error);
}

TEST_F(WorkspaceTest, ReadmeCrees) {
    auto exists = [&](const char* rel) {
        std::error_code ec;
        return fs::is_regular_file(root / rel, ec);
    };
    EXPECT_TRUE(exists("README.md"));                 // projet
    EXPECT_TRUE(exists("libs/core/README.md"));       // layer
    EXPECT_TRUE(exists("libs/core/demo/README.md"));  // module
    EXPECT_TRUE(exists("libs/net/README.md"));
    EXPECT_TRUE(exists("libs/net/http/README.md"));
    EXPECT_TRUE(exists("apps/demo/README.md"));       // application
}
