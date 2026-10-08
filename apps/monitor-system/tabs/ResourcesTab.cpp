#include "ResourcesTab.hpp"

#include <tui/widget/layout/Border.hpp>
#include <tui/widget/layout/Horizontal.hpp>
#include <tui/widget/layout/Vertical.hpp>
#include <tui/widgets/Badge.hpp>

#include <cstdio>

namespace sysmon {

namespace {

tui::Color core_palette_color(int i) {
    static const tui::Color kPalette[] = {
        {220, 50, 47}, {38, 139, 210}, {133, 153, 0}, {211, 54, 130}, {42, 161, 152},
        {181, 137, 0}, {108, 113, 196}, {220, 120, 60}, {100, 200, 100}, {180, 100, 220},
    };
    return kPalette[static_cast<size_t>(i) % (sizeof(kPalette) / sizeof(kPalette[0]))];
}

std::shared_ptr<tui::Widget> wrap_bordered(std::string title, std::shared_ptr<tui::Widget> child) {
    auto border = std::make_shared<tui::Border>();
    border->set_title(std::move(title));
    border->set_child(std::move(child));
    return border;
}

} // namespace

ResourcesTab::ResourcesTab() {
    scrollable_ = std::make_shared<FocusableScrollable>();
    auto content = std::make_shared<tui::Vertical>();

    // --- CPU ---------------------------------------------------------
    {
        // Un premier releve (jete) pour connaitre le nombre de coeurs
        // avant de construire l'arbre de widgets (une serie par coeur).
        CpuSample discovery = read_cpu(cpu_state_);
        int core_count = static_cast<int>(discovery.per_core_percent.size());

        cpu_chart_ = std::make_shared<LineChart>(YMode::FixedPercent);
        auto legend = std::make_shared<tui::Horizontal>();
        for (int i = 0; i < core_count; ++i) {
            tui::Color color = core_palette_color(i);
            int idx = cpu_chart_->add_series("CPU" + std::to_string(i), color);
            cpu_series_.push_back(idx);

            auto badge = std::make_shared<tui::Badge>("CPU" + std::to_string(i));
            badge->set_colors(tui::Color::Black(), color);
            legend->add_child(badge, tui::LayoutParams::auto_size());
        }

        auto section = std::make_shared<tui::Vertical>();
        section->add_child(cpu_chart_, tui::LayoutParams::auto_size());
        section->add_child(legend, tui::LayoutParams::fixed(1));
        content->add_child(wrap_bordered("Utilisation CPU", section), tui::LayoutParams::auto_size());
    }

    // --- Memoire / Swap ------------------------------------------------
    {
        mem_chart_ = std::make_shared<LineChart>(YMode::FixedPercent);
        mem_series_ = mem_chart_->add_series("Memoire", tui::Color::Blue());
        swap_series_ = mem_chart_->add_series("Swap", tui::Color{230, 160, 30});

        auto legend = std::make_shared<tui::Horizontal>();
        auto mem_badge = std::make_shared<tui::Badge>("Memoire");
        mem_badge->set_colors(tui::Color::Black(), tui::Color::Blue());
        auto swap_badge = std::make_shared<tui::Badge>("Swap");
        swap_badge->set_colors(tui::Color::Black(), tui::Color{230, 160, 30});
        legend->add_child(mem_badge, tui::LayoutParams::auto_size());
        legend->add_child(swap_badge, tui::LayoutParams::auto_size());

        mem_pie_ = std::make_shared<PieChart>();

        auto chart_section = std::make_shared<tui::Vertical>();
        chart_section->add_child(mem_chart_, tui::LayoutParams::auto_size());
        chart_section->add_child(legend, tui::LayoutParams::fixed(1));

        auto row = std::make_shared<tui::Horizontal>();
        row->add_child(wrap_bordered("Memoire & Swap", chart_section), tui::LayoutParams::stretch(2));
        row->add_child(wrap_bordered("Repartition memoire", mem_pie_), tui::LayoutParams::stretch(1));
        content->add_child(row, tui::LayoutParams::auto_size());
    }

    // --- Reseau ----------------------------------------------------------
    {
        net_chart_ = std::make_shared<LineChart>(YMode::AutoScale);
        net_chart_->set_y_formatter([](double v) { return human_rate(v); });
        net_rx_series_ = net_chart_->add_series("Recu", tui::Color::Green());
        net_tx_series_ = net_chart_->add_series("Envoye", tui::Color{230, 160, 30});

        net_stats_label_ = std::make_shared<tui::Label>("");

        auto section = std::make_shared<tui::Vertical>();
        section->add_child(net_chart_, tui::LayoutParams::auto_size());
        section->add_child(net_stats_label_, tui::LayoutParams::fixed(1));
        content->add_child(wrap_bordered("Reseau", section), tui::LayoutParams::auto_size());
    }

    // --- Disque ------------------------------------------------------------
    {
        disk_chart_ = std::make_shared<LineChart>(YMode::AutoScale);
        disk_chart_->set_y_formatter([](double v) { return human_rate(v); });
        disk_read_series_ = disk_chart_->add_series("Lecture", tui::Color::Green());
        disk_write_series_ = disk_chart_->add_series("Ecriture", tui::Color{230, 160, 30});

        disk_stats_label_ = std::make_shared<tui::Label>("");

        auto section = std::make_shared<tui::Vertical>();
        section->add_child(disk_chart_, tui::LayoutParams::auto_size());
        section->add_child(disk_stats_label_, tui::LayoutParams::fixed(1));
        content->add_child(wrap_bordered("Disque", section), tui::LayoutParams::auto_size());
    }

    scrollable_->set_child(content);
}

void ResourcesTab::refresh(double dt) {
    CpuSample cpu = read_cpu(cpu_state_);
    for (size_t i = 0; i < cpu_series_.size() && i < cpu.per_core_percent.size(); ++i) {
        cpu_chart_->push_sample(cpu_series_[i], cpu.per_core_percent[i]);
    }

    MemInfo mem = read_mem();
    double mem_pct = mem.total > 0 ? 100.0 * static_cast<double>(mem.used) / static_cast<double>(mem.total) : 0.0;
    double swap_pct =
        mem.swap_total > 0 ? 100.0 * static_cast<double>(mem.swap_used) / static_cast<double>(mem.swap_total) : 0.0;
    mem_chart_->push_sample(mem_series_, mem_pct);
    mem_chart_->push_sample(swap_series_, swap_pct);

    mem_pie_->set_center_text(human_go(mem.used) + " / " + human_go(mem.total));
    mem_pie_->set_slices({
        {"Utilise", static_cast<double>(mem.used), human_go(mem.used), tui::Color::Blue()},
        {"Cache", static_cast<double>(mem.cache), human_go(mem.cache), tui::Color{100, 200, 100}},
        {"Libre", static_cast<double>(mem.free), human_go(mem.free), tui::Color::Gray()},
    });

    NetSample net = read_net(net_state_, dt);
    net_chart_->push_sample(net_rx_series_, net.rx_rate);
    net_chart_->push_sample(net_tx_series_, net.tx_rate);
    net_stats_label_->set_text("Recu: " + human_rate(net.rx_rate) + " (total " + human_bytes(static_cast<double>(net.rx_bytes_total)) +
                                ")   Envoye: " + human_rate(net.tx_rate) + " (total " +
                                human_bytes(static_cast<double>(net.tx_bytes_total)) + ")");

    DiskIoSample disk = read_disk(disk_state_, dt);
    disk_chart_->push_sample(disk_read_series_, disk.read_rate);
    disk_chart_->push_sample(disk_write_series_, disk.write_rate);
    disk_stats_label_->set_text("Lecture: " + human_rate(disk.read_rate) + " (total " +
                                 human_bytes(static_cast<double>(disk.read_bytes_total)) + ")   Ecriture: " +
                                 human_rate(disk.write_rate) + " (total " +
                                 human_bytes(static_cast<double>(disk.write_bytes_total)) + ")");
}

} // namespace sysmon
