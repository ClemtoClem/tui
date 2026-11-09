// panels/TerminalPanel.hpp
#pragma once

#include <tui/widget/layout/Border.hpp>
#include <tui/widgets/Terminal.hpp>

namespace yocto {

class TerminalPanel {
public:
    TerminalPanel();
    [[nodiscard]] std::shared_ptr<tui::Widget> root() const { return panel_; }
    [[nodiscard]] std::shared_ptr<tui::Terminal> terminal() const { return terminal_; }

private:
    std::shared_ptr<tui::Border> panel_;
    std::shared_ptr<tui::Terminal> terminal_;
};

} // namespace yocto