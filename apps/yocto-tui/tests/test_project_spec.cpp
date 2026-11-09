#include <gtest/gtest.h>

#include "config/YoctoConfig.hpp"
#include "project/Presets.hpp"
#include "project/ProjectSpec.hpp"

using namespace yocto;

namespace {

bool has_issue(const std::vector<Issue>& issues, Issue::Level level, const std::string& needle) {
    for (const auto& i : issues) {
        if (i.level == level && (i.message.find(needle) != std::string::npos ||
                                 i.where.find(needle) != std::string::npos)) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST(IniEscape, RoundTripsSpecialCharacters) {
    const std::string samples[] = {
        "",
        "simple",
        "\n  indenté\n\tavec tab\n",
        "  espaces de tête et de fin  ",
        "barre | verticale et \\ antislash",
        "\\n littéral",
        "   ",
    };
    for (const auto& s : samples) {
        EXPECT_EQ(ini_unescape(ini_escape(s)), s) << "échantillon : [" << s << "]";
        // Une valeur échappée tient sur une ligne, sans espace en bordure.
        const std::string e = ini_escape(s);
        EXPECT_EQ(e.find('\n'), std::string::npos);
        if (!e.empty()) {
            EXPECT_NE(e.front(), ' ');
            EXPECT_NE(e.back(), ' ');
        }
    }
}

TEST(ProjectSpecIni, PresetSurvivesYoctoConfigRoundTrip) {
    YoctoConfig cfg;
    cfg.name = "tp";
    cfg.project = formation_preset("secret");
    cfg.apply_defaults();

    std::string error;
    const YoctoConfig back = YoctoConfig::from_ini(cfg.to_ini(), error);
    ASSERT_TRUE(error.empty()) << error;

    const ProjectSpec& a = cfg.project;
    const ProjectSpec& b = back.project;
    EXPECT_EQ(b.layer_name, a.layer_name);
    EXPECT_EQ(b.layer_depends, a.layer_depends);
    EXPECT_EQ(b.image.name, a.image.name);
    EXPECT_EQ(b.image.hostname, a.image.hostname);
    ASSERT_EQ(b.image.users.size(), 1u);
    EXPECT_EQ(b.image.users[0].password, "secret");
    ASSERT_EQ(b.extra_layers.size(), a.extra_layers.size());
    EXPECT_EQ(b.extra_layers[0].sublayers, "meta-oe");

    ASSERT_EQ(b.apps.size(), a.apps.size());
    for (size_t i = 0; i < a.apps.size(); ++i) {
        EXPECT_EQ(b.apps[i].name, a.apps[i].name);
        EXPECT_EQ(b.apps[i].kind, a.apps[i].kind);
        EXPECT_EQ(b.apps[i].in_image, a.apps[i].in_image);
        ASSERT_EQ(b.apps[i].files.size(), a.apps[i].files.size());
        for (size_t j = 0; j < a.apps[i].files.size(); ++j) {
            EXPECT_EQ(b.apps[i].files[j].name, a.apps[i].files[j].name);
            EXPECT_EQ(b.apps[i].files[j].dest, a.apps[i].files[j].dest);
            EXPECT_EQ(b.apps[i].files[j].mode, a.apps[i].files[j].mode);
            EXPECT_EQ(b.apps[i].files[j].content, a.apps[i].files[j].content);
        }
    }
    ASSERT_EQ(b.rootfs_files.size(), 2u);
    // Le motd commence par une ligne vide et des espaces : préservés.
    EXPECT_EQ(b.rootfs_files[0].content, a.rootfs_files[0].content);
    ASSERT_EQ(b.fragments.size(), 1u);
    EXPECT_EQ(b.fragments[0].content, "CONFIG_NTPD=y\n");
    ASSERT_EQ(b.patches.size(), 1u);
    EXPECT_EQ(b.patches[0].replace, a.patches[0].replace);
    EXPECT_EQ(b.patches[0].method, PatchMethod::Devtool);

    // Les champs historiques de YoctoConfig ne sont pas perturbés.
    EXPECT_EQ(back.machine, cfg.machine);
    EXPECT_EQ(back.poky_branch, cfg.poky_branch);
}

TEST(ProjectSpecIni, OldConfigWithoutProjectKeepsDefaults) {
    std::string error;
    const auto cfg = YoctoConfig::from_ini("[general]\nname = x\nmachine = qemuarm64\n", error);
    EXPECT_TRUE(error.empty());
    EXPECT_EQ(cfg.machine, "qemuarm64");
    EXPECT_EQ(cfg.project.layer_name, ProjectSpec{}.layer_name);
    EXPECT_TRUE(cfg.project.apps.empty());
}

TEST(ProjectSpec, CollectionStripsMetaPrefix) {
    ProjectSpec p;
    p.layer_name = "meta-formation";
    EXPECT_EQ(p.collection(), "formation");
    EXPECT_EQ(p.rootfs_recipe_name(), "formation-rootfs-files");
    p.layer_name = "monlayer";
    EXPECT_EQ(p.collection(), "monlayer");
}

TEST(RecipeName, FollowsBitBakeRules) {
    EXPECT_TRUE(is_valid_recipe_name("formation-apps"));
    EXPECT_TRUE(is_valid_recipe_name("gtk+3"));
    EXPECT_TRUE(is_valid_recipe_name("libfoo2.0"));
    EXPECT_FALSE(is_valid_recipe_name(""));
    EXPECT_FALSE(is_valid_recipe_name("Majuscule"));
    EXPECT_FALSE(is_valid_recipe_name("sous_tiret"));
    EXPECT_FALSE(is_valid_recipe_name("-tiret"));
}

TEST(Validate, PresetHasNoErrors) {
    const auto issues = validate(formation_preset(), "scarthgap");
    for (const auto& i : issues) {
        EXPECT_NE(i.level, Issue::Level::Error) << i.where << " : " << i.message;
    }
}

TEST(Validate, DetectsTypicalMistakes) {
    ProjectSpec p = formation_preset();
    p.apps[0].name = "Bad_Name";
    p.apps[1].files[0].dest = "usr/bin/relatif";
    p.apps[1].files[1].mode = "999";
    p.apps.push_back(p.apps[2]);                  // doublon ntpd-formation
    p.rootfs_files[0].owner_recipe.clear();       // /etc/motd sans base-files
    p.patches[0].search.clear();
    CustomApp gpl = app_template(AppKind::CFiles, "gpl-app");
    gpl.license = "GPL-2.0-only";
    p.apps.push_back(gpl);

    const auto issues = validate(p, "scarthgap");
    EXPECT_TRUE(has_errors(issues));
    EXPECT_TRUE(has_issue(issues, Issue::Level::Error, "nom de recette invalide"));
    EXPECT_TRUE(has_issue(issues, Issue::Level::Error, "destination invalide"));
    EXPECT_TRUE(has_issue(issues, Issue::Level::Error, "mode invalide"));
    EXPECT_TRUE(has_issue(issues, Issue::Level::Error, "déjà utilisé"));
    EXPECT_TRUE(has_issue(issues, Issue::Level::Warning, "base-files"));
    EXPECT_TRUE(has_issue(issues, Issue::Level::Error, "texte à rechercher vide"));
    EXPECT_TRUE(has_issue(issues, Issue::Level::Error, "LIC_FILES_CHKSUM"));
}

TEST(Validate, MakeAppWithoutMakefileIsAnError) {
    ProjectSpec p;
    CustomApp a = app_template(AppKind::Make, "monappli");
    a.files.erase(a.files.begin());  // retire le Makefile
    p.apps.push_back(a);
    EXPECT_TRUE(has_issue(validate(p, "scarthgap"), Issue::Level::Error, "aucun fichier Makefile"));
}

TEST(Validate, DisabledItemsAreIgnored) {
    ProjectSpec p;
    CustomApp a;
    a.name = "INVALIDE";
    a.enabled = false;
    p.apps.push_back(a);
    EXPECT_FALSE(has_errors(validate(p, "scarthgap")));
}

TEST(SuggestedOwner, KnowsCommonPokyFiles) {
    EXPECT_EQ(suggested_owner_recipe("/etc/motd"), "base-files");
    EXPECT_EQ(suggested_owner_recipe("/etc/network/interfaces"), "init-ifupdown");
    EXPECT_EQ(suggested_owner_recipe("/etc/mon-appli.conf"), "");
}
