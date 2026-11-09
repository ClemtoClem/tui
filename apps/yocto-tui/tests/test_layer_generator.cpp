#include <gtest/gtest.h>

#include "config/YoctoConfig.hpp"
#include "project/LayerGenerator.hpp"
#include "project/Presets.hpp"

#include <algorithm>

using namespace yocto;

namespace {

/// Hasheur factice : déterministe, sans dépendre d'openssl.
std::string fake_hasher(const std::string& pw, const std::string& salt) {
    return "$6$" + salt + "$hash-de-" + pw;
}

YoctoConfig make_config(const std::string& branch = "scarthgap") {
    YoctoConfig cfg;
    cfg.name = "test";
    cfg.workdir = "/lab";
    cfg.poky_branch = branch;
    cfg.machine = "qemux86-64";
    cfg.project = formation_preset("formation");
    cfg.apply_defaults();
    return cfg;
}

const PlannedFile* find_file(const GenerationPlan& plan, const std::string& suffix) {
    for (const auto& f : plan.files) {
        const std::string p = f.path.string();
        if (p.size() >= suffix.size() && p.compare(p.size() - suffix.size(), suffix.size(), suffix) == 0) {
            return &f;
        }
    }
    return nullptr;
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

} // namespace

TEST(LayerGeneratorHelpers, UnpackVarDependsOnRelease) {
    EXPECT_EQ(LayerGenerator::unpack_var("scarthgap"), "${WORKDIR}");
    EXPECT_EQ(LayerGenerator::unpack_var("kirkstone"), "${WORKDIR}");
    EXPECT_EQ(LayerGenerator::unpack_var("styhead"), "${UNPACKDIR}");
    EXPECT_EQ(LayerGenerator::unpack_var("walnascar"), "${UNPACKDIR}");
}

TEST(LayerGeneratorHelpers, InstallPathVar) {
    EXPECT_EQ(LayerGenerator::install_path_var("/usr/bin/hello"), "${bindir}/hello");
    EXPECT_EQ(LayerGenerator::install_path_var("/usr/lib/libx.so"), "${libdir}/libx.so");
    EXPECT_EQ(LayerGenerator::install_path_var("/etc/init.d/svc"), "${sysconfdir}/init.d/svc");
    EXPECT_EQ(LayerGenerator::install_path_var("/etc"), "${sysconfdir}");
    EXPECT_EQ(LayerGenerator::install_path_var("/bin/sh"), "${base_bindir}/sh");
    EXPECT_EQ(LayerGenerator::install_path_var("/opt/app/x"), "/opt/app/x");
    // /usr/binaire ne doit pas être pris pour /usr/bin.
    EXPECT_EQ(LayerGenerator::install_path_var("/usr/binaire"), "/usr/binaire");
}

TEST(LayerGeneratorHelpers, GitSrcUri) {
    EXPECT_EQ(LayerGenerator::git_src_uri("https://github.com/a/b.git", "dev"),
              "git://github.com/a/b.git;protocol=https;branch=dev;destsuffix=git");
    EXPECT_EQ(LayerGenerator::git_src_uri("git@github.com:a/b.git", "main"),
              "git://git@github.com/a/b.git;protocol=ssh;branch=main;destsuffix=git");
}

TEST(LayerGeneratorHelpers, EscapeHashAndSalt) {
    EXPECT_EQ(LayerGenerator::escape_hash("$6$abc$def"), "\\$6\\$abc\\$def");
    const std::string s1 = derive_salt("x");
    EXPECT_EQ(s1.size(), 16u);
    EXPECT_EQ(s1, derive_salt("x"));   // déterministe
    EXPECT_NE(s1, derive_salt("y"));
}

TEST(LayerGenerator, LayerConf) {
    const auto plan = LayerGenerator(make_config(), fake_hasher).generate();
    EXPECT_EQ(plan.layer_dir, "/lab/layers/meta-formation");
    const auto* f = find_file(plan, "meta-formation/conf/layer.conf");
    ASSERT_NE(f, nullptr);
    EXPECT_TRUE(contains(f->content, "BBFILE_COLLECTIONS += \"formation\""));
    EXPECT_TRUE(contains(f->content, "BBFILE_PRIORITY_formation = \"6\""));
    EXPECT_TRUE(contains(f->content, "LAYERDEPENDS_formation = \"core openembedded-layer\""));
    EXPECT_TRUE(contains(f->content, "LAYERSERIES_COMPAT_formation = \"scarthgap\""));
}

TEST(LayerGenerator, ScriptsRecipeInstallsWithAutoRdepends) {
    const auto plan = LayerGenerator(make_config(), fake_hasher).generate();
    const auto* r = find_file(plan, "recipes-apps/formation-scripts/formation-scripts_1.0.bb");
    ASSERT_NE(r, nullptr);
    EXPECT_TRUE(contains(r->content, "LIC_FILES_CHKSUM = \"file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302\""));
    EXPECT_TRUE(contains(r->content, "SRC_URI = \"file://formation-scripts/\""));
    EXPECT_TRUE(contains(r->content, "S = \"${WORKDIR}/formation-scripts\""));
    EXPECT_TRUE(contains(r->content, "install -m 0755 ${S}/bonjour.sh ${D}${bindir}/bonjour"));
    EXPECT_TRUE(contains(r->content, "install -m 0755 ${S}/info.py ${D}${bindir}/info-systeme"));
    // Shebang python3 -> dépendance d'exécution ajoutée automatiquement.
    EXPECT_TRUE(contains(r->content, "RDEPENDS:${PN} += \"python3-core\""));
    EXPECT_TRUE(contains(r->content, "do_compile[noexec] = \"1\""));

    const auto* src = find_file(plan, "recipes-apps/formation-scripts/files/formation-scripts/bonjour.sh");
    ASSERT_NE(src, nullptr);
    EXPECT_EQ(src->mode, 0755u);
    EXPECT_TRUE(contains(src->content, "Bonjour depuis"));
}

TEST(LayerGenerator, CFilesRecipeCompilesEachSource) {
    const auto plan = LayerGenerator(make_config(), fake_hasher).generate();
    const auto* r = find_file(plan, "formation-apps/formation-apps_1.0.bb");
    ASSERT_NE(r, nullptr);
    EXPECT_TRUE(contains(r->content, "B = \"${WORKDIR}/build\""));
    EXPECT_TRUE(contains(r->content, "${CC} ${CFLAGS} ${LDFLAGS} ${S}/hello.c -o ${B}/hello-yocto"));
    EXPECT_TRUE(contains(r->content, "${CC} ${CFLAGS} ${LDFLAGS} ${S}/main.c -o ${B}/appli-make-yocto"));
    EXPECT_TRUE(contains(r->content, "install -m 0755 ${B}/hello-yocto ${D}${bindir}/hello-yocto"));
    EXPECT_TRUE(contains(r->content, "FILES:${PN} += \"${bindir}/hello-yocto ${bindir}/appli-make-yocto\""));
}

TEST(LayerGenerator, MakeAndCMakeRecipes) {
    const auto plan = LayerGenerator(make_config(), fake_hasher).generate();
    const auto* mk = find_file(plan, "appli-make/appli-make_1.0.bb");
    ASSERT_NE(mk, nullptr);
    EXPECT_TRUE(contains(mk->content, "TARGET_CC_ARCH += \"${LDFLAGS}\""));
    EXPECT_TRUE(contains(mk->content, "oe_runmake install DESTDIR=${D}"));
    const auto* cm = find_file(plan, "appli-cmake/appli-cmake_1.0.bb");
    ASSERT_NE(cm, nullptr);
    EXPECT_TRUE(contains(cm->content, "inherit cmake"));
    // Le Makefile inline garde ses tabulations.
    const auto* makefile = find_file(plan, "files/appli-make/Makefile");
    ASSERT_NE(makefile, nullptr);
    EXPECT_TRUE(contains(makefile->content, "\n\t$(CC) $(LDFLAGS) -o $@ $^\n"));
}

TEST(LayerGenerator, GitAndHostDirSources) {
    auto cfg = make_config();
    CustomApp git = app_template(AppKind::CMake, "depuis-git");
    git.source = SourceKind::Git;
    git.git_url = "https://example.com/proj.git";
    git.git_srcrev = "0123456789abcdef0123456789abcdef01234567";
    git.files.clear();
    cfg.project.apps.push_back(git);
    CustomApp host = app_template(AppKind::Make, "depuis-hote");
    host.source = SourceKind::HostDir;
    host.source_dir = "/home/moi/projet";
    cfg.project.apps.push_back(host);

    const auto plan = LayerGenerator(cfg, fake_hasher).generate();
    const auto* g = find_file(plan, "depuis-git/depuis-git_1.0.bb");
    ASSERT_NE(g, nullptr);
    EXPECT_TRUE(contains(g->content, "SRC_URI = \"git://example.com/proj.git;protocol=https;branch=main;destsuffix=git\""));
    EXPECT_TRUE(contains(g->content, "SRCREV = \"0123456789abcdef0123456789abcdef01234567\""));
    EXPECT_TRUE(contains(g->content, "PV = \"1.0+git\""));
    EXPECT_TRUE(contains(g->content, "S = \"${WORKDIR}/git\""));

    // Sources hôte : copie planifiée, pas de fichier inline.
    EXPECT_EQ(find_file(plan, "files/depuis-hote/main.c"), nullptr);
    const bool copied = std::any_of(plan.copies.begin(), plan.copies.end(), [](const PlannedCopy& c) {
        return c.from_dir == "/home/moi/projet" &&
               c.to_dir == "/lab/layers/meta-formation/recipes-apps/depuis-hote/files/depuis-hote";
    });
    EXPECT_TRUE(copied);
}

TEST(LayerGenerator, NewReleasesUseUnpackdir) {
    const auto plan = LayerGenerator(make_config("styhead"), fake_hasher).generate();
    const auto* r = find_file(plan, "formation-scripts/formation-scripts_1.0.bb");
    ASSERT_NE(r, nullptr);
    EXPECT_TRUE(contains(r->content, "S = \"${UNPACKDIR}/formation-scripts\""));
    const auto* bf = find_file(plan, "recipes-core/base-files/base-files_%.bbappend");
    ASSERT_NE(bf, nullptr);
    EXPECT_TRUE(contains(bf->content, "${UNPACKDIR}/rootfs/etc/motd"));
}

TEST(LayerGenerator, OwnedRootfsFilesBecomeBbappends) {
    const auto plan = LayerGenerator(make_config(), fake_hasher).generate();
    const auto* bf = find_file(plan, "recipes-core/base-files/base-files_%.bbappend");
    ASSERT_NE(bf, nullptr);
    EXPECT_TRUE(contains(bf->content, "FILESEXTRAPATHS:prepend := \"${THISDIR}/files:\""));
    EXPECT_TRUE(contains(bf->content, "file://rootfs/etc/motd"));
    EXPECT_TRUE(contains(bf->content, "install -m 0644 ${WORKDIR}/rootfs/etc/motd ${D}${sysconfdir}/motd"));
    const auto* motd = find_file(plan, "recipes-core/base-files/files/rootfs/etc/motd");
    ASSERT_NE(motd, nullptr);
    EXPECT_EQ(motd->content.front(), '\n');

    const auto* ifup = find_file(plan, "recipes-core/init-ifupdown/init-ifupdown_%.bbappend");
    ASSERT_NE(ifup, nullptr);
    EXPECT_TRUE(contains(ifup->content, "${D}${sysconfdir}/network/interfaces"));
}

TEST(LayerGenerator, FragmentsAndRootfsShareOneBbappendPerRecipe) {
    auto cfg = make_config();
    cfg.project.rootfs_files.push_back({"/etc/syslog.conf", "0644", "busybox", "*.* /var/log/messages\n", true});
    const auto plan = LayerGenerator(cfg, fake_hasher).generate();

    const auto count = std::count_if(plan.files.begin(), plan.files.end(), [](const PlannedFile& f) {
        return f.path.filename() == "busybox_%.bbappend";
    });
    EXPECT_EQ(count, 1);
    const auto* bb = find_file(plan, "recipes-core/busybox/busybox_%.bbappend");
    ASSERT_NE(bb, nullptr);
    EXPECT_TRUE(contains(bb->content, "file://ntpd.cfg"));
    EXPECT_TRUE(contains(bb->content, "file://rootfs/etc/syslog.conf"));
    ASSERT_NE(find_file(plan, "recipes-core/busybox/files/ntpd.cfg"), nullptr);
}

TEST(LayerGenerator, UnownedRootfsFilesGetDedicatedRecipeInImage) {
    auto cfg = make_config();
    cfg.project.rootfs_files.push_back({"/etc/mon-appli.conf", "0600", "", "cle=valeur\n", true});
    const auto plan = LayerGenerator(cfg, fake_hasher).generate();

    const auto* r = find_file(plan, "recipes-core/formation-rootfs-files/formation-rootfs-files_1.0.bb");
    ASSERT_NE(r, nullptr);
    EXPECT_TRUE(contains(r->content, "install -m 0600 ${S}/etc/mon-appli.conf ${D}${sysconfdir}/mon-appli.conf"));
    EXPECT_TRUE(contains(r->content, "CONFFILES:${PN} += \"${sysconfdir}/mon-appli.conf\""));
    const auto* img = find_file(plan, "recipes-core/images/formation-image.bb");
    ASSERT_NE(img, nullptr);
    EXPECT_TRUE(contains(img->content, "formation-rootfs-files"));
}

TEST(LayerGenerator, PatchFileGetsUpstreamStatus) {
    auto cfg = make_config();
    PatchSpec p;
    p.recipe = "bc";
    p.name = "0001-fix.patch";
    p.method = PatchMethod::PatchFile;
    p.patch_content = "--- a/x\n+++ b/x\n@@ -1 +1 @@\n-a\n+b\n";
    cfg.project.patches.push_back(p);
    const auto plan = LayerGenerator(cfg, fake_hasher).generate();
    const auto* f = find_file(plan, "recipes-extended/bc/files/0001-fix.patch");
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(f->content.rfind("Upstream-Status: Inappropriate", 0), 0u);
    const auto* app = find_file(plan, "recipes-extended/bc/bc_%.bbappend");
    ASSERT_NE(app, nullptr);
    EXPECT_TRUE(contains(app->content, "file://0001-fix.patch"));
}

TEST(LayerGenerator, ImageRecipe) {
    const auto plan = LayerGenerator(make_config(), fake_hasher).generate();
    const auto* img = find_file(plan, "recipes-core/images/formation-image.bb");
    ASSERT_NE(img, nullptr);
    const std::string& c = img->content;
    EXPECT_TRUE(contains(c, "require recipes-core/images/core-image-minimal.bb"));
    EXPECT_TRUE(contains(c, "IMAGE_INSTALL:append = \" strace lsof bc htop jq formation-scripts formation-apps ntpd-formation\""));
    // appli-make / appli-cmake : recettes disponibles, mais hors image.
    EXPECT_FALSE(contains(c, "appli-make "));
    EXPECT_TRUE(contains(c, "ssh-server-dropbear empty-root-password allow-empty-password allow-root-login"));
    EXPECT_TRUE(contains(c, "inherit extrausers"));
    EXPECT_TRUE(contains(c, "useradd -p '\\$6\\$"));
    EXPECT_TRUE(contains(c, "\\$hash-de-formation' stagiaire;"));
}

TEST(LayerGenerator, OldReleasesUseDebugTweaks) {
    const auto plan = LayerGenerator(make_config("kirkstone"), fake_hasher).generate();
    const auto* img = find_file(plan, "formation-image.bb");
    ASSERT_NE(img, nullptr);
    EXPECT_TRUE(contains(img->content, "debug-tweaks"));
    EXPECT_FALSE(contains(img->content, "empty-root-password"));
}

TEST(LayerGenerator, HasherFailureIsReportedAsError) {
    const auto plan = LayerGenerator(make_config(), [](const std::string&, const std::string&) {
        return std::string();
    }).generate();
    EXPECT_TRUE(has_errors(plan.issues));
}

TEST(LayerGenerator, DisabledAppIsNotGenerated) {
    auto cfg = make_config();
    cfg.project.apps[0].enabled = false;
    const auto plan = LayerGenerator(cfg, fake_hasher).generate();
    EXPECT_EQ(find_file(plan, "formation-scripts_1.0.bb"), nullptr);
    const auto* img = find_file(plan, "formation-image.bb");
    ASSERT_NE(img, nullptr);
    EXPECT_FALSE(contains(img->content, "formation-scripts"));
}

TEST(LayerGenerator, BuildConfEdits) {
    auto cfg = make_config();
    cfg.project.rm_work = true;
    cfg.project.local_conf_extra = "PACKAGE_CLASSES = \"package_ipk\"";
    const auto plan = LayerGenerator(cfg, fake_hasher).generate();
    ASSERT_EQ(plan.edits.size(), 2u);
    const auto& local = plan.edits[0];
    EXPECT_EQ(local.path, "/lab/builds/build-qemux86-64/conf/local.conf");
    EXPECT_TRUE(contains(local.block_body, "hostname:pn-base-files = \"formation-qemu\""));
    EXPECT_TRUE(contains(local.block_body, "INHERIT += \"rm_work\""));
    EXPECT_TRUE(contains(local.block_body, "PACKAGE_CLASSES = \"package_ipk\"\n"));
    const auto& layers = plan.edits[1];
    ASSERT_TRUE(layers.is_bblayers);
    EXPECT_EQ(layers.layer_paths, (std::vector<std::string>{
        "/lab/layers/meta-openembedded/meta-oe", "/lab/layers/meta-formation"}));
}

TEST(ManagedBlock, AppendReplaceRemoveIdempotent) {
    const std::string original = "MACHINE ?= \"qemux86-64\"\n";
    const std::string once = merge_managed_block(original, "#B", "#E", "A = \"1\"\n");
    EXPECT_EQ(once, "MACHINE ?= \"qemux86-64\"\n\n#B\nA = \"1\"\n#E\n");
    EXPECT_EQ(merge_managed_block(once, "#B", "#E", "A = \"1\"\n"), once);

    const std::string changed = merge_managed_block(once + "APRES = \"x\"\n", "#B", "#E", "A = \"2\"\n");
    EXPECT_EQ(changed, "MACHINE ?= \"qemux86-64\"\n\n#B\nA = \"2\"\n#E\nAPRES = \"x\"\n");

    const std::string removed = merge_managed_block(once, "#B", "#E", "");
    EXPECT_EQ(removed, "MACHINE ?= \"qemux86-64\"\n\n");
    EXPECT_EQ(merge_managed_block(original, "#B", "#E", ""), original);
}

TEST(ManagedBlock, BblayersSkipsLayersAlreadyDeclared) {
    const std::string existing =
        "BBLAYERS ?= \" \\\n"
        "  /lab/layers/poky/meta \\\n"
        "  /lab/layers/meta-openembedded/meta-oe \\\n"
        "  \"\n"
        "# /lab/layers/meta-formation (ancien commentaire)\n";
    ManagedEdit e;
    e.is_bblayers = true;
    e.layer_paths = {"/lab/layers/meta-openembedded/meta-oe", "/lab/layers/meta-formation"};
    const std::string out = render_managed_edit(e, existing);
    EXPECT_TRUE(contains(out, "BBLAYERS += \" \\\n  /lab/layers/meta-formation \\\n  \"\n"));
    // meta-oe n'est pas répété (déjà ajouté par bitbake-layers add-layer).
    EXPECT_EQ(out.find("meta-oe", out.find(kBblayersBegin)), std::string::npos);
    EXPECT_EQ(render_managed_edit(e, out), out);
}
