// panels/TerminalPanel.cpp
#include "TerminalPanel.hpp"
#include "../Theme.hpp"

namespace yocto {

TerminalPanel::TerminalPanel() {
    terminal_ = std::make_shared<tui::Terminal>();

    panel_ = std::make_shared<tui::Border>();
    panel_->set_border_style(tui::BorderStyle::Rounded);
    panel_->set_color(kBorder);
    panel_->set_title("Terminal (devshell / commandes)");
    panel_->set_child(terminal_);
}

} // namespace yocto