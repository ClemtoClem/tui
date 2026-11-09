/* @file panels/project/LayerImageEditor.cpp */

#include "panels/project/Editors.hpp"
#include "panels/project/Ui.hpp"

#include <cstdlib>

namespace yocto {

using tui::LayoutParams;

namespace {
bool is_digit(char32_t c) { return c >= U'0' && c <= U'9'; }
} // namespace

LayerImageEditor::LayerImageEditor(ProjectSpec& spec, std::function<void()> on_changed)
    : EditorBase(spec, std::move(on_changed)) {
    using namespace ui;

    layer_name_    = input("meta-monprojet");
    priority_      = input("6");
    depends_       = input("core");
    image_name_    = input("mon-image");
    base_          = input("core-image-minimal");
    summary_       = input("Image de mon produit");
    hostname_      = input("ma-cible");
    packages_      = input("strace htop");
    features_      = input("ssh-server-dropbear");
    root_password_ = input("vide : voir la case ci-dessous");
    empty_root_    = std::make_shared<tui::Checkbox>("root sans mot de passe (développement)");
    priority_->set_validator(is_digit);

    bind(layer_name_, [this](const std::string& v) { spec_.layer_name = v; });
    bind(priority_, [this](const std::string& v) { spec_.layer_priority = std::atoi(v.c_str()); });
    bind(depends_, [this](const std::string& v) { spec_.layer_depends = v; });
    bind(image_name_, [this](const std::string& v) { spec_.image.name = v; });
    bind(base_, [this](const std::string& v) { spec_.image.base = v; });
    bind(summary_, [this](const std::string& v) { spec_.image.summary = v; });
    bind(hostname_, [this](const std::string& v) { spec_.image.hostname = v; });
    bind(packages_, [this](const std::string& v) { spec_.image.extra_packages = v; });
    bind(features_, [this](const std::string& v) { spec_.image.features = v; });
    bind(root_password_, [this](const std::string& v) { spec_.image.root_password = v; });
    bind(empty_root_, [this](bool v) { spec_.image.empty_root_password = v; });

    auto [scroll, form] = scroll_form();
    form->add_child(muted("─ Layer personnel ─"), LayoutParams::fixed(1));
    field(*form, "Nom (créé sous <lab>/layers/, ex : meta-formation)", layer_name_);
    field(*form, "Priorité BBFILE_PRIORITY (1-99)", priority_);
    field(*form, "LAYERDEPENDS (ex : core openembedded-layer)", depends_);
    form->add_child(muted(""), LayoutParams::fixed(1));
    form->add_child(muted("─ Recette d'image ─"), LayoutParams::fixed(1));
    field(*form, "Nom de l'image (bitbake <nom>)", image_name_);
    field(*form, "Image de base (core-image-minimal, -base, -full-cmdline...)", base_);
    field(*form, "Description (SUMMARY)", summary_);
    field(*form, "Nom d'hôte de la cible (/etc/hostname)", hostname_);
    field(*form, "Paquets existants ajoutés (IMAGE_INSTALL)", packages_);
    field(*form, "IMAGE_FEATURES (ssh-server-dropbear, tools-debug...)", features_);
    field(*form, "Mot de passe root (hashé SHA-512 à la génération)", root_password_);
    form->add_child(empty_root_, LayoutParams::fixed(1));
    form->add_child(muted(""), LayoutParams::fixed(1));
    form->add_child(muted("Les applications du projet marquées « dans l'image » sont ajoutées "
                          "automatiquement à IMAGE_INSTALL.", true),
                    LayoutParams::fixed(2));

    // --- Comptes ---
    users_ = list();
    users_->set_on_select([this](size_t i) { load_user(i); });
    user_name_     = input("stagiaire");
    user_password_ = input("mot de passe");
    user_groups_   = input("audio,video");
    bind(user_name_, [this](const std::string& v) {
        if (auto* u = current_user()) { u->name = v; refresh_users(users_->selected_index()); }
    });
    bind(user_password_, [this](const std::string& v) { if (auto* u = current_user()) u->password = v; });
    bind(user_groups_, [this](const std::string& v) { if (auto* u = current_user()) u->groups = v; });

    auto add = button("Ajouter", [this] {
        spec_.image.users.push_back({"utilisateur", "", ""});
        refresh_users(spec_.image.users.size() - 1);
        changed();
    });
    auto remove = button("Supprimer", [this] {
        const size_t i = users_->selected_index();
        if (i >= spec_.image.users.size()) return;
        spec_.image.users.erase(spec_.image.users.begin() + static_cast<long>(i));
        refresh_users(i > 0 ? i - 1 : 0);
        changed();
    });

    auto right = std::make_shared<tui::Vertical>();
    right->add_child(users_, LayoutParams::stretch(1));
    right->add_child(row({add, remove}), LayoutParams::fixed(1));
    field(*right, "Identifiant", user_name_);
    field(*right, "Mot de passe", user_password_);
    field(*right, "Groupes supplémentaires (useradd -G)", user_groups_);
    right->add_child(muted("Créés par la classe extrausers. Les mots de passe sont stockés en clair "
                           "dans la config et hashés dans la recette.", true),
                     LayoutParams::fixed(3));

    auto columns = std::make_shared<tui::Horizontal>();
    columns->add_child(box("Layer et image", scroll), LayoutParams::stretch(1));
    columns->add_child(box("Comptes de la cible", right), LayoutParams::fixed(44));
    root_ = columns;
    reload();
}

void LayerImageEditor::reload() {
    ui::LoadingGuard guard(loading_);
    layer_name_->set_text(spec_.layer_name);
    priority_->set_text(std::to_string(spec_.layer_priority));
    depends_->set_text(spec_.layer_depends);
    image_name_->set_text(spec_.image.name);
    base_->set_text(spec_.image.base);
    summary_->set_text(spec_.image.summary);
    hostname_->set_text(spec_.image.hostname);
    packages_->set_text(spec_.image.extra_packages);
    features_->set_text(spec_.image.features);
    root_password_->set_text(spec_.image.root_password);
    empty_root_->set_checked(spec_.image.empty_root_password);
    refresh_users(0);
}

UserAccount* LayerImageEditor::current_user() {
    const size_t i = users_->selected_index();
    return i < spec_.image.users.size() ? &spec_.image.users[i] : nullptr;
}

void LayerImageEditor::refresh_users(size_t select) {
    std::vector<tui::ListItem> items;
    for (const auto& u : spec_.image.users) {
        items.push_back(tui::ListItem{u.name.empty() ? "(sans nom)" : u.name, u.groups, true, "•"});
    }
    const bool reselect = users_->selected_index() != select || items.size() != users_->items().size();
    users_->set_items(std::move(items));
    if (reselect) users_->set_selected_index(select);
    load_user(users_->selected_index());
}

void LayerImageEditor::load_user(size_t index) {
    ui::LoadingGuard guard(loading_);
    const bool ok = index < spec_.image.users.size();
    const UserAccount empty{};
    const UserAccount& u = ok ? spec_.image.users[index] : empty;
    // Ne réécrit pas le champ en cours de frappe (évite de déplacer le curseur).
    if (user_name_->text() != u.name) user_name_->set_text(u.name);
    if (user_password_->text() != u.password) user_password_->set_text(u.password);
    if (user_groups_->text() != u.groups) user_groups_->set_text(u.groups);
}

} // namespace yocto
