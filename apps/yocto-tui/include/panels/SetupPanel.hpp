/* apps/yocto-tui/panels/SetupPanel.hpp */
#pragma once

#include "../config/ConfigManager.hpp"
#include "../config/YoctoConfig.hpp"
#include "../runners/SetupRunner.hpp"

#include <tui/App.hpp>
#include <tui/widget/Widget.hpp>
#include <tui/widget/layout/Border.hpp>
#include <tui/widgets/Button.hpp>
#include <tui/widgets/Checkbox.hpp>
#include <tui/widgets/Input.hpp>
#include <tui/widgets/Label.hpp>
#include <tui/widgets/ListView.hpp>
#include <tui/widgets/TextArea.hpp>

#include <functional>
#include <memory>
#include <string>

namespace yocto {

/// Onglet « Configuration & Préparation » : sélectionne, crée,
/// duplique, édite, sauvegarde et supprime les profils
/// configs/config_yocto_<nom>.ini, puis exécute la séquence
/// SetupRunner sur un thread de travail. La sortie des commandes est
/// marshalée vers le thread UI via App::post().
class SetupPanel {
public:
    SetupPanel(tui::App& app, ConfigManager& configs);

    [[nodiscard]] std::shared_ptr<tui::Widget> root() const { return panel_; }
    [[nodiscard]] const YoctoConfig& current_config() const { return current_; }
    [[nodiscard]] bool has_config() const { return has_config_; }

    /// Recharge la liste des configs et sélectionne `name` si non vide.
    void reload(const std::string& name = "");

    void set_on_config_changed(std::function<void(const YoctoConfig&)> cb) {
        on_config_changed_ = std::move(cb);
    }
    void set_on_status(std::function<void(const std::string&)> cb) {
        on_status_ = std::move(cb);
    }

private:
    void build_ui();

    void refresh_config_list();
    void load_selected();
    void new_config();
    void duplicate_selected();
    void delete_selected();
    void save_current();

    void apply_form_to_config();
    void apply_config_to_form();

    void start_setup();
    void retry_selected_step();
    void append_log(const std::string& line);
    void set_step_status(size_t index, StepStatus s, const std::string& msg);

    tui::App& app_;
    ConfigManager& configs_;

    std::shared_ptr<tui::Border>    panel_;
    std::shared_ptr<tui::ListView>  config_list_;
    std::shared_ptr<tui::Input>     name_input_;
    std::shared_ptr<tui::Input>     workdir_input_;
    std::shared_ptr<tui::Input>     url_input_;
    std::shared_ptr<tui::Input>     branch_input_;
    std::shared_ptr<tui::Input>     tag_input_;
    std::shared_ptr<tui::Input>     machine_input_;
    std::shared_ptr<tui::Input>     threads_input_;
    std::shared_ptr<tui::Input>     image_input_;
    std::shared_ptr<tui::Input>     git_name_input_;
    std::shared_ptr<tui::Input>     git_email_input_;
    std::shared_ptr<tui::Checkbox>  install_check_;
    std::shared_ptr<tui::Checkbox>  locale_check_;
    std::shared_ptr<tui::Checkbox>  apparmor_check_;
    std::shared_ptr<tui::Checkbox>  git_check_;
    std::shared_ptr<tui::Checkbox>  first_build_check_;
    std::shared_ptr<tui::Button>    save_button_;
    std::shared_ptr<tui::Button>    new_button_;
    std::shared_ptr<tui::Button>    duplicate_button_;
    std::shared_ptr<tui::Button>    delete_button_;
    std::shared_ptr<tui::Button>    run_button_;
    std::shared_ptr<tui::Button>    retry_button_;
    std::shared_ptr<tui::ListView>  step_list_;
    std::shared_ptr<tui::TextArea>  log_view_;

    YoctoConfig current_;
    bool has_config_ = false;
    std::string log_buffer_;
    std::unique_ptr<SetupRunner> runner_;
    bool setup_running_ = false;

    std::function<void(const YoctoConfig&)> on_config_changed_;
    std::function<void(const std::string&)> on_status_;
};

} // namespace yocto