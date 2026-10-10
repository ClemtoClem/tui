#include "tabs/FilesystemsTab.hpp"

#include <algorithm>

namespace sysmon {

FilesystemsTab::FilesystemsTab() {
    table_ = std::make_shared<DataTable>();
    table_->set_columns({
        {"Disponible", tui::LayoutParams::fixed(11)},
        {"Nom peripherique", tui::LayoutParams::stretch(2)},
        {"Repertoire racine", tui::LayoutParams::stretch(3)},
        {"Type", tui::LayoutParams::fixed(10)},
        {"Total", tui::LayoutParams::fixed(9)},
        {"Utilisation", tui::LayoutParams::fixed(20)},
    });
    table_->set_cell_text([this](int row, int col) { return cell_text(row, col); });
    table_->set_bar_value([this](int row, int col) { return bar_value(row, col); });
    table_->set_on_header_click([this](int col) { on_header_click(col); });
    table_->set_sort_indicator(sort_col_, sort_ascending_);
}

void FilesystemsTab::refresh() {
    mounts_ = read_mounts();
    resort();
    table_->set_row_count(static_cast<int>(mounts_.size()));
}

void FilesystemsTab::on_header_click(int col) {
    if (col == 5) return; // colonne barre : pas de tri (pas de valeur textuelle sensee)
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

void FilesystemsTab::resort() {
    order_.resize(mounts_.size());
    for (size_t i = 0; i < order_.size(); ++i) order_[i] = static_cast<int>(i);

    auto less = [&](int a, int b) -> bool {
        const MountInfo& ma = mounts_[static_cast<size_t>(a)];
        const MountInfo& mb = mounts_[static_cast<size_t>(b)];
        switch (sort_col_) {
            case 0: return ma.available < mb.available;
            case 1: return ma.device < mb.device;
            case 2: return ma.mount_point < mb.mount_point;
            case 3: return ma.fs_type < mb.fs_type;
            case 4: return ma.total < mb.total;
            default: return false;
        }
    };
    std::stable_sort(order_.begin(), order_.end(), less);
    if (!sort_ascending_) std::reverse(order_.begin(), order_.end());
}

std::string FilesystemsTab::cell_text(int row, int col) const {
    if (row < 0 || row >= static_cast<int>(order_.size())) return "";
    const MountInfo& m = mounts_[static_cast<size_t>(order_[static_cast<size_t>(row)])];
    switch (col) {
        case 0: return human_bytes(static_cast<double>(m.available));
        case 1: return m.device;
        case 2: return m.mount_point;
        case 3: return m.fs_type;
        case 4: return human_go(m.total);
        default: return "";
    }
}

std::optional<double> FilesystemsTab::bar_value(int row, int col) const {
    if (col != 5 || row < 0 || row >= static_cast<int>(order_.size())) return std::nullopt;
    return mounts_[static_cast<size_t>(order_[static_cast<size_t>(row)])].used_fraction;
}

} // namespace sysmon
