#include "SettingsPanel.hpp"

#include "../Theme.hpp"

#include <tui/widget/layout/Border.hpp>
#include <tui/widget/layout/Vertical.hpp>
#include <tui/widgets/Label.hpp>

using tui::LayoutParams;
using tui::Vertical;

namespace ide {

SettingsPanel::SettingsPanel(AppSettings initial) : settings_(std::move(initial)) {
    build_ui();
}

void SettingsPanel::emit() {
    if (on_change_) on_change_(settings_);
}

void SettingsPanel::build_ui() {
    auto title = std::make_shared<tui::Label>("Parametres de l'application");
    title->set_style(tui::TextStyle::Bold);
    title->set_foreground(kFg);

    auto theme_label = std::make_shared<tui::Label>("Theme :");
    theme_label->set_foreground(kMuted);
    theme_radio_ = std::make_shared<tui::RadioSet>();
    theme_radio_->set_options({"Planor (violet)", "Sombre", "Clair"});
    theme_radio_->set_selected_index(settings_.theme == "dark" ? 1 : settings_.theme == "light" ? 2 : 0);
    theme_radio_->set_on_change([this](size_t i) {
        settings_.theme = i == 1 ? "dark" : i == 2 ? "light" : "planor";
        emit();
    });

    blame_checkbox_ = std::make_shared<tui::Checkbox>("Afficher git blame dans la marge de l'editeur");
    blame_checkbox_->set_checked(settings_.show_blame);
    blame_checkbox_->set_on_change([this](bool v) { settings_.show_blame = v; emit(); });

    confirm_checkbox_ = std::make_shared<tui::Checkbox>("Confirmer avant de fermer un fichier modifie");
    confirm_checkbox_->set_checked(settings_.confirm_before_close);
    confirm_checkbox_->set_on_change([this](bool v) { settings_.confirm_before_close = v; emit(); });

    auto tab_width_label = std::make_shared<tui::Label>("Largeur de tabulation :");
    tab_width_label->set_foreground(kMuted);
    tab_width_input_ = std::make_shared<tui::NumberInput>();
    tab_width_input_->set_range(1, 8);
    tab_width_input_->set_step(1);
    tab_width_input_->set_value(settings_.tab_width);
    tab_width_input_->set_on_change([this](double v) { settings_.tab_width = static_cast<int>(v); emit(); });

    auto shell_label = std::make_shared<tui::Label>("Shell du terminal integre (vide = $SHELL) :");
    shell_label->set_foreground(kMuted);
    shell_input_ = std::make_shared<tui::Input>();
    shell_input_->set_text(settings_.default_shell);
    shell_input_->set_placeholder("/bin/bash");
    shell_input_->set_on_change([this](const std::string& v) { settings_.default_shell = v; emit(); });

    auto spacer = [] { return std::make_shared<tui::Label>(""); };

    auto content = std::make_shared<Vertical>();
    content->add_child(title, LayoutParams::fixed(1));
    content->add_child(spacer(), LayoutParams::fixed(1));
    content->add_child(theme_label, LayoutParams::fixed(1));
    content->add_child(theme_radio_, LayoutParams::fixed(3));
    content->add_child(spacer(), LayoutParams::fixed(1));
    content->add_child(blame_checkbox_, LayoutParams::fixed(1));
    content->add_child(confirm_checkbox_, LayoutParams::fixed(1));
    content->add_child(spacer(), LayoutParams::fixed(1));
    content->add_child(tab_width_label, LayoutParams::fixed(1));
    content->add_child(tab_width_input_, LayoutParams::fixed(1));
    content->add_child(spacer(), LayoutParams::fixed(1));
    content->add_child(shell_label, LayoutParams::fixed(1));
    content->add_child(shell_input_, LayoutParams::fixed(1));
    content->add_child(spacer(), LayoutParams::stretch(1));

    auto panel = std::make_shared<tui::Border>();
    panel->set_border_style(tui::BorderStyle::Rounded);
    panel->set_color(kBorder);
    panel->set_title("Configuration");
    panel->set_child(content);
    root_ = panel;
}

} // namespace ide
