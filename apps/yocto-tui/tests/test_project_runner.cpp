#include <gtest/gtest.h>

#include "project/PlanApplier.hpp"
#include "project/Presets.hpp"
#include "runners/ProjectRunner.hpp"

#include <filesystem>
#include <fstream>
#include <map>
#include <unistd.h>

using namespace yocto;
namespace fs = std::filesystem;

namespace {

fs::path make_tmp(const std::string& tag) {
    const fs::path p = fs::temp_directory_path() / ("yocto-tui-runner-" + std::to_string(::getpid()) + "-" + tag);
    fs::remove_all(p);
    fs::create_directories(p);
    return p;
}

void write(const fs::path& p, const std::string& s) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << s;
}

} // namespace

TEST(ReplaceInString, FirstOrAll) {
    std::string s = "a-b-a-b";
    EXPECT_EQ(replace_in_string(s, "a", "x", false), 1u);
    EXPECT_EQ(s, "x-b-a-b");
    EXPECT_EQ(replace_in_string(s, "b", "bb", true), 2u);
    EXPECT_EQ(s, "x-bb-a-bb");
    EXPECT_EQ(replace_in_string(s, "", "z", true), 0u);
    EXPECT_EQ(replace_in_string(s, "absent", "z", true), 0u);
}

TEST(FindFileContaining, SkipsGitAndBinaries) {
    const fs::path root = make_tmp("find");
    write(root / ".git" / "objects" / "x", "This is free software");
    write(root / "doc.bin", std::string("This is free software\0", 22));
    write(root / "bc" / "main.c", "puts(\"This is free software with ABSOLUTELY NO WARRANTY\");\n");
    write(root / "z" / "other.c", "This is free software\n");
    EXPECT_EQ(find_file_containing(root, "ABSOLUTELY NO WARRANTY"), root / "bc" / "main.c");
    // Plusieurs correspondances : la première par ordre lexicographique.
    EXPECT_EQ(find_file_containing(root, "This is free software"), root / "bc" / "main.c");
    EXPECT_TRUE(find_file_containing(root, "introuvable").empty());
    fs::remove_all(root);
}

TEST(ProjectRunner, DryRunTouchesNothingAndRunsAllSteps) {
    const fs::path root = make_tmp("dry");
    YoctoConfig cfg;
    cfg.workdir = root.string();
    cfg.project = formation_preset();
    cfg.apply_defaults();

    ProjectRunner runner(cfg, /*dry_run=*/true, [](const std::string& pw, const std::string& salt) {
        return "$6$" + salt + "$" + pw;
    });
    std::vector<std::string> log;
    std::map<ProjectStepId, StepStatus> final_status;
    runner.set_on_output([&](const std::string& l) { log.push_back(l); });
    runner.set_on_status([&](ProjectStepId id, StepStatus s, const std::string&) { final_status[id] = s; });

    runner.run({ProjectStepId::FetchLayers, ProjectStepId::WriteLayer, ProjectStepId::ConfigureBuild,
                ProjectStepId::CheckLayers, ProjectStepId::ApplyPatches});

    for (const auto& step : runner.steps()) {
        EXPECT_EQ(final_status[step.id], StepStatus::Done) << step.label;
    }
    // Rien n'a été créé dans le lab.
    EXPECT_TRUE(fs::is_empty(root));

    auto logged = [&](const std::string& needle) {
        return std::any_of(log.begin(), log.end(), [&](const std::string& l) { return l.find(needle) != std::string::npos; });
    };
    EXPECT_TRUE(logged("git clone -b 'scarthgap' 'https://git.openembedded.org/meta-openembedded'"));
    EXPECT_TRUE(logged("devtool modify 'bc'"));
    EXPECT_TRUE(logged("devtool finish 'bc'"));
    EXPECT_TRUE(logged("bitbake-layers show-layers"));
    EXPECT_TRUE(logged("formation-image.bb"));
    fs::remove_all(root);
}

TEST(ProjectRunner, InvalidProjectStopsBeforeWriting) {
    const fs::path root = make_tmp("invalid");
    YoctoConfig cfg;
    cfg.workdir = root.string();
    cfg.project = formation_preset();
    cfg.project.apps[0].name = "Invalide";
    cfg.apply_defaults();

    ProjectRunner runner(cfg, false, [](const std::string&, const std::string&) { return std::string("$6$x$y"); });
    std::map<ProjectStepId, StepStatus> st;
    runner.set_on_status([&](ProjectStepId id, StepStatus s, const std::string&) { st[id] = s; });
    runner.run({ProjectStepId::WriteLayer, ProjectStepId::ConfigureBuild});

    EXPECT_EQ(st[ProjectStepId::WriteLayer], StepStatus::Failed);
    EXPECT_EQ(st[ProjectStepId::ConfigureBuild], StepStatus::Skipped);
    EXPECT_FALSE(fs::exists(root / "layers"));
    fs::remove_all(root);
}
