/* apps/yocto-tui/panels/BuildPanel.hpp */
#pragma once

#include "../runner/BuildRunner.hpp"
#include "../config/YoctoConfig.hpp"

#include <tui/App.hpp>
#include <tui/widget/Widget.hpp>
#include <tui/widget/layout/Border.hpp>
#include <tui/widgets/Button.hpp>
#include <tui/widgets/Input.hpp>
#include <tui/widgets/Label.hpp>
#include <tui/widgets/ProgressBar.hpp>
#include <tui/widgets/TextArea.hpp>

#include <chrono>
#include <functional>
#include <memory>
#include <string>

namespace yocto {

/// Onglet « Build » : lance bitbake via BuildRunner (processus
/// séparé, annulable), affiche la progression des tâches, la tâche
/// courante, le temps écoulé et le journal complet.
class BuildPanel {
public:
    BuildPanel(tui::App& app, YoctoConfig config);

    [[nodiscard]] std::shared_ptr<tui::Widget> root() const { return panel_; }

    /// Appelé par main quand la configuration active change.
    void set_config(const YoctoConfig& cfg);

    /// Appelé périodiquement par le timer de main : met à jour le
    /// temps écoulé pendant un build.
    void tick();

    void set_on_status(std::function<void(const std::string&)> cb) {
        on_status_ = std::move(cb);
    }

private:
    void build_ui();
    void start_build();
    void cancel_build();

    void on_output_line(const std::string& line);
    void append_log(const std::string& line);
    void update_progress();
    void reset_progress();

    tui::App& app_;
    YoctoConfig config_;
    BuildRunner runner_;

    std::shared_ptr<tui::Border>     panel_;
    std::shared_ptr<tui::Input>     target_input_;
    std::shared_ptr<tui::Button>    start_button_;
    std::shared_ptr<tui::Button>    cancel_button_;
    std::shared_ptr<tui::ProgressBar> progress_;
    std::shared_ptr<tui::Label>     progress_label_;
    std::shared_ptr<tui::Label>     elapsed_label_;
    std::shared_ptr<tui::Label>     task_label_;
    std::shared_ptr<tui::Label>     status_label_;
    std::shared_ptr<tui::TextArea>  log_view_;

    int total_tasks_ = 0;
    int completed_tasks_ = 0;
    bool building_ = false;
    bool cancelled_ = false;
    std::chrono::steady_clock::time_point start_time_;
    std::string log_buffer_;

    std::function<void(const std::string&)> on_status_;
};

} // namespace yocto