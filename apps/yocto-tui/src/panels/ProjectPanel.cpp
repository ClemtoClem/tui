/* @file panels/ProjectPanel.cpp */

#include "panels/ProjectPanel.hpp"
#include "panels/project/Ui.hpp"
#include "project/Presets.hpp"

namespace yocto {

using tui::LayoutParams;

ProjectPanel::ProjectPanel() {
    auto edited = [this] { on_edit(); };
    layer_editor_       = std::make_unique<LayerImageEditor>(spec_, edited);
    layers_conf_editor_ = std::make_unique<LayersConfEditor>(spec_, edited);
    apps_editor_        = std::make_unique<AppsEditor>(spec_, edited);
    system_editor_      = std::make_unique<SystemEditor>(spec_, edited);
    patches_editor_     = std::make_unique<PatchesEditor>(spec_, edited);
    files_editor_       = std::make_unique<YoctoFilesEditor>(spec_, edited);

    tabs_ = std::make_shared<tui::Tabs>();
    tabs_->set_bar_style(tui::TabsBarStyle::Line);
    tabs_->set_colors(kBorder, kAccent, kMuted);
    tabs_->add_tab(" Layer & image ", layer_editor_->root());
    tabs_->add_tab(" Couches & local.conf ", layers_conf_editor_->root());
    tabs_->add_tab(" Applications ", apps_editor_->root());
    tabs_->add_tab(" Système Linux ", system_editor_->root());
    tabs_->add_tab(" Patchs ", patches_editor_->root());
    tabs_->add_tab(" Fichiers Yocto ", files_editor_->root());

    auto save = ui::button("Enregistrer (Ctrl+S)", [this] { if (on_save_) on_save_(); });
    auto preset = ui::button("Préréglage « TP formation »", [this] { load_preset(); });
    auto reset = ui::button("Projet vide", [this] { reset_project(); });

    dirty_label_ = ui::muted("");
    issues_label_ = ui::muted("");

    auto header = std::make_shared<tui::Horizontal>();
    header->add_child(save, LayoutParams{tui::LayoutMode::Auto, 0, tui::CrossAlign::Start});
    header->add_child(preset, LayoutParams{tui::LayoutMode::Auto, 0, tui::CrossAlign::Start});
    header->add_child(reset, LayoutParams{tui::LayoutMode::Auto, 0, tui::CrossAlign::Start});
    header->add_child(dirty_label_, LayoutParams::stretch(1));

    auto layout = std::make_shared<tui::Vertical>();
    layout->add_child(header, LayoutParams::fixed(1));
    layout->add_child(issues_label_, LayoutParams::fixed(1));
    layout->add_child(tabs_, LayoutParams::stretch(1));
    root_ = layout;
    update_header();
}

void ProjectPanel::set_on_status(std::function<void(const std::string&)> cb) {
    on_status_ = cb;
    for (EditorBase* e : std::initializer_list<EditorBase*>{
             layer_editor_.get(), layers_conf_editor_.get(), apps_editor_.get(),
             system_editor_.get(), patches_editor_.get(), files_editor_.get()}) {
        e->set_on_status(cb);
    }
}

void ProjectPanel::load(const YoctoConfig& cfg) {
    spec_ = cfg.project;
    loaded_name_ = cfg.name;
    dirty_ = false;
    env_ = cfg;
    reload_all();
    files_editor_->set_config(cfg);
    update_header();
}

void ProjectPanel::set_environment(const YoctoConfig& cfg) {
    env_ = cfg;
    YoctoConfig with_spec = cfg;
    with_spec.project = spec_;
    files_editor_->set_config(with_spec);
    update_header();
}

void ProjectPanel::mark_saved() {
    dirty_ = false;
    update_header();
}

bool ProjectPanel::handle_save_shortcut() {
    if (tabs_->active_index() != 5) return false;
    files_editor_->save_current();
    return true;
}

void ProjectPanel::cycle_subtab(int delta) {
    const size_t n = tabs_->tab_count();
    if (n == 0) return;
    const int count = static_cast<int>(n);
    const auto next = static_cast<size_t>(((static_cast<int>(tabs_->active_index()) + delta) % count + count) % count);
    tabs_->set_active_index(next);
    // Le fichier ouvert dans « Fichiers Yocto » a pu changer (application).
    if (next == 5) files_editor_->reload();
}

void ProjectPanel::on_edit() {
    dirty_ = true;
    update_header();
    if (on_changed_) on_changed_(spec_);
}

void ProjectPanel::reload_all() {
    layer_editor_->reload();
    layers_conf_editor_->reload();
    apps_editor_->reload();
    system_editor_->reload();
    patches_editor_->reload();
}

void ProjectPanel::update_header() {
    dirty_label_->set_text(dirty_ ? "  ● modifications non enregistrées" : "  enregistré");
    dirty_label_->set_foreground(dirty_ ? kWarning : kMuted);

    const auto issues = validate(spec_, env_.poky_branch);
    int errors = 0;
    int warnings = 0;
    const Issue* first = nullptr;
    for (const auto& i : issues) {
        if (i.level == Issue::Level::Error) { ++errors; if (!first) first = &i; }
        else ++warnings;
    }
    if (!first && !issues.empty()) first = &issues.front();

    std::string text = "Layer " + spec_.layer_name + " · image " + spec_.image.name + " · " +
                       std::to_string(spec_.apps.size()) + " app(s) · ";
    if (errors == 0 && warnings == 0) {
        text += "✓ projet valide";
        issues_label_->set_foreground(kSuccess);
    } else {
        text += (errors ? "✗ " + std::to_string(errors) + " erreur(s)" : std::string("✓ 0 erreur")) +
                ", " + std::to_string(warnings) + " avertissement(s) — " + first->where + " : " + first->message;
        issues_label_->set_foreground(errors ? kError : kWarning);
    }
    issues_label_->set_text(text);
}

void ProjectPanel::load_preset() {
    spec_ = formation_preset();
    reload_all();
    on_edit();
    if (on_status_) {
        on_status_("✓ Préréglage « TP formation » chargé (meta-formation, formation-image) : "
                   "Ctrl+S pour l'enregistrer");
    }
}

void ProjectPanel::reset_project() {
    spec_ = ProjectSpec{};
    reload_all();
    on_edit();
    if (on_status_) on_status_("Projet vidé : Ctrl+S pour enregistrer, ou rechargez la configuration pour annuler");
}

} // namespace yocto
