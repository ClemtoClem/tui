// panels/ConfigPanel.hpp
#pragma once

#include "../util/Workspace.hpp"
#include <tui/widget/layout/Border.hpp>
#include <tui/widgets/Input.hpp>
#include <tui/widgets/Label.hpp>
#include <tui/widgets/Button.hpp>
#include <tui/widgets/Checkbox.hpp>

namespace yocto {

class ConfigPanel {
public:
    explicit ConfigPanel(const WorkspaceInfo& ws);

    [[nodiscard]] std::shared_ptr<tui::Widget> root() const { return panel_; }
    void set_on_saved(std::function<void()> cb) { on_saved_ = std::move(cb); }

private:
    void save();
    WorkspaceInfo workspace_;
    std::shared_ptr<tui::Border> panel_;
    std::shared_ptr<tui::Input> distro_input_;
    std::shared_ptr<tui::Input> machine_input_;
    std::shared_ptr<tui::Input> image_input_;
    std::shared_ptr<tui::Checkbox> debug_check_;
    std::shared_ptr<tui::Button> save_button_;
    std::function<void()> on_saved_;
};

} // namespace yocto