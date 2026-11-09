/* @file panels/project/SystemEditor.cpp */

#include "panels/project/Editors.hpp"
#include "panels/project/Ui.hpp"
#include "project/Presets.hpp"

namespace yocto {

using tui::LayoutParams;

SystemEditor::SystemEditor(ProjectSpec& spec, std::function<void()> on_changed)
    : EditorBase(spec, std::move(on_changed)) {
    using namespace ui;

    // --- Fichiers de la cible ---
    files_ = list();
    files_->set_on_select([this](size_t i) { load_file(i); });
    path_  = input("/etc/mon-appli.conf");
    mode_  = input("0644");
    mode_->set_validator([](char32_t c) { return c >= U'0' && c <= U'7'; });
    owner_ = input("vide = recette <layer>-rootfs-files");
    owner_hint_ = muted("");
    file_enabled_ = std::make_shared<tui::Checkbox>("Inclus dans l'image");
    file_content_ = editor();

    bind(path_, [this](const std::string& v) {
        if (auto* f = current_file()) {
            f->path = v;
            refresh_files(files_->selected_index());
        }
    });
    bind(mode_, [this](const std::string& v) { if (auto* f = current_file()) f->mode = v; });
    bind(owner_, [this](const std::string& v) {
        if (auto* f = current_file()) { f->owner_recipe = v; update_owner_hint(); }
    });
    bind(file_enabled_, [this](bool v) {
        if (auto* f = current_file()) { f->enabled = v; refresh_files(files_->selected_index()); }
    });
    bind(file_content_, [this](const std::string& v) { if (auto* f = current_file()) f->content = v; });

    auto add_file = button("Ajouter", [this] {
        spec_.rootfs_files.push_back(rootfs_template());
        refresh_files(spec_.rootfs_files.size() - 1);
        changed();
    });
    auto remove_file = button("Supprimer", [this] {
        const size_t i = files_->selected_index();
        if (i >= spec_.rootfs_files.size()) return;
        spec_.rootfs_files.erase(spec_.rootfs_files.begin() + static_cast<long>(i));
        refresh_files(i > 0 ? i - 1 : 0);
        changed();
    });
    auto use_owner = button("Utiliser la suggestion", [this] {
        auto* f = current_file();
        if (!f) return;
        const std::string owner = suggested_owner_recipe(f->path);
        if (owner.empty()) return;
        f->owner_recipe = owner;
        { ui::LoadingGuard guard(loading_); owner_->set_text(owner); }
        update_owner_hint();
        changed();
    });

    auto left = std::make_shared<tui::Vertical>();
    left->add_child(files_, LayoutParams::fixed(6));
    left->add_child(row({add_file, remove_file}), LayoutParams::fixed(1));
    field(*left, "Chemin dans la cible", path_);
    field(*left, "Mode", mode_);
    field(*left, "Recette qui fournit déjà ce fichier (bbappend)", owner_);
    left->add_child(owner_hint_, LayoutParams::fixed(1));
    left->add_child(row({use_owner}), LayoutParams::fixed(1));
    left->add_child(file_enabled_, LayoutParams::fixed(1));
    left->add_child(muted("Contenu :"), LayoutParams::fixed(1));
    left->add_child(file_content_, LayoutParams::stretch(1));

    // --- Fragments Kconfig ---
    fragments_ = list();
    fragments_->set_on_select([this](size_t i) { load_fragment(i); });
    frag_recipe_ = input("busybox, linux-yocto, u-boot...");
    frag_name_   = input("fragment.cfg");
    frag_enabled_ = std::make_shared<tui::Checkbox>("Appliqué");
    frag_content_ = editor();

    bind(frag_recipe_, [this](const std::string& v) {
        if (auto* f = current_fragment()) { f->recipe = v; refresh_fragments(fragments_->selected_index()); }
    });
    bind(frag_name_, [this](const std::string& v) {
        if (auto* f = current_fragment()) { f->name = v; refresh_fragments(fragments_->selected_index()); }
    });
    bind(frag_enabled_, [this](bool v) {
        if (auto* f = current_fragment()) { f->enabled = v; refresh_fragments(fragments_->selected_index()); }
    });
    bind(frag_content_, [this](const std::string& v) { if (auto* f = current_fragment()) f->content = v; });

    auto add_frag = button("Ajouter", [this] {
        spec_.fragments.push_back(fragment_template());
        refresh_fragments(spec_.fragments.size() - 1);
        changed();
    });
    auto remove_frag = button("Supprimer", [this] {
        const size_t i = fragments_->selected_index();
        if (i >= spec_.fragments.size()) return;
        spec_.fragments.erase(spec_.fragments.begin() + static_cast<long>(i));
        refresh_fragments(i > 0 ? i - 1 : 0);
        changed();
    });

    auto right = std::make_shared<tui::Vertical>();
    right->add_child(fragments_, LayoutParams::fixed(6));
    right->add_child(row({add_frag, remove_frag}), LayoutParams::fixed(1));
    field(*right, "Recette configurée", frag_recipe_);
    field(*right, "Nom du fragment (.cfg)", frag_name_);
    right->add_child(frag_enabled_, LayoutParams::fixed(1));
    right->add_child(muted("Options Kconfig (CONFIG_X=y, # CONFIG_Y is not set) ; ajoutées au "
                           "SRC_URI de la recette, fusionnées dans son .config.", true),
                     LayoutParams::fixed(2));
    right->add_child(frag_content_, LayoutParams::stretch(1));

    auto columns = std::make_shared<tui::Horizontal>();
    columns->add_child(box("Fichiers Linux de la cible (/etc, scripts d'init...)", left), LayoutParams::stretch(3));
    columns->add_child(box("Fragments de configuration (BusyBox, noyau)", right), LayoutParams::stretch(2));
    root_ = columns;
    reload();
}

void SystemEditor::reload() {
    refresh_files(0);
    refresh_fragments(0);
}

RootfsFile* SystemEditor::current_file() {
    const size_t i = files_->selected_index();
    return i < spec_.rootfs_files.size() ? &spec_.rootfs_files[i] : nullptr;
}

ConfigFragment* SystemEditor::current_fragment() {
    const size_t i = fragments_->selected_index();
    return i < spec_.fragments.size() ? &spec_.fragments[i] : nullptr;
}

void SystemEditor::refresh_files(size_t select) {
    std::vector<tui::ListItem> items;
    for (const auto& f : spec_.rootfs_files) {
        items.push_back(tui::ListItem{f.path.empty() ? "(sans chemin)" : f.path,
                                      f.owner_recipe.empty() ? "recette du projet" : f.owner_recipe, true,
                                      f.enabled ? "✓" : "○"});
    }
    const bool reselect = files_->selected_index() != select || items.size() != files_->items().size();
    files_->set_items(std::move(items));
    if (reselect) files_->set_selected_index(select);
    load_file(files_->selected_index());
}

void SystemEditor::load_file(size_t index) {
    {
        ui::LoadingGuard guard(loading_);
        const RootfsFile empty{};
        const bool ok = index < spec_.rootfs_files.size();
        const RootfsFile& f = ok ? spec_.rootfs_files[index] : empty;
        if (path_->text() != f.path) path_->set_text(f.path);
        if (mode_->text() != f.mode) mode_->set_text(f.mode);
        if (owner_->text() != f.owner_recipe) owner_->set_text(f.owner_recipe);
        file_enabled_->set_checked(f.enabled);
        if (file_content_->text() != f.content) file_content_->set_text(f.content);
        file_content_->set_read_only(!ok);
    }
    update_owner_hint();
}

void SystemEditor::update_owner_hint() {
    auto* f = current_file();
    if (!f) { owner_hint_->set_text(""); return; }
    const std::string owner = suggested_owner_recipe(f->path);
    if (owner.empty()) {
        owner_hint_->set_text(f->owner_recipe.empty() ? "Fichier propre au projet." : "bbappend sur " + f->owner_recipe);
        owner_hint_->set_foreground(kMuted);
    } else if (owner == f->owner_recipe) {
        owner_hint_->set_text("✓ remplace le fichier de " + owner);
        owner_hint_->set_foreground(kSuccess);
    } else {
        owner_hint_->set_text("! déjà fourni par « " + owner + " » : conflit probable");
        owner_hint_->set_foreground(kWarning);
    }
}

void SystemEditor::refresh_fragments(size_t select) {
    std::vector<tui::ListItem> items;
    for (const auto& f : spec_.fragments) {
        items.push_back(tui::ListItem{f.name.empty() ? "(sans nom)" : f.name, f.recipe, true, f.enabled ? "✓" : "○"});
    }
    const bool reselect = fragments_->selected_index() != select || items.size() != fragments_->items().size();
    fragments_->set_items(std::move(items));
    if (reselect) fragments_->set_selected_index(select);
    load_fragment(fragments_->selected_index());
}

void SystemEditor::load_fragment(size_t index) {
    ui::LoadingGuard guard(loading_);
    const ConfigFragment empty{};
    const bool ok = index < spec_.fragments.size();
    const ConfigFragment& f = ok ? spec_.fragments[index] : empty;
    if (frag_recipe_->text() != f.recipe) frag_recipe_->set_text(f.recipe);
    if (frag_name_->text() != f.name) frag_name_->set_text(f.name);
    frag_enabled_->set_checked(f.enabled);
    if (frag_content_->text() != f.content) frag_content_->set_text(f.content);
    frag_content_->set_read_only(!ok);
}

} // namespace yocto
