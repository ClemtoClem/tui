/* @file panels/project/PatchesEditor.cpp */

#include "panels/project/Editors.hpp"
#include "panels/project/Ui.hpp"
#include "project/Presets.hpp"

namespace yocto {

using tui::LayoutParams;

PatchesEditor::PatchesEditor(ProjectSpec& spec, std::function<void()> on_changed)
    : EditorBase(spec, std::move(on_changed)) {
    using namespace ui;

    patches_ = list();
    patches_->set_on_select([this](size_t i) { load_patch(i); });

    recipe_   = input("bc");
    name_     = input("identifiant ou nom du .patch");
    file_     = input("vide = recherche dans toutes les sources");
    search_   = input("texte exact à remplacer");
    replace_  = input("texte de remplacement");
    message_  = input("sujet du commit (en-tête du patch)");
    upstream_ = input("Inappropriate [configuration locale]");
    method_ = std::make_shared<tui::RadioSet>();
    method_->set_options({"devtool : rechercher / remplacer", "Fichier .patch fourni"});
    replace_all_ = std::make_shared<tui::Checkbox>("Remplacer toutes les occurrences");
    enabled_     = std::make_shared<tui::Checkbox>("Patch actif");
    patch_content_ = editor();

    bind(recipe_, [this](const std::string& v) {
        if (auto* p = current_patch()) { p->recipe = v; refresh_patches(patches_->selected_index()); }
    });
    bind(name_, [this](const std::string& v) {
        if (auto* p = current_patch()) { p->name = v; refresh_patches(patches_->selected_index()); }
    });
    bind(file_, [this](const std::string& v) { if (auto* p = current_patch()) p->file = v; });
    bind(search_, [this](const std::string& v) { if (auto* p = current_patch()) p->search = v; });
    bind(replace_, [this](const std::string& v) { if (auto* p = current_patch()) p->replace = v; });
    bind(message_, [this](const std::string& v) { if (auto* p = current_patch()) p->message = v; });
    bind(upstream_, [this](const std::string& v) { if (auto* p = current_patch()) p->upstream_status = v; });
    bind(method_, [this](size_t i) {
        if (auto* p = current_patch()) {
            p->method = static_cast<PatchMethod>(i);
            refresh_patches(patches_->selected_index());
        }
    });
    bind(replace_all_, [this](bool v) { if (auto* p = current_patch()) p->replace_all = v; });
    bind(enabled_, [this](bool v) {
        if (auto* p = current_patch()) { p->enabled = v; refresh_patches(patches_->selected_index()); }
    });
    bind(patch_content_, [this](const std::string& v) { if (auto* p = current_patch()) p->patch_content = v; });

    auto add = button("Ajouter", [this] {
        spec_.patches.push_back(patch_template());
        refresh_patches(spec_.patches.size() - 1);
        changed();
    });
    auto remove = button("Supprimer", [this] {
        const size_t i = patches_->selected_index();
        if (i >= spec_.patches.size()) return;
        spec_.patches.erase(spec_.patches.begin() + static_cast<long>(i));
        refresh_patches(i > 0 ? i - 1 : 0);
        changed();
    });

    auto left = std::make_shared<tui::Vertical>();
    left->add_child(patches_, LayoutParams::stretch(1));
    left->add_child(row({add, remove}), LayoutParams::fixed(1));

    auto [scroll, form] = scroll_form();
    field(*form, "Recette à patcher", recipe_);
    field(*form, "Nom", name_);
    field(*form, "Méthode", method_, 2);
    form->add_child(muted(""), LayoutParams::fixed(1));
    form->add_child(muted("─ devtool ─"), LayoutParams::fixed(1));
    field(*form, "Fichier à modifier (relatif aux sources)", file_);
    field(*form, "Rechercher", search_);
    field(*form, "Remplacer par", replace_);
    form->add_child(replace_all_, LayoutParams::fixed(1));
    field(*form, "Message de commit", message_);
    form->add_child(muted(""), LayoutParams::fixed(1));
    field(*form, "Upstream-Status (obligatoire pour la QA)", upstream_);
    form->add_child(enabled_, LayoutParams::fixed(1));

    auto right = std::make_shared<tui::Vertical>();
    right->add_child(muted("devtool : modify → remplacement → git commit → finish dans le layer ; les "
                           "patchs produits par devtool ne sont jamais écrasés par la génération.", true),
                     LayoutParams::fixed(2));
    right->add_child(muted("Fichier .patch : collez un diff unifié (git format-patch) ; il est ajouté au "
                           "SRC_URI par un bbappend. Upstream-Status est ajouté s'il manque.", true),
                     LayoutParams::fixed(2));
    right->add_child(patch_content_, LayoutParams::stretch(1));

    auto columns = std::make_shared<tui::Horizontal>();
    columns->add_child(box("Patchs", left), LayoutParams::fixed(30));
    columns->add_child(box("Modification", scroll), LayoutParams::fixed(50));
    columns->add_child(box("Contenu du .patch", right), LayoutParams::stretch(1));
    root_ = columns;
    reload();
}

void PatchesEditor::reload() { refresh_patches(0); }

PatchSpec* PatchesEditor::current_patch() {
    const size_t i = patches_->selected_index();
    return i < spec_.patches.size() ? &spec_.patches[i] : nullptr;
}

void PatchesEditor::refresh_patches(size_t select) {
    std::vector<tui::ListItem> items;
    for (const auto& p : spec_.patches) {
        items.push_back(tui::ListItem{p.name.empty() ? "(sans nom)" : p.name,
                                      p.recipe + (p.method == PatchMethod::Devtool ? " · devtool" : " · .patch"),
                                      true, p.enabled ? "✓" : "○"});
    }
    const bool reselect = patches_->selected_index() != select || items.size() != patches_->items().size();
    patches_->set_items(std::move(items));
    if (reselect) patches_->set_selected_index(select);
    load_patch(patches_->selected_index());
}

void PatchesEditor::load_patch(size_t index) {
    ui::LoadingGuard guard(loading_);
    const PatchSpec empty{};
    const bool ok = index < spec_.patches.size();
    const PatchSpec& p = ok ? spec_.patches[index] : empty;
    auto set = [](const std::shared_ptr<tui::Input>& in, const std::string& v) {
        if (in->text() != v) in->set_text(v);
    };
    set(recipe_, p.recipe);
    set(name_, p.name);
    set(file_, p.file);
    set(search_, p.search);
    set(replace_, p.replace);
    set(message_, p.message);
    set(upstream_, p.upstream_status);
    method_->set_selected_index(static_cast<size_t>(p.method));
    replace_all_->set_checked(p.replace_all);
    enabled_->set_checked(p.enabled);
    if (patch_content_->text() != p.patch_content) patch_content_->set_text(p.patch_content);
    patch_content_->set_read_only(!ok);
}

} // namespace yocto
