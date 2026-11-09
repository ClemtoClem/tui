// panels/ConfigPanel.cpp
#include "panels/ConfigPanel.hpp"
#include "Theme.hpp"
#include <tui/widget/layout/Vertical.hpp>

namespace yocto {

ConfigPanel::ConfigPanel(const WorkspaceInfo& ws) : workspace_(ws) {
    auto title = std::make_shared<tui::Label>("Configuration locale (local.conf)");
    title->set_style(tui::TextStyle::Bold);

    auto distro_lbl = std::make_shared<tui::Label>("DISTRO :");
    distro_lbl->set_foreground(kMuted);
    distro_input_ = std::make_shared<tui::Input>();
    distro_input_->set_text(ws.distro);
    distro_input_->set_placeholder("poky");

    auto machine_lbl = std::make_shared<tui::Label>("MACHINE :");
    machine_lbl->set_foreground(kMuted);
    machine_input_ = std::make_shared<tui::Input>();
    machine_input_->set_text(ws.machine);
    machine_input_->set_placeholder("qemux86-64");

    auto image_lbl = std::make_shared<tui::Label>("IMAGE_RECIPE :");
    image_lbl->set_foreground(kMuted);
    image_input_ = std::make_shared<tui::Input>();
    image_input_->set_text(ws.image_recipe);
    image_input_->set_placeholder("core-image-minimal");

    debug_check_ = std::make_shared<tui::Checkbox>("Activer les symboles de débogage (dbg-pkgs)");

    save_button_ = std::make_shared<tui::Button>("Enregistrer");
    save_button_->set_on_click([this] { save(); });

    auto content = std::make_shared<tui::Vertical>();
    content->add_child(title, tui::LayoutParams::fixed(1));
    content->add_child(std::make_shared<tui::Label>(""), tui::LayoutParams::fixed(1));
    content->add_child(distro_lbl, tui::LayoutParams::fixed(1));
    content->add_child(distro_input_, tui::LayoutParams::fixed(1));
    content->add_child(std::make_shared<tui::Label>(""), tui::LayoutParams::fixed(1));
    content->add_child(machine_lbl, tui::LayoutParams::fixed(1));
    content->add_child(machine_input_, tui::LayoutParams::fixed(1));
    content->add_child(std::make_shared<tui::Label>(""), tui::LayoutParams::fixed(1));
    content->add_child(image_lbl, tui::LayoutParams::fixed(1));
    content->add_child(image_input_, tui::LayoutParams::fixed(1));
    content->add_child(std::make_shared<tui::Label>(""), tui::LayoutParams::fixed(1));
    content->add_child(debug_check_, tui::LayoutParams::fixed(1));
    content->add_child(std::make_shared<tui::Label>(""), tui::LayoutParams::fixed(1));
    content->add_child(save_button_, tui::LayoutParams{tui::LayoutMode::Auto, 0, tui::CrossAlign::Start});
    content->add_child(std::make_shared<tui::Label>(""), tui::LayoutParams::stretch(1));

    panel_ = std::make_shared<tui::Border>();
    panel_->set_border_style(tui::BorderStyle::Rounded);
    panel_->set_color(kBorder);
    panel_->set_title("Configuration");
    panel_->set_child(content);
}

void ConfigPanel::save() {
    Workspace::write_local_conf_var(workspace_.build_dir, "DISTRO", distro_input_->text());
    Workspace::write_local_conf_var(workspace_.build_dir, "MACHINE", machine_input_->text());
    // L'image recipe est stockée séparément (variable d'application)
    if (on_saved_) on_saved_();
}

} // namespace yocto