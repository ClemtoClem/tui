/* @file tests/test_toml.cpp — parseur, sérialiseur et Document éditable. */

#include "io/TomlDocument.hpp"
#include "io/toml.hpp"

#include <gtest/gtest.h>

TEST(Toml, ScalairesEtTables) {
    auto v = toml::parse(R"(
titre = "a\tb"
lit = 'C:\chemin'
n = 1_000
hex = 0xff
f = 2.5e1
oui = true
date = 1979-05-27T07:32:00Z
[serveur.http]
port = 8080
)");
    EXPECT_EQ(v.find("titre")->as_string(), "a\tb");
    EXPECT_EQ(v.find("lit")->as_string(), "C:\\chemin");
    EXPECT_EQ(v.find("n")->as_integer(), 1000);
    EXPECT_EQ(v.find("hex")->as_integer(), 255);
    EXPECT_DOUBLE_EQ(v.find("f")->as_float(), 25.0);
    EXPECT_TRUE(v.find("oui")->as_bool());
    EXPECT_EQ(v.find("date")->as_string(), "1979-05-27T07:32:00Z");
    ASSERT_NE(v.at_path({"serveur", "http", "port"}), nullptr);
    EXPECT_EQ(v.at_path({"serveur", "http", "port"})->as_integer(), 8080);
}

TEST(Toml, TableauxMultiLignesEtTablesEnLigne) {
    auto v = toml::parse("w = [\n  \"-Wall\", # commentaire\n  \"-Wextra\",\n]\nenv = { HOME = \"/tmp\", n.x = 1 }\n"
                         "[[d]]\nnom = \"a\"\n[[d]]\nnom = \"b\"\n");
    bool ok = false;
    EXPECT_EQ(v.find("w")->as_strings(&ok), (std::vector<std::string>{"-Wall", "-Wextra"}));
    EXPECT_TRUE(ok);
    EXPECT_TRUE(v.find("env")->is_inline());
    EXPECT_EQ(v.at_path({"env", "n", "x"})->as_integer(), 1);
    ASSERT_EQ(v.find("d")->items().size(), 2u);
    EXPECT_EQ(v.find("d")->items()[1].find("nom")->as_string(), "b");
}

TEST(Toml, ChainesMultiLignes) {
    auto v = toml::parse("a = \"\"\"\nun \\\n   deux\"\"\"\nb = '''\nx\ny'''\n");
    EXPECT_EQ(v.find("a")->as_string(), "un deux");
    EXPECT_EQ(v.find("b")->as_string(), "x\ny");
}

TEST(Toml, ErreursLocalisees) {
    try {
        (void)toml::parse("a = 1\nb = [1,\n2\nc = 3\n", "x.toml");
        FAIL() << "erreur attendue";
    } catch (const toml::ParseError& e) {
        EXPECT_EQ(e.file(), "x.toml");
        EXPECT_EQ(e.line(), 4u);
    }
    EXPECT_THROW((void)toml::parse("a = 1\na = 2\n"), toml::ParseError);
    EXPECT_THROW((void)toml::parse("[t]\n[t]\n"), toml::ParseError);
    EXPECT_THROW((void)toml::parse("a = \"non terminée\n"), toml::ParseError);
}

TEST(Toml, SerialisationAllerRetour) {
    auto v = toml::parse("[a]\nx = [\"1\", \"2\"]\n[a.b]\ny = true\n[[c]]\nz = 1.5\n");
    auto again = toml::parse(toml::serialize(v));
    EXPECT_TRUE(v == again);
}

TEST(TomlDocument, ConserveCommentairesEtAlignement) {
    auto d = toml::Document::from_text("# en-tête\n[build]\njobs = 0          # 0 = auto\nmodes.fast.flags = [\"-O3\"]\n");
    d.set({"build"}, "jobs", toml::Value(4));
    d.set({"build", "modes", "fast"}, "defines", toml::Value(std::vector<std::string>{"NDEBUG"}));
    d.set({"package"}, "strip", toml::Value(true));
    EXPECT_NE(d.text().find("# en-tête"), std::string::npos);
    EXPECT_NE(d.text().find("jobs = 4          # 0 = auto"), std::string::npos);
    EXPECT_NE(d.text().find("modes.fast.defines = [\"NDEBUG\"]"), std::string::npos);
    EXPECT_NE(d.text().find("[package]\nstrip = true"), std::string::npos);
    EXPECT_EQ(d.data().at_path({"build", "jobs"})->as_integer(), 4);
}

TEST(TomlDocument, TablesEnLigneEtSuppression) {
    auto d = toml::Document::from_text("[tests]\nenv = { A = \"1\" }\n[x.y]\nk = 1\n[x.y.z]\nm = 2\n[w]\nq = 3\n");
    d.set({"tests", "env"}, "B", toml::Value("2"));
    EXPECT_EQ(d.data().at_path({"tests", "env", "B"})->as_string(), "2");
    EXPECT_TRUE(d.remove({"tests", "env"}, "A"));
    EXPECT_EQ(d.data().at_path({"tests", "env", "A"}), nullptr);
    EXPECT_TRUE(d.remove_table({"x", "y"}));
    EXPECT_EQ(d.data().find("x"), nullptr);
    EXPECT_EQ(d.data().at_path({"w", "q"})->as_integer(), 3);
    EXPECT_TRUE(d.rename_table({"w"}, {"v"}));
    EXPECT_EQ(d.data().at_path({"v", "q"})->as_integer(), 3);
}

TEST(TomlDocument, ModificationInvalideRefusee) {
    auto d = toml::Document::from_text("[a]\nx = 1\n");
    std::string before = d.text();
    EXPECT_THROW(d.replace_text("[a]\nx = \n"), toml::ParseError);
    EXPECT_EQ(d.text(), before);
}

TEST(Toml, SaisieUtilisateur) {
    auto l = toml::value_from_text("a, b ,c", toml::Type::Array);
    ASSERT_TRUE(l);
    EXPECT_EQ(l->as_strings(), (std::vector<std::string>{"a", "b", "c"}));
    EXPECT_TRUE(toml::value_from_text("oui", toml::Type::Boolean)->as_bool());
    EXPECT_FALSE(toml::value_from_text("abc", toml::Type::Integer).has_value());
    EXPECT_EQ(toml::value_to_text(toml::Value(std::vector<std::string>{"x", "y"})), "x, y");
}
