/* apps/yocto-tui/panels/ApplyPanel.hpp */
#pragma once

#include "../config/YoctoConfig.hpp"
#include "../project/PlanApplier.hpp"
#include "../runners/ProjectRunner.hpp"

#include <tui/App.hpp>
#include <tui/widget/Widget.hpp>
#include <tui/widgets/Button.hpp>
#include <tui/widgets/Checkbox.hpp>
#include <tui/widgets/Label.hpp>
#include <tui/widgets/ListView.hpp>
#include <tui/widgets/TextArea.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace yocto {

/// Onglet « Générer & appliquer » : aperçu de tout ce que le projet
/// produit (fichiers nouveaux / modifiés / supprimés, diff ligne à
/// ligne, problèmes de validation), puis exécution des étapes du
/// ProjectRunner, en simulation ou pour de vrai.
class ApplyPanel {
public:
    ApplyPanel(tui::App& app, YoctoConfig config);

    [[nodiscard]] std::shared_ptr<tui::Widget> root() const { return root_; }

    void set_config(const YoctoConfig& cfg);
    /// Recalcule le plan et l'aperçu (lit le disque, n'écrit rien).
    void refresh_preview();

    void set_on_status(std::function<void(const std::string&)> cb) { on_status_ = std::move(cb); }
    /// Demande un build bitbake (onglet Build) à la fin d'une application réussie.
    void set_on_request_build(std::function<void(const std::string&)> cb) { on_request_build_ = std::move(cb); }

private:
    void build_ui();
    void show_entry(size_t index);
    void run(bool all_selected);
    void append_log(const std::string& line);
    void set_step_status(ProjectStepId id, StepStatus s, const std::string& msg);
    [[nodiscard]] std::vector<ProjectStepId> checked_steps() const;
    void status(const std::string& msg) { if (on_status_) on_status_(msg); }

    tui::App& app_;
    YoctoConfig config_;
    std::vector<PreviewEntry> entries_;
    std::vector<Issue> issues_;
    bool diff_mode_ = true;
    std::vector<char> view_kinds_;  // premier caractère de chaque ligne affichée (coloration)

    std::unique_ptr<ProjectRunner> runner_;
    bool running_ = false;
    bool run_failed_ = false;
    std::string log_buffer_;

    std::shared_ptr<tui::Widget> root_;
    std::shared_ptr<tui::ListView> issues_list_;
    std::shared_ptr<tui::Checkbox> dry_run_;
    std::vector<std::pair<ProjectStepId, std::shared_ptr<tui::Checkbox>>> step_checks_;
    std::shared_ptr<tui::Checkbox> build_after_;
    std::shared_ptr<tui::ListView> steps_list_;
    std::shared_ptr<tui::Label> summary_;
    std::shared_ptr<tui::ListView> entries_list_;
    std::shared_ptr<tui::Button> mode_button_;
    std::shared_ptr<tui::Label> view_title_;
    std::shared_ptr<tui::TextArea> view_;
    std::shared_ptr<tui::TextArea> log_;

    std::function<void(const std::string&)> on_status_;
    std::function<void(const std::string&)> on_request_build_;
};

} // namespace yocto
