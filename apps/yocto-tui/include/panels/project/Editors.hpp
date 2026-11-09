/* apps/yocto-tui/panels/project/Editors.hpp */
#pragma once

#include "../../config/YoctoConfig.hpp"
#include "../../project/ProjectSpec.hpp"

#include <tui/widget/Widget.hpp>
#include <tui/widgets/Button.hpp>
#include <tui/widgets/Checkbox.hpp>
#include <tui/widgets/Input.hpp>
#include <tui/widgets/Label.hpp>
#include <tui/widgets/ListView.hpp>
#include <tui/widgets/RadioSet.hpp>
#include <tui/widgets/TextArea.hpp>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace yocto {

/// Base des sous-onglets d'édition du projet : chaque éditeur modifie
/// directement le ProjectSpec partagé (possédé par ProjectPanel) et
/// signale chaque modification par `on_changed`.
class EditorBase {
public:
    EditorBase(ProjectSpec& spec, std::function<void()> on_changed)
        : spec_(spec), on_changed_(std::move(on_changed)) {}
    virtual ~EditorBase() = default;
    EditorBase(const EditorBase&) = delete;
    EditorBase& operator=(const EditorBase&) = delete;

    [[nodiscard]] std::shared_ptr<tui::Widget> root() const { return root_; }

    /// Relit entièrement le projet (préréglage chargé, autre config).
    virtual void reload() = 0;

    void set_on_status(std::function<void(const std::string&)> cb) { on_status_ = std::move(cb); }

protected:
    void changed() {
        if (!loading_ && on_changed_) on_changed_();
    }
    void status(const std::string& msg) {
        if (on_status_) on_status_(msg);
    }
    /// Branche un Input sur un setter, ignoré pendant le chargement.
    void bind(const std::shared_ptr<tui::Input>& in, std::function<void(const std::string&)> set);
    void bind(const std::shared_ptr<tui::Checkbox>& cb, std::function<void(bool)> set);
    void bind(const std::shared_ptr<tui::TextArea>& ta, std::function<void(const std::string&)> set);
    void bind(const std::shared_ptr<tui::RadioSet>& rs, std::function<void(size_t)> set);

    ProjectSpec& spec_;
    bool loading_ = false;
    std::shared_ptr<tui::Widget> root_;
    std::function<void()> on_changed_;
    std::function<void(const std::string&)> on_status_;
};

// --- Layer, image et comptes ---------------------------------------------------

class LayerImageEditor : public EditorBase {
public:
    LayerImageEditor(ProjectSpec& spec, std::function<void()> on_changed);
    void reload() override;

private:
    void refresh_users(size_t select);
    void load_user(size_t index);
    UserAccount* current_user();

    std::shared_ptr<tui::Input> layer_name_, priority_, depends_;
    std::shared_ptr<tui::Input> image_name_, base_, summary_, hostname_, packages_, features_, root_password_;
    std::shared_ptr<tui::Checkbox> empty_root_;
    std::shared_ptr<tui::ListView> users_;
    std::shared_ptr<tui::Input> user_name_, user_password_, user_groups_;
};

// --- Couches externes et local.conf ----------------------------------------------

class LayersConfEditor : public EditorBase {
public:
    LayersConfEditor(ProjectSpec& spec, std::function<void()> on_changed);
    void reload() override;

private:
    void refresh_layers(size_t select);
    void load_layer(size_t index);
    ExtraLayer* current_layer();

    std::shared_ptr<tui::ListView> layers_;
    std::shared_ptr<tui::Input> name_, url_, branch_, sublayers_;
    std::shared_ptr<tui::Checkbox> enabled_;
    std::shared_ptr<tui::Checkbox> rm_work_;
    std::shared_ptr<tui::TextArea> local_conf_;
};

// --- Applications custom -------------------------------------------------------

class AppsEditor : public EditorBase {
public:
    AppsEditor(ProjectSpec& spec, std::function<void()> on_changed);
    void reload() override;

private:
    void refresh_apps(size_t select);
    void load_app(size_t index);
    void refresh_files(size_t select);
    void load_file(size_t index);
    void update_source_widgets();
    void import_from_host();
    CustomApp* current_app();
    ProjectFile* current_file();

    std::shared_ptr<tui::ListView> apps_;
    std::shared_ptr<tui::Input> name_, version_, summary_, license_, chksum_;
    std::shared_ptr<tui::RadioSet> kind_, source_;
    std::shared_ptr<tui::Input> source_dir_, git_url_, git_branch_, git_srcrev_;
    std::shared_ptr<tui::Input> depends_, rdepends_, extra_conf_, autostart_, autostart_params_;
    std::shared_ptr<tui::Checkbox> enabled_, in_image_;
    std::shared_ptr<tui::ListView> files_;
    std::shared_ptr<tui::Input> file_name_, file_dest_, file_mode_;
    std::shared_ptr<tui::TextArea> content_;
    std::shared_ptr<tui::Label> content_hint_;
    std::shared_ptr<tui::Input> import_path_;
};

// --- Système Linux : fichiers de la cible et fragments Kconfig --------------------

class SystemEditor : public EditorBase {
public:
    SystemEditor(ProjectSpec& spec, std::function<void()> on_changed);
    void reload() override;

private:
    void refresh_files(size_t select);
    void load_file(size_t index);
    void refresh_fragments(size_t select);
    void load_fragment(size_t index);
    void update_owner_hint();
    RootfsFile* current_file();
    ConfigFragment* current_fragment();

    std::shared_ptr<tui::ListView> files_;
    std::shared_ptr<tui::Input> path_, mode_, owner_;
    std::shared_ptr<tui::Label> owner_hint_;
    std::shared_ptr<tui::Checkbox> file_enabled_;
    std::shared_ptr<tui::TextArea> file_content_;

    std::shared_ptr<tui::ListView> fragments_;
    std::shared_ptr<tui::Input> frag_recipe_, frag_name_;
    std::shared_ptr<tui::Checkbox> frag_enabled_;
    std::shared_ptr<tui::TextArea> frag_content_;
};

// --- Patchs ----------------------------------------------------------------------

class PatchesEditor : public EditorBase {
public:
    PatchesEditor(ProjectSpec& spec, std::function<void()> on_changed);
    void reload() override;

private:
    void refresh_patches(size_t select);
    void load_patch(size_t index);
    PatchSpec* current_patch();

    std::shared_ptr<tui::ListView> patches_;
    std::shared_ptr<tui::Input> recipe_, name_, file_, search_, replace_, message_, upstream_;
    std::shared_ptr<tui::RadioSet> method_;
    std::shared_ptr<tui::Checkbox> replace_all_, enabled_;
    std::shared_ptr<tui::TextArea> patch_content_;
};

// --- Fichiers Yocto sur disque -----------------------------------------------------

/// Édition directe des fichiers réels du lab : local.conf, bblayers.conf,
/// env.sh et tout fichier du layer (y compris ceux produits par devtool).
class YoctoFilesEditor : public EditorBase {
public:
    YoctoFilesEditor(ProjectSpec& spec, std::function<void()> on_changed);
    void reload() override;
    void set_config(const YoctoConfig& cfg);
    /// Enregistre le fichier ouvert (bouton ou Ctrl+S).
    void save_current();

private:
    void refresh_list();
    void open_selected();

    YoctoConfig config_;
    std::vector<std::filesystem::path> paths_;
    std::filesystem::path open_path_;
    std::string open_content_;
    std::shared_ptr<tui::ListView> list_;
    std::shared_ptr<tui::Label> path_label_;
    std::shared_ptr<tui::Label> warn_label_;
    std::shared_ptr<tui::TextArea> editor_;
};

} // namespace yocto
