/* @file tests/test_includes.cpp — analyse des #include et des modules C++. */

#include "analysis/Includes.hpp"
#include "core/Util.hpp"
#include "model/Scaffold.hpp"
#include "model/Workspace.hpp"

#include <gtest/gtest.h>

#include <unistd.h>

namespace {

std::string test_name() { return ::testing::UnitTest::GetInstance()->current_test_info()->name(); }

fs::path tmp_file(const char* suffix, const std::string& content) {
    fs::path p = fs::temp_directory_path() /
                 (std::string("cpp-maker-incl-") + std::to_string(::getpid()) + "-" + test_name() + suffix);
    util::write_file(p, content);
    return p;
}

/// Projet temporaire : layer core + module core.demo + application demo.
class IncludesTest : public ::testing::Test {
protected:
    fs::path root;

    void SetUp() override {
        root = fs::temp_directory_path() / ("cpp-maker-incl-" + std::to_string(::getpid()) + "-" + test_name());
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
    void add_layer(const char* name) {
        auto ws = load();
        model::Transaction tx(root);
        scaffold::add_layer(tx, ws, {name, "", {}, ""});
        tx.commit();
    }
    void add_module(const char* layer, const char* name, const char* type = "header-only") {
        auto ws = load();
        model::Transaction tx(root);
        scaffold::add_module(tx, ws, {layer, name, type, "c++", {}, "", true});
        tx.commit();
    }
    void add_app(const char* name) {
        auto ws = load();
        model::Transaction tx(root);
        scaffold::add_app(tx, ws, {name, "", "c++", {}, "", false});
        tx.commit();
    }
    void write(const char* rel, const std::string& content) { util::write_file(root / rel, content); }

    bool has(const std::vector<analysis::MissingDep>& v, const std::string& target, const std::string& module) {
        for (auto& m : v)
            if (m.target == target && m.module == module && !m.ambiguous()) return true;
        return false;
    }
};

} // namespace

// --- Analyse lexicale ---------------------------------------------------------

TEST(Scan, Includes) {
    auto f = tmp_file(".hpp", "#include <vector>\n#include \"foo/bar.hpp\"\n#include   <spaced.hpp>\n");
    EXPECT_EQ(analysis::scan_includes(f), (std::vector<std::string>{"vector", "foo/bar.hpp", "spaced.hpp"}));
    fs::remove(f);
}

TEST(Scan, Modules) {
    auto f = tmp_file(".cppm", R"(export module core.demo;
import core.other;
export import net.http;
import <vector>;
import "header.hpp";
module;              // fragment global
// import ignored.mod;
/* import also.ignored; */
)");
    EXPECT_EQ(analysis::scan_module_decls(f), (std::vector<std::string>{"core.demo"}));
    EXPECT_EQ(analysis::scan_imports(f), (std::vector<std::string>{"core.other", "net.http"}));
    fs::remove(f);
}

// --- Détection de dépendances implicites --------------------------------------

TEST_F(IncludesTest, InclusionSansDependance) {
    add_module("core", "extra");
    write("libs/core/demo/src/demo.cpp", "#include \"demo/demo.hpp\"\n#include \"extra/extra.hpp\"\n");
    EXPECT_TRUE(has(analysis::detect_missing_deps(load()), "core.demo", "core.extra"));
}

TEST_F(IncludesTest, RaccourciFichierPrincipal) {
    // « #include "demo" » désigne le fichier principal include/demo/demo.hpp.
    add_app("probe");
    write("apps/probe/src/main.cpp", "#include \"demo\"\nint main(){return 0;}\n");
    EXPECT_TRUE(has(analysis::detect_missing_deps(load()), "probe", "core.demo"));
}

TEST_F(IncludesTest, RaccourciQualifieLayer) {
    // Deux modules « log » dans deux layers : « net/log » lève l'ambiguïté.
    add_layer("net");
    add_module("core", "log");
    add_module("net", "log");
    add_app("probe");
    write("apps/probe/src/main.cpp", "#include \"net/log\"\nint main(){return 0;}\n");
    EXPECT_TRUE(has(analysis::detect_missing_deps(load()), "probe", "net.log"));
}

TEST_F(IncludesTest, RaccourciAmbigu) {
    add_layer("net");
    add_module("core", "log");
    add_module("net", "log");
    add_app("probe");
    write("apps/probe/src/main.cpp", "#include \"log\"\nint main(){return 0;}\n");
    auto missing = analysis::detect_missing_deps(load());
    bool found = false;
    for (auto& m : missing)
        if (m.target == "probe" && m.ambiguous() && m.header == "log") {
            EXPECT_EQ(m.alternatives.size(), 2u);
            found = true;
        }
    EXPECT_TRUE(found);
}

TEST_F(IncludesTest, RaccourciPrefereMemeLayer) {
    // Deux modules « log » : un module du layer core préfère core.log.
    add_layer("net");
    add_module("core", "log");
    add_module("net", "log");
    add_module("core", "other");
    write("libs/core/other/include/other/other.hpp", "#pragma once\n#include \"log\"\n");
    EXPECT_TRUE(has(analysis::detect_missing_deps(load()), "core.other", "core.log"));
}

TEST_F(IncludesTest, ImportDeModule) {
    // « import core.demo; » détecte la dépendance sans en-tête.
    add_app("probe");
    write("apps/probe/src/main.cpp", "import core.demo;\nint main(){return 0;}\n");
    auto missing = analysis::detect_missing_deps(load());
    bool found = false;
    for (auto& m : missing)
        if (m.target == "probe" && m.module == "core.demo" && m.via_import) found = true;
    EXPECT_TRUE(found);
}

TEST_F(IncludesTest, ImportNomDeclare) {
    // « export module demo; » relie le nom C++ « demo » au module core.demo.
    write("libs/core/demo/src/demo.cppm", "export module demo;\n");
    add_app("probe");
    write("apps/probe/src/main.cpp", "import demo;\nint main(){return 0;}\n");
    auto missing = analysis::detect_missing_deps(load());
    bool found = false;
    for (auto& m : missing)
        if (m.target == "probe" && m.module == "core.demo" && m.via_import) found = true;
    EXPECT_TRUE(found);
}
