#include <gtest/gtest.h>

#include "config/YoctoConfig.hpp"
#include "project/PlanApplier.hpp"
#include "project/Presets.hpp"

#include <filesystem>
#include <fstream>
#include <unistd.h>

using namespace yocto;
namespace fs = std::filesystem;

namespace {

std::string fake_hasher(const std::string& pw, const std::string& salt) {
    return "$6$" + salt + "$" + pw;
}

/// Lab jetable sous le répertoire temporaire du système.
class PlanApplierTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() / ("yocto-tui-test-" + std::to_string(::getpid()) + "-" +
                                             ::testing::UnitTest::GetInstance()->current_test_info()->name());
        fs::remove_all(root_);
        fs::create_directories(root_);
        cfg_.name = "test";
        cfg_.workdir = root_.string();
        cfg_.poky_branch = "scarthgap";
        cfg_.project = formation_preset();
        cfg_.apply_defaults();
    }
    void TearDown() override { fs::remove_all(root_); }

    GenerationPlan plan() const { return LayerGenerator(cfg_, fake_hasher).generate(); }

    static void write(const fs::path& p, const std::string& s) {
        fs::create_directories(p.parent_path());
        std::ofstream(p) << s;
    }

    fs::path root_;
    YoctoConfig cfg_;
};

int count(const std::vector<PreviewEntry>& v, EntryStatus s) {
    return static_cast<int>(std::count_if(v.begin(), v.end(), [s](const PreviewEntry& e) { return e.status == s; }));
}

} // namespace

TEST_F(PlanApplierTest, DryRunWritesNothing) {
    std::vector<std::string> log;
    const auto rep = apply_layer(plan(), true, [&](const std::string& l) { log.push_back(l); });
    EXPECT_TRUE(rep.ok());
    EXPECT_GT(rep.written, 10);
    EXPECT_FALSE(fs::exists(root_ / "layers"));
    ASSERT_FALSE(log.empty());
    EXPECT_NE(log.front().find("[simulation]"), std::string::npos);
}

TEST_F(PlanApplierTest, ApplyThenPreviewIsUnchanged) {
    const auto p = plan();
    const auto rep = apply_layer(p, false, nullptr);
    ASSERT_TRUE(rep.ok()) << rep.errors.front();
    EXPECT_TRUE(fs::exists(p.layer_dir / "conf" / "layer.conf"));
    EXPECT_TRUE(fs::exists(p.layer_dir / kManifestName));

    // Le mode d'exécution des scripts est appliqué.
    const auto perms = fs::status(p.layer_dir / "recipes-apps/formation-scripts/files/formation-scripts/bonjour.sh").permissions();
    EXPECT_NE(perms & fs::perms::owner_exec, fs::perms::none);

    const auto again = preview_layer(plan());
    EXPECT_EQ(count(again, EntryStatus::New), 0);
    EXPECT_EQ(count(again, EntryStatus::Modified), 0);
    EXPECT_EQ(count(again, EntryStatus::Unchanged), static_cast<int>(again.size()));
}

TEST_F(PlanApplierTest, RemovedItemsAreCleanedButManualFilesKept) {
    apply_layer(plan(), false, nullptr);
    const fs::path layer = root_ / "layers" / "meta-formation";
    // Fichier ajouté à la main (ou par devtool finish) : jamais touché.
    write(layer / "recipes-extended/bc/bc_%.bbappend", "# devtool\n");

    cfg_.project.apps.erase(cfg_.project.apps.begin());  // retire formation-scripts
    const auto entries = preview_layer(plan());
    EXPECT_GE(count(entries, EntryStatus::Removed), 3);  // recette + 2 sources

    const auto rep = apply_layer(plan(), false, nullptr);
    ASSERT_TRUE(rep.ok());
    EXPECT_FALSE(fs::exists(layer / "recipes-apps/formation-scripts"));  // répertoires vides élagués
    EXPECT_TRUE(fs::exists(layer / "recipes-extended/bc/bc_%.bbappend"));
}

TEST_F(PlanApplierTest, ModifiedContentIsDetected) {
    apply_layer(plan(), false, nullptr);
    cfg_.project.image.extra_packages = "strace";
    const auto entries = preview_layer(plan());
    ASSERT_EQ(count(entries, EntryStatus::Modified), 1);
    for (const auto& e : entries) {
        if (e.status != EntryStatus::Modified) continue;
        EXPECT_EQ(e.display, "recipes-core/images/formation-image.bb");
        const std::string d = format_diff(line_diff(e.old_content, e.new_content));
        EXPECT_NE(d.find("- IMAGE_INSTALL:append = \" strace lsof bc"), std::string::npos);
        EXPECT_NE(d.find("+ IMAGE_INSTALL:append = \" strace formation-scripts"), std::string::npos);
    }
}

TEST_F(PlanApplierTest, HostDirIsCopied) {
    const fs::path src = root_ / "mes-sources";
    write(src / "Makefile", "all:\n");
    write(src / "src" / "main.c", "int main(void){return 0;}\n");
    write(src / ".git" / "HEAD", "ref: refs/heads/main\n");
    CustomApp a = app_template(AppKind::Make, "mon-projet");
    a.source = SourceKind::HostDir;
    a.source_dir = src.string();
    a.files.clear();
    cfg_.project.apps.push_back(a);

    const auto rep = apply_layer(plan(), false, nullptr);
    ASSERT_TRUE(rep.ok());
    const fs::path dst = root_ / "layers/meta-formation/recipes-apps/mon-projet/files/mon-projet";
    EXPECT_TRUE(fs::exists(dst / "Makefile"));
    EXPECT_TRUE(fs::exists(dst / "src" / "main.c"));
    EXPECT_FALSE(fs::exists(dst / ".git"));
}

TEST_F(PlanApplierTest, BuildConfRequiresInitialisedBuild) {
    const auto entries = preview_build_conf(plan());
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0].status, EntryStatus::Missing);
    EXPECT_FALSE(apply_build_conf(plan(), false, nullptr).ok());
}

TEST_F(PlanApplierTest, BuildConfBlocksAreMergedOnce) {
    const fs::path conf = root_ / "builds" / "build-qemux86-64" / "conf";
    write(conf / "local.conf", "MACHINE ??= \"qemux86-64\"\n");
    write(conf / "bblayers.conf", "BBLAYERS ?= \" \\\n  " + (root_ / "layers/poky/meta").string() + " \\\n  \"\n");

    ASSERT_TRUE(apply_build_conf(plan(), false, nullptr).ok());
    ASSERT_TRUE(apply_build_conf(plan(), false, nullptr).ok());

    const std::string local = read_file(conf / "local.conf");
    EXPECT_EQ(local.rfind("MACHINE ??= \"qemux86-64\"\n", 0), 0u);  // contenu utilisateur intact
    EXPECT_NE(local.find("hostname:pn-base-files = \"formation-qemu\""), std::string::npos);
    EXPECT_EQ(local.find(kLocalConfBegin), local.rfind(kLocalConfBegin));  // un seul bloc

    const std::string layers = read_file(conf / "bblayers.conf");
    EXPECT_NE(layers.find((root_ / "layers/meta-formation").string()), std::string::npos);
    EXPECT_NE(layers.find((root_ / "layers/meta-openembedded/meta-oe").string()), std::string::npos);

    const auto entries = preview_build_conf(plan());
    EXPECT_EQ(count(entries, EntryStatus::Unchanged), 2);
}

TEST(LineDiff, BasicEdits) {
    const auto d = line_diff("a\nb\nc\n", "a\nB\nc\nd\n");
    ASSERT_EQ(d.size(), 5u);
    EXPECT_EQ(d[0].op, ' ');
    EXPECT_EQ(d[1].op, '-');
    EXPECT_EQ(d[1].text, "b");
    EXPECT_EQ(d[2].op, '+');
    EXPECT_EQ(d[2].text, "B");
    EXPECT_EQ(d[3].op, ' ');
    EXPECT_EQ(d[4].op, '+');
    EXPECT_EQ(format_diff(line_diff("x\n", "x\n")), "(aucune différence)\n");
}

TEST(LineDiff, ContextIsTrimmed) {
    std::string a;
    for (int i = 0; i < 50; ++i) a += "ligne " + std::to_string(i) + "\n";
    std::string b = a;
    b.replace(b.find("ligne 25"), 8, "LIGNE 25");
    const std::string out = format_diff(line_diff(a, b), 2);
    EXPECT_NE(out.find("- ligne 25"), std::string::npos);
    EXPECT_NE(out.find("+ LIGNE 25"), std::string::npos);
    EXPECT_EQ(out.find("ligne 10"), std::string::npos);
    EXPECT_NE(out.find("..."), std::string::npos);
}
