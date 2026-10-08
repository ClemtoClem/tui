// panels/DashboardPanel.hpp
#pragma once

#include "../util/BitBake.hpp"
#include "../util/Workspace.hpp"
#include <tui/widget/layout/Border.hpp>
#include <tui/widget/layout/Vertical.hpp>
#include <tui/widgets/Label.hpp>
#include <tui/widgets/Button.hpp>
#include <tui/widgets/ListView.hpp>

namespace yocto {

class DashboardPanel {
public:
    explicit DashboardPanel(const WorkspaceInfo& ws);

    [[nodiscard]] std::shared_ptr<tui::Widget> root() const { return panel_; }
    void refresh();

    void set_on_build_requested(std::function<void()> cb) { on_build_ = std::move(cb); }
    void set_on_status(std::function<void(const std::string&)> cb) { on_status_ = std::move(cb); }

private:
    WorkspaceInfo workspace_;
    std::shared_ptr<tui::Border> panel_;
    std::shared_ptr<tui::Label> distro_label_;
    std::shared_ptr<tui::Label> machine_label_;
    std::shared_ptr<tui::Label> image_label_;
    std::shared_ptr<tui::ListView> recent_jobs_;
    std::shared_ptr<tui::Button> build_button_;
    std::function<void()> on_build_;
    std::function<void(const std::string&)> on_status_;
};

} // namespace yocto