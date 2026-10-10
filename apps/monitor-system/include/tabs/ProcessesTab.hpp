#pragma once

#include "sysinfo/Sysinfo.hpp"
#include "widgets/DataTable.hpp"

#include <tui/widget/Widget.hpp>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sysmon {

class ProcessesTab {
public:
    ProcessesTab();

    [[nodiscard]] std::shared_ptr<tui::Widget> root() const { return table_; }
    [[nodiscard]] std::shared_ptr<DataTable> table() const { return table_; }

    void refresh(double dt);

private:
    void resort();
    void on_header_click(int col);
    [[nodiscard]] std::string cell_text(int row, int col) const;
    [[nodiscard]] std::optional<tui::Color> cell_color(int row, int col) const;

    std::shared_ptr<DataTable> table_;
    ProcState state_;
    std::vector<ProcessInfo> processes_;
    std::vector<int> order_;
    int sort_col_ = 2; // %CPU
    bool sort_ascending_ = false;
};

} // namespace sysmon
