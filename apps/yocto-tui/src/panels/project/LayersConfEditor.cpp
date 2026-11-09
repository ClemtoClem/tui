/* @file panels/project/LayersConfEditor.cpp */

#include "panels/project/Editors.hpp"
#include "project/Presets.hpp"
#include "panels/project/Ui.hpp"

namespace yocto {

using tui::LayoutParams;

LayersConfEditor::LayersConfEditor(ProjectSpec& spec, std::function<void()> on_changed)
    : EditorBase(spec, std::move(on_changed)) {
    using namespace ui;

    layers_ = list();
    layers_->set_on_select([this](size_t i) { load_layer(i); });
    name_      = input("meta-openembedded");
    url_       = input("https://git.openembedded.org/meta-openembedded");
    branch_    = input("vide = même branche que poky");
    sublayers_ = input("meta-oe meta-python (vide = racine du dépôt)");
    enabled_   = std::make_shared<tui::Checkbox>("Utilisée par le projet");

    bind(name_, [this](const std::string& v) {
        if (auto* l = current_layer()) { l->name = v; refresh_layers(layers_->selected_index()); }
    });
    bind(url_, [this](const std::string& v) { if (auto* l = current_layer()) l->url = v; });
    bind(branch_, [this](const std::string& v) { if (auto* l = current_layer()) l->branch = v; });
    bind(sublayers_, [this](const std::string& v) {
        if (auto* l = current_layer()) { l->sublayers = v; refresh_layers(layers_->selected_index()); }
    });
    bind(enabled_, [this](bool v) {
        if (auto* l = current_layer()) { l->enabled = v; refresh_layers(layers_->selected_index()); }
    });

    auto add = button("Ajouter", [this] {
        spec_.extra_layers.push_back(extra_layer_template());
        refresh_layers(spec_.extra_layers.size() - 1);
        changed();
    });
    auto remove = button("Supprimer", [this] {
        const size_t i = layers_->selected_index();
        if (i >= spec_.extra_layers.size()) return;
        spec_.extra_layers.erase(spec_.extra_layers.begin() + static_cast<long>(i));
        refresh_layers(i > 0 ? i - 1 : 0);
        changed();
    });

    auto left = std::make_shared<tui::Vertical>();
    left->add_child(layers_, LayoutParams::stretch(1));
    left->add_child(row({add, remove}), LayoutParams::fixed(1));
    field(*left, "Répertoire sous <lab>/layers/", name_);
    field(*left, "URL du dépôt git", url_);
    field(*left, "Branche", branch_);
    field(*left, "Sous-couches à déclarer dans bblayers.conf", sublayers_);
    left->add_child(enabled_, LayoutParams::fixed(1));
    left->add_child(muted("Clonées par l'étape « Couches externes », puis ajoutées au bloc géré de "
                          "bblayers.conf (sans doublon avec bitbake-layers add-layer).", true),
                    LayoutParams::fixed(3));

    // --- local.conf ---
    rm_work_ = std::make_shared<tui::Checkbox>("INHERIT += \"rm_work\" (supprime les répertoires de travail : économise le disque)");
    bind(rm_work_, [this](bool v) { spec_.rm_work = v; });
    local_conf_ = editor();
    bind(local_conf_, [this](const std::string& v) { spec_.local_conf_extra = v; });

    auto right = std::make_shared<tui::Vertical>();
    right->add_child(rm_work_, LayoutParams::fixed(1));
    right->add_child(muted("Lignes ajoutées telles quelles au bloc géré de local.conf "
                           "(le nom d'hôte de l'onglet « Layer et image » y est ajouté automatiquement). "
                           "Exemples : BB_NUMBER_THREADS = \"4\"   PARALLEL_MAKE = \"-j 4\"   "
                           "PACKAGE_CLASSES = \"package_ipk\"",
                           true),
                     LayoutParams::fixed(4));
    right->add_child(local_conf_, LayoutParams::stretch(1));

    auto columns = std::make_shared<tui::Horizontal>();
    columns->add_child(box("Couches externes", left), LayoutParams::fixed(52));
    columns->add_child(box("Ajouts à local.conf", right), LayoutParams::stretch(1));
    root_ = columns;
    reload();
}

void LayersConfEditor::reload() {
    ui::LoadingGuard guard(loading_);
    rm_work_->set_checked(spec_.rm_work);
    local_conf_->set_text(spec_.local_conf_extra);
    refresh_layers(0);
}

ExtraLayer* LayersConfEditor::current_layer() {
    const size_t i = layers_->selected_index();
    return i < spec_.extra_layers.size() ? &spec_.extra_layers[i] : nullptr;
}

void LayersConfEditor::refresh_layers(size_t select) {
    std::vector<tui::ListItem> items;
    for (const auto& l : spec_.extra_layers) {
        items.push_back(tui::ListItem{l.name.empty() ? "(sans nom)" : l.name, l.sublayers, true,
                                      l.enabled ? "✓" : "○"});
    }
    const bool reselect = layers_->selected_index() != select || items.size() != layers_->items().size();
    layers_->set_items(std::move(items));
    if (reselect) layers_->set_selected_index(select);
    load_layer(layers_->selected_index());
}

void LayersConfEditor::load_layer(size_t index) {
    ui::LoadingGuard guard(loading_);
    const ExtraLayer empty{};
    const ExtraLayer& l = index < spec_.extra_layers.size() ? spec_.extra_layers[index] : empty;
    if (name_->text() != l.name) name_->set_text(l.name);
    if (url_->text() != l.url) url_->set_text(l.url);
    if (branch_->text() != l.branch) branch_->set_text(l.branch);
    if (sublayers_->text() != l.sublayers) sublayers_->set_text(l.sublayers);
    enabled_->set_checked(l.enabled);
}

} // namespace yocto
