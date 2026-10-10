#pragma once

#include "sysinfo/Sysinfo.hpp"
#include "widgets/LineChart.hpp"
#include "widgets/PieChart.hpp"

#include <tui/widget/layout/ScrollableContainer.hpp>
#include <tui/widgets/Label.hpp>

#include <functional>
#include <memory>
#include <vector>

namespace sysmon {

// ScrollableVertical n'est pas focusable() par defaut dans la
// bibliotheque (donc jamais atteint par Tab), on l'etend pour permettre
// le defilement clavier de cet onglet en plus de la molette (deja geree
// par ScrollableContainer::on_mouse).
class FocusableScrollable : public tui::ScrollableVertical {
public:
    [[nodiscard]] bool focusable() const override { return true; }
    void set_on_quit(std::function<void()> fn) { on_quit_ = std::move(fn); }

    bool on_key(const tui::KeyEvent& e) override {
        if (e.key == tui::Key::Char && (e.codepoint == U'q' || e.codepoint == U'Q')) {
            if (on_quit_) on_quit_();
            return true;
        }
        return tui::ScrollableVertical::on_key(e);
    }

private:
    std::function<void()> on_quit_;
};

class ResourcesTab {
public:
    ResourcesTab();

    [[nodiscard]] std::shared_ptr<tui::Widget> root() const { return scrollable_; }
    [[nodiscard]] std::shared_ptr<FocusableScrollable> scrollable() const { return scrollable_; }

    void refresh(double dt);

private:
    std::shared_ptr<FocusableScrollable> scrollable_;

    std::shared_ptr<LineChart> cpu_chart_;
    std::vector<int> cpu_series_;

    std::shared_ptr<LineChart> mem_chart_;
    int mem_series_ = -1;
    int swap_series_ = -1;
    std::shared_ptr<PieChart> mem_pie_;

    std::shared_ptr<LineChart> net_chart_;
    int net_rx_series_ = -1;
    int net_tx_series_ = -1;
    std::shared_ptr<tui::Label> net_stats_label_;

    std::shared_ptr<LineChart> disk_chart_;
    int disk_read_series_ = -1;
    int disk_write_series_ = -1;
    std::shared_ptr<tui::Label> disk_stats_label_;

    CpuStatState cpu_state_;
    NetState net_state_;
    DiskState disk_state_;
};

} // namespace sysmon
