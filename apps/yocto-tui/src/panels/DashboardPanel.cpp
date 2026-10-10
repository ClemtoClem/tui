// panels/DashboardPanel.cpp
#include "panels/DashboardPanel.hpp"
#include "Theme.hpp"
#include <tui/widget/layout/Horizontal.hpp>

namespace yocto {

DashboardPanel::DashboardPanel(const WorkspaceInfo& ws) : workspace_(ws) {
    auto title = std::make_shared<tui::Label>("Tableau de bord Yocto");
    title->set_style(tui::TextStyle::Bold);
    title->set_foreground(kFg);

    distro_label_ = std::make_shared<tui::Label>("Distro : " + ws.distro);
    distro_label_->set_foreground(kMuted);
    machine_label_ = std::make_shared<tui::Label>("Machine : " + ws.machine);
    machine_label_->set_foreground(kMuted);
    image_label_ = std::make_shared<tui::Label>("Image : " + ws.image_recipe);
    image_label_->set_foreground(kMuted);

    build_button_ = std::make_shared<tui::Button>("Lancer la construction");
    build_button_->set_on_click([this] { if (on_build_) on_build_(); });

    recent_jobs_ = std::make_shared<tui::ListView>();
    recent_jobs_->set_style(tui::ListViewStyle::Detailed);
    recent_jobs_->set_colors(kAccent, kFg, kMuted);
    recent_jobs_->set_items({
        tui::ListItem{"core-image-minimal", "Terminé - il y a 2 heures", true, ""},
        tui::ListItem{"virtual/kernel",     "Échec - il y a 1 jour",     true, ""},
        tui::ListItem{"u-boot",             "Terminé - il y a 2 jours",  true, ""},
    });

    auto content = std::make_shared<tui::Vertical>();
    content->add_child(title, tui::LayoutParams::fixed(1));
    content->add_child(std::make_shared<tui::Label>(""), tui::LayoutParams::fixed(1));
    content->add_child(distro_label_, tui::LayoutParams::fixed(1));
    content->add_child(machine_label_, tui::LayoutParams::fixed(1));
    content->add_child(image_label_, tui::LayoutParams::fixed(1));
    content->add_child(std::make_shared<tui::Label>(""), tui::LayoutParams::fixed(1));
    content->add_child(build_button_, tui::LayoutParams{tui::LayoutMode::Auto, 0, tui::CrossAlign::Start});
    content->add_child(std::make_shared<tui::Label>(""), tui::LayoutParams::fixed(1));
    content->add_child(std::make_shared<tui::Label>("Constructions récentes :"), tui::LayoutParams::fixed(1));
    content->add_child(recent_jobs_, tui::LayoutParams::stretch(1));

    panel_ = std::make_shared<tui::Border>();
    panel_->set_border_style(tui::BorderStyle::Rounded);
    panel_->set_color(kBorder);
    panel_->set_title("Yocto Dashboard");
    panel_->set_child(content);
}

void DashboardPanel::refresh() {
    distro_label_->set_text("Distro : " + workspace_.distro);
    machine_label_->set_text("Machine : " + workspace_.machine);
    image_label_->set_text("Image : " + workspace_.image_recipe);
}

} // namespace yocto