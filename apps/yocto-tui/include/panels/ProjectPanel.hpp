/* apps/yocto-tui/panels/ProjectPanel.hpp */
#pragma once

#include "../config/YoctoConfig.hpp"
#include "project/Editors.hpp"

#include <tui/widget/Widget.hpp>
#include <tui/widget/layout/Tabs.hpp>
#include <tui/widgets/Label.hpp>

#include <functional>
#include <memory>
#include <string>

namespace yocto {

/// Onglet « Projet » : édition du layer, de l'image, des couches
/// externes, des applications custom, de la configuration Linux de la
/// cible, des patchs et des fichiers Yocto sur disque. Le ProjectSpec
/// édité est poussé à chaque modification (on_changed) ; l'enregistrement
/// dans configs/config_yocto_<nom>.ini est demandé via on_save.
class ProjectPanel {
public:
    ProjectPanel();

    [[nodiscard]] std::shared_ptr<tui::Widget> root() const { return root_; }

    /// Charge le projet d'une configuration (changement de profil).
    void load(const YoctoConfig& cfg);
    /// Met à jour les chemins du lab sans toucher au projet édité.
    void set_environment(const YoctoConfig& cfg);

    [[nodiscard]] const ProjectSpec& spec() const { return spec_; }
    [[nodiscard]] const std::string& loaded_name() const { return loaded_name_; }
    [[nodiscard]] bool dirty() const { return dirty_; }
    void mark_saved();

    /// Ctrl+S dans le sous-onglet « Fichiers Yocto » enregistre le
    /// fichier ouvert ; retourne false ailleurs (enregistrement du projet).
    bool handle_save_shortcut();

    /// Sous-onglet précédent / suivant (Alt+← / Alt+→).
    void cycle_subtab(int delta);
    [[nodiscard]] std::shared_ptr<tui::Tabs> tabs() const { return tabs_; }

    void set_on_changed(std::function<void(const ProjectSpec&)> cb) { on_changed_ = std::move(cb); }
    void set_on_save(std::function<void()> cb) { on_save_ = std::move(cb); }
    void set_on_status(std::function<void(const std::string&)> cb);

private:
    void on_edit();
    void reload_all();
    void update_header();
    void load_preset();
    void reset_project();

    ProjectSpec spec_;
    YoctoConfig env_;
    std::string loaded_name_;
    bool dirty_ = false;

    std::shared_ptr<tui::Widget> root_;
    std::shared_ptr<tui::Tabs> tabs_;
    std::shared_ptr<tui::Label> dirty_label_;
    std::shared_ptr<tui::Label> issues_label_;

    std::unique_ptr<LayerImageEditor> layer_editor_;
    std::unique_ptr<LayersConfEditor> layers_conf_editor_;
    std::unique_ptr<AppsEditor> apps_editor_;
    std::unique_ptr<SystemEditor> system_editor_;
    std::unique_ptr<PatchesEditor> patches_editor_;
    std::unique_ptr<YoctoFilesEditor> files_editor_;

    std::function<void(const ProjectSpec&)> on_changed_;
    std::function<void()> on_save_;
    std::function<void(const std::string&)> on_status_;
};

} // namespace yocto
