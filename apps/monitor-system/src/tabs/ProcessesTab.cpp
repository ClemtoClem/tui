#include "tabs/ProcessesTab.hpp"

#include <algorithm>
#include <cstdio>

namespace sysmon {

namespace {
std::string format_percent(double v) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.1f", v);
    return buf;
}
} // namespace

ProcessesTab::ProcessesTab() {
    table_ = std::make_shared<DataTable>();
    table_->set_columns({
        {"Nom", tui::LayoutParams::stretch(3)},
        {"Utilisateur", tui::LayoutParams::fixed(11)},
        {"%CPU", tui::LayoutParams::fixed(7)},
        {"ID", tui::LayoutParams::fixed(7)},
        {"Memoire", tui::LayoutParams::fixed(9)},
        {"Total lecture disque", tui::LayoutParams::fixed(14)},
        {"Total ecriture disque", tui::LayoutParams::fixed(14)},
        {"Lecture disque", tui::LayoutParams::fixed(11)},
        {"Ecriture disque", tui::LayoutParams::fixed(11)},
        {"Priorite", tui::LayoutParams::fixed(9)},
    });
    table_->set_cell_text([this](int row, int col) { return cell_text(row, col); });
    table_->set_cell_color([this](int row, int col) { return cell_color(row, col); });
    table_->set_on_header_click([this](int col) { on_header_click(col); });
    table_->set_sort_indicator(sort_col_, sort_ascending_);
}

void ProcessesTab::refresh(double dt) {
    processes_ = read_processes(state_, dt);
    resort();
    table_->set_row_count(static_cast<int>(processes_.size()));
}

void ProcessesTab::on_header_click(int col) {
    if (col == sort_col_) {
        sort_ascending_ = !sort_ascending_;
    } else {
        sort_col_ = col;
        sort_ascending_ = true;
    }
    resort();
    table_->set_sort_indicator(sort_col_, sort_ascending_);
    table_->need_repaint();
}

void ProcessesTab::resort() {
    order_.resize(processes_.size());
    for (size_t i = 0; i < order_.size(); ++i) order_[i] = static_cast<int>(i);

    auto less = [&](int a, int b) -> bool {
        const ProcessInfo& pa = processes_[static_cast<size_t>(a)];
        const ProcessInfo& pb = processes_[static_cast<size_t>(b)];
        switch (sort_col_) {
            case 0: return pa.name < pb.name;
            case 1: return pa.user < pb.user;
            case 2: return pa.cpu_percent < pb.cpu_percent;
            case 3: return pa.pid < pb.pid;
            case 4: return pa.rss_bytes < pb.rss_bytes;
            case 5: return pa.disk_read_total < pb.disk_read_total;
            case 6: return pa.disk_write_total < pb.disk_write_total;
            case 7: return pa.disk_read_rate < pb.disk_read_rate;
            case 8: return pa.disk_write_rate < pb.disk_write_rate;
            case 9: return pa.nice < pb.nice;
            default: return false;
        }
    };
    std::stable_sort(order_.begin(), order_.end(), less);
    if (!sort_ascending_) std::reverse(order_.begin(), order_.end());
}

std::string ProcessesTab::cell_text(int row, int col) const {
    if (row < 0 || row >= static_cast<int>(order_.size())) return "";
    const ProcessInfo& p = processes_[static_cast<size_t>(order_[static_cast<size_t>(row)])];
    switch (col) {
        case 0: return p.name;
        case 1: return p.user;
        case 2: return format_percent(p.cpu_percent);
        case 3: return std::to_string(p.pid);
        case 4: return human_bytes(static_cast<double>(p.rss_bytes));
        case 5: return human_bytes(static_cast<double>(p.disk_read_total));
        case 6: return human_bytes(static_cast<double>(p.disk_write_total));
        case 7: return human_rate(p.disk_read_rate);
        case 8: return human_rate(p.disk_write_rate);
        case 9: return std::to_string(p.nice);
        default: return "";
    }
}

std::optional<tui::Color> ProcessesTab::cell_color(int row, int col) const {
    if (col != 2 || row < 0 || row >= static_cast<int>(order_.size())) return std::nullopt;
    double cpu = processes_[static_cast<size_t>(order_[static_cast<size_t>(row)])].cpu_percent;
    if (cpu > 75.0) return tui::Color::Red();
    if (cpu > 40.0) return tui::Color{230, 160, 30};
    return std::nullopt;
}

} // namespace sysmon
