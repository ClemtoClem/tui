#pragma once

#include <tui/widget/Widget.hpp>
#include <tui/widgets/Checkbox.hpp>
#include <tui/widgets/Input.hpp>
#include <tui/widgets/NumberInput.hpp>
#include <tui/widgets/RadioSet.hpp>

#include <functional>
#include <memory>
#include <string>

namespace ide {

struct AppSettings {
    std::string theme = "planor"; // "planor" | "dark" | "light"
    bool show_blame = false;
    bool confirm_before_close = true;
    int tab_width = 4;
    std::string default_shell; // vide = $SHELL
};

/// Panneau de configuration : options de l'application via les widgets de
/// formulaire génériques du module ui (RadioSet/Checkbox/NumberInput/
/// Input) - aucun widget nouveau requis ici, uniquement de la composition.
class SettingsPanel {
public:
    explicit SettingsPanel(AppSettings initial);

    [[nodiscard]] std::shared_ptr<tui::Widget> root() const { return root_; }
    [[nodiscard]] const AppSettings& settings() const { return settings_; }
    void set_on_change(std::function<void(const AppSettings&)> cb) { on_change_ = std::move(cb); }

private:
    void build_ui();
    void emit();

    AppSettings settings_;
    std::shared_ptr<tui::Widget> root_;
    std::shared_ptr<tui::RadioSet> theme_radio_;
    std::shared_ptr<tui::Checkbox> blame_checkbox_;
    std::shared_ptr<tui::Checkbox> confirm_checkbox_;
    std::shared_ptr<tui::NumberInput> tab_width_input_;
    std::shared_ptr<tui::Input> shell_input_;
    std::function<void(const AppSettings&)> on_change_;
};

} // namespace ide
