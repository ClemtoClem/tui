/* apps/yocto-tui/panels/SetupPanel.cpp */
#include "SetupPanel.hpp"

#include "../Theme.hpp"

#include <tui/widget/layout/Horizontal.hpp>
#include <tui/widget/layout/Vertical.hpp>

#include <algorithm>
#include <thread>

using tui::Border;
using tui::BorderStyle;
using tui::Button;
using tui::Checkbox;
using tui::Horizontal;
using tui::Input;
using tui::Label;
using tui::LayoutParams;
using tui::ListItem;
using tui::ListView;
using tui::TextArea;
using tui::Vertical;

namespace yocto {

namespace {

std::string status_prefix(StepStatus s) {
    switch (s) {
        case StepStatus::Pending: return "·";
        case StepStatus::Running: return ">";
        case StepStatus::Done:    return "✓";
        case StepStatus::Failed:  return "✗";
        case StepStatus::Skipped: return "—";
    }
    return "?";
}

std::string step_status_text(StepStatus s) {
    switch (s) {
        case StepStatus::Pending: return "en attente";
        case StepStatus::Running: return "en cours";
        case StepStatus::Done:    return "terminé";
        case StepStatus::Failed:  return "échec";
        case StepStatus::Skipped: return "sauté";
    }
    return "?";
}

// Libellés des étapes affichés avant la première exécution (ils sont
// ensuite remplacés par ceux du SetupRunner).
const std::vector<std::string> kStepLabels = {
    "Vérification des prérequis",
    "Installation des paquets hôte",
    "Locale UTF-8",
    "Restriction AppArmor",
    "Configuration git",
    "Arborescence du lab",
    "Clone de poky",
    "Figeage du tag",
    "Répertoire de build",
    "Script env.sh",
    "Premier build",
};

} // namespace

SetupPanel::SetupPanel(tui::App& app, ConfigManager& configs)
    : app_(app), configs_(configs) {
    build_ui();
    refresh_config_list();
}

void SetupPanel::build_ui() {
    // --- Colonne gauche : configs existantes + actions ---
    config_list_ = std::make_shared<ListView>();
    config_list_->set_colors(kAccent, kFg, kMuted);
    config_list_->set_on_select([this](size_t) { load_selected(); });

    new_button_ = std::make_shared<Button>("Nouvelle");
    new_button_->set_on_click([this] { new_config(); });

    duplicate_button_ = std::make_shared<Button>("Dupliquer");
    duplicate_button_->set_on_click([this] { duplicate_selected(); });

    delete_button_ = std::make_shared<Button>("Supprimer");
    delete_button_->set_on_click([this] { delete_selected(); });

    auto left_buttons = std::make_shared<Horizontal>();
    left_buttons->add_child(new_button_,        LayoutParams{tui::LayoutMode::Auto, 0, tui::CrossAlign::Start});
    left_buttons->add_child(duplicate_button_, LayoutParams{tui::LayoutMode::Auto, 0, tui::CrossAlign::Start});
    left_buttons->add_child(delete_button_,     LayoutParams{tui::LayoutMode::Auto, 0, tui::CrossAlign::Start});

    auto left = std::make_shared<Vertical>();
    left->add_child(config_list_, LayoutParams::stretch(1));
    left->add_child(left_buttons, LayoutParams::fixed(1));

    auto left_panel = std::make_shared<Border>();
    left_panel->set_border_style(BorderStyle::Rounded);
    left_panel->set_color(kBorder);
    left_panel->set_title("Configurations (configs/config_yocto_<nom>.ini)");
    left_panel->set_child(left);

    // --- Colonne centrale : formulaire ---
    name_input_       = std::make_shared<Input>();
    workdir_input_   = std::make_shared<Input>();
    url_input_        = std::make_shared<Input>();
    branch_input_    = std::make_shared<Input>();
    tag_input_        = std::make_shared<Input>();
    machine_input_   = std::make_shared<Input>();
    threads_input_   = std::make_shared<Input>();
    image_input_     = std::make_shared<Input>();
    git_name_input_  = std::make_shared<Input>();
    git_email_input_ = std::make_shared<Input>();

    threads_input_->set_validator([](char32_t c) { return c >= U'0' && c <= U'9'; });

    install_check_     = std::make_shared<Checkbox>("Installer les paquets hôte (sudo)");
    locale_check_     = std::make_shared<Checkbox>("Forcer la locale en_US.UTF-8");
    apparmor_check_   = std::make_shared<Checkbox>("Lever la restriction AppArmor (Ubuntu 24.04)");
    git_check_        = std::make_shared<Checkbox>("Configurer git (user.name / user.email)");
    first_build_check_ = std::make_shared<Checkbox>("Enchaîner le premier build (très long)");

    save_button_ = std::make_shared<Button>("Enregistrer");
    save_button_->set_on_click([this] { save_current(); });

    run_button_ = std::make_shared<Button>("Lancer la préparation");
    run_button_->set_on_click([this] { start_setup(); });

    auto lbl = [](const std::string& s) {
        auto l = std::make_shared<Label>(s);
        l->set_foreground(kMuted);
        return l;
    };

    auto form = std::make_shared<Vertical>();
    form->add_child(lbl("Nom de la configuration"), LayoutParams::fixed(1));
    form->add_child(name_input_, LayoutParams::fixed(1));
    form->add_child(lbl("Répertoire du lab (ex : ~/yocto-lab)"), LayoutParams::fixed(1));
    form->add_child(workdir_input_, LayoutParams::fixed(1));
    form->add_child(lbl("URL Git de Poky"), LayoutParams::fixed(1));
    form->add_child(url_input_, LayoutParams::fixed(1));
    form->add_child(lbl("Branche Poky (ex : scarthgap)"), LayoutParams::fixed(1));
    form->add_child(branch_input_, LayoutParams::fixed(1));
    form->add_child(lbl("Tag à figer (vide = dernier tag de la branche)"), LayoutParams::fixed(1));
    form->add_child(tag_input_, LayoutParams::fixed(1));
    form->add_child(lbl("MACHINE (ex : qemux86-64)"), LayoutParams::fixed(1));
    form->add_child(machine_input_, LayoutParams::fixed(1));
    form->add_child(lbl("Threads (0 = automatique)"), LayoutParams::fixed(1));
    form->add_child(threads_input_, LayoutParams::fixed(1));
    form->add_child(lbl("Recette d'image (ex : core-image-minimal)"), LayoutParams::fixed(1));
    form->add_child(image_input_, LayoutParams::fixed(1));
    form->add_child(lbl("git user.name"), LayoutParams::fixed(1));
    form->add_child(git_name_input_, LayoutParams::fixed(1));
    form->add_child(lbl("git user.email"), LayoutParams::fixed(1));
    form->add_child(git_email_input_, LayoutParams::fixed(1));
    form->add_child(install_check_, LayoutParams::fixed(1));
    form->add_child(locale_check_, LayoutParams::fixed(1));
    form->add_child(apparmor_check_, LayoutParams::fixed(1));
    form->add_child(git_check_, LayoutParams::fixed(1));
    form->add_child(first_build_check_, LayoutParams::fixed(1));
    form->add_child(save_button_, LayoutParams{tui::LayoutMode::Auto, 0, tui::CrossAlign::Start});
    form->add_child(run_button_, LayoutParams{tui::LayoutMode::Auto, 0, tui::CrossAlign::Start});
    form->add_child(std::make_shared<Label>(""), LayoutParams::stretch(1));

    auto form_panel = std::make_shared<Border>();
    form_panel->set_border_style(BorderStyle::Rounded);
    form_panel->set_color(kBorder);
    form_panel->set_title("Paramètres");
    form_panel->set_child(form);

    // --- Colonne droite : étapes + journal ---
    step_list_ = std::make_shared<ListView>();
    step_list_->set_colors(kAccent, kFg, kMuted);

    retry_button_ = std::make_shared<Button>("Relancer l'étape sélectionnée");
    retry_button_->set_on_click([this] { retry_selected_step(); });

    log_view_ = std::make_shared<TextArea>();
    log_view_->set_read_only(true);
    log_view_->set_show_scrollbar(true);
    log_view_->set_accent_color(kAccent);

    auto right = std::make_shared<Vertical>();
    right->add_child(step_list_, LayoutParams::fixed(13));
    right->add_child(retry_button_, LayoutParams{tui::LayoutMode::Auto, 0, tui::CrossAlign::Start});
    right->add_child(log_view_, LayoutParams::stretch(1));

    auto right_panel = std::make_shared<Border>();
    right_panel->set_border_style(BorderStyle::Rounded);
    right_panel->set_color(kBorder);
    right_panel->set_title("Préparation du lab");
    right_panel->set_child(right);

    // --- Assemblage ---
    auto columns = std::make_shared<Horizontal>();
    columns->add_child(left_panel, LayoutParams::fixed(28));
    columns->add_child(form_panel, LayoutParams::fixed(44));
    columns->add_child(right_panel, LayoutParams::stretch(1));

    panel_ = std::make_shared<Border>();
    panel_->set_border_style(BorderStyle::Rounded);
    panel_->set_color(kBorder);
    panel_->set_child(columns);

    // Placeholders des étapes avant toute exécution
    std::vector<ListItem> placeholder;
    for (const auto& label : kStepLabels) {
        placeholder.push_back(ListItem{label, "", false, status_prefix(StepStatus::Pending)});
    }
    step_list_->set_items(std::move(placeholder));
}

// --- Gestion des configurations ----------------------------------------

void SetupPanel::reload(const std::string& name) {
    refresh_config_list();
    if (name.empty()) return;
    auto names = configs_.list();
    for (size_t i = 0; i < names.size(); ++i) {
        if (names[i] == name) {
            config_list_->set_selected_index(i);
            load_selected();
            return;
        }
    }
}

void SetupPanel::refresh_config_list() {
    auto names = configs_.list();
    std::vector<ListItem> items;
    items.reserve(names.size());
    for (const auto& n : names) {
        items.push_back(ListItem{n, "", true, "⚙"});
    }
    config_list_->set_items(std::move(items));
}

void SetupPanel::load_selected() {
    auto names = configs_.list();
    const size_t idx = config_list_->selected_index();
    if (idx >= names.size()) return;

    std::string error;
    auto cfg = configs_.load(names[idx], error);
    if (!cfg) {
        if (on_status_) on_status_("Erreur : chargement impossible : " + error);
        return;
    }
    current_ = *cfg;
    has_config_ = true;
    apply_config_to_form();
    if (on_config_changed_) on_config_changed_(current_);
    if (on_status_) on_status_("✓ Configuration chargée : " + current_.name);
}

void SetupPanel::new_config() {
    // Nom unique auto : "default", "default-2", "default-3", ...
    std::string base = "default";
    std::string name = base;
    int suffix = 1;
    while (configs_.exists(name)) {
        ++suffix;
        name = base + "-" + std::to_string(suffix);
    }

    YoctoConfig fresh;
    fresh.name = name;
    fresh.apply_defaults();
    current_ = fresh;
    has_config_ = true;
    apply_config_to_form();

    std::string error;
    if (!configs_.save(current_, error)) {
        if (on_status_) on_status_("Erreur : création impossible : " + error);
        return;
    }
    refresh_config_list();
    reload(name);
    if (on_config_changed_) on_config_changed_(current_);
    if (on_status_) on_status_("✓ Nouvelle configuration : " + name);
}

void SetupPanel::duplicate_selected() {
    auto names = configs_.list();
    const size_t idx = config_list_->selected_index();
    if (idx >= names.size()) return;

    std::string error;
    auto cfg = configs_.load(names[idx], error);
    if (!cfg) {
        if (on_status_) on_status_("Erreur : duplication impossible : " + error);
        return;
    }

    const std::string base = names[idx] + "-copie";
    std::string name = base;
    int suffix = 1;
    while (configs_.exists(name)) {
        name = base + "-" + std::to_string(++suffix);
    }
    cfg->name = name;

    if (!configs_.save(*cfg, error)) {
        if (on_status_) on_status_("Erreur : duplication impossible : " + error);
        return;
    }
    refresh_config_list();
    reload(name);
    if (on_status_) on_status_("✓ Configuration dupliquée : " + name);
}

void SetupPanel::delete_selected() {
    auto names = configs_.list();
    const size_t idx = config_list_->selected_index();
    if (idx >= names.size()) return;

    std::string error;
    if (!configs_.remove(names[idx], error)) {
        if (on_status_) on_status_("Erreur : suppression impossible : " + error);
        return;
    }
    refresh_config_list();
    auto remaining = configs_.list();
    if (!remaining.empty()) {
        reload(remaining[idx < remaining.size() ? idx : 0]);
    } else {
        has_config_ = false;
        current_ = YoctoConfig{};
        apply_config_to_form();
    }
    if (on_status_) on_status_("✓ Configuration supprimée : " + names[idx]);
}

void SetupPanel::save_current() {
    if (!has_config_) {
        current_ = YoctoConfig{};
        has_config_ = true;
    }
    apply_form_to_config();

    if (!ConfigManager::is_valid_name(current_.name)) {
        if (on_status_) on_status_("Erreur : nom invalide (attendu : lettres, chiffres, _ ou -)");
        return;
    }
    std::string error;
    if (!configs_.save(current_, error)) {
        if (on_status_) on_status_("Erreur : échec de sauvegarde : " + error);
        return;
    }
    refresh_config_list();
    reload(current_.name);
    if (on_config_changed_) on_config_changed_(current_);
    if (on_status_) on_status_("✓ Configuration enregistrée : " + current_.name);
}

// --- Synchronisation formulaire <-> config -----------------------------

void SetupPanel::apply_form_to_config() {
    current_.name          = name_input_->text();
    current_.workdir       = workdir_input_->text();
    current_.poky_url      = url_input_->text();
    current_.poky_branch  = branch_input_->text();
    current_.yocto_tag     = tag_input_->text();
    current_.machine       = machine_input_->text();
    current_.image_recipe  = image_input_->text();
    current_.git_user_name  = git_name_input_->text();
    current_.git_user_email = git_email_input_->text();
    try {
        current_.threads = std::stoi(threads_input_->text());
    } catch (...) {
        current_.threads = 0;
    }
    current_.install_packages = install_check_->checked();
    current_.configure_locale = locale_check_->checked();
    current_.fix_apparmor     = apparmor_check_->checked();
    current_.configure_git    = git_check_->checked();
    current_.first_build      = first_build_check_->checked();
    current_.apply_defaults();
}

void SetupPanel::apply_config_to_form() {
    name_input_->set_text(current_.name);
    workdir_input_->set_text(current_.workdir);
    url_input_->set_text(current_.poky_url);
    branch_input_->set_text(current_.poky_branch);
    tag_input_->set_text(current_.yocto_tag);
    machine_input_->set_text(current_.machine);
    threads_input_->set_text(std::to_string(current_.threads));
    image_input_->set_text(current_.image_recipe);
    git_name_input_->set_text(current_.git_user_name);
    git_email_input_->set_text(current_.git_user_email);
    install_check_->set_checked(current_.install_packages);
    locale_check_->set_checked(current_.configure_locale);
    apparmor_check_->set_checked(current_.fix_apparmor);
    git_check_->set_checked(current_.configure_git);
    first_build_check_->set_checked(current_.first_build);
}

// --- Lancement de la préparation ---------------------------------------

void SetupPanel::start_setup() {
    if (!has_config_) {
        if (on_status_) on_status_("Erreur : aucune configuration sélectionnée");
        return;
    }
    if (setup_running_) {
        if (on_status_) on_status_("Erreur : une préparation est déjà en cours");
        return;
    }

    // Enregistre d'abord l'état courant du formulaire
    save_current();

    log_buffer_.clear();
    log_view_->set_text("");
    append_log("=== Préparation de : " + current_.name + " ===");

    runner_ = std::make_unique<SetupRunner>(current_);
    {
        std::vector<ListItem> items;
        for (const auto& s : runner_->steps()) {
            items.push_back(ListItem{s.label, "", true, status_prefix(StepStatus::Pending)});
        }
        step_list_->set_items(std::move(items));
    }

    if (on_status_) on_status_("Préparation démarrée : " + current_.name);
    setup_running_ = true;

    SetupRunner* runner_ptr = runner_.get();
    runner_ptr->set_on_output([this](const std::string& line) {
        app_.post([this, line] { append_log(line); });
    });
    runner_ptr->set_on_status([this](StepId id, StepStatus s, const std::string& msg) {
        app_.post([this, id, s, msg] {
            if (runner_) {
                for (size_t i = 0; i < runner_->steps().size(); ++i) {
                    if (runner_->steps()[i].id != id) continue;
                    set_step_status(i, s, msg);
                    break;
                }
            }
            if (s == StepStatus::Failed && on_status_) {
                on_status_("Erreur : " + (msg.empty() ? std::string("étape échouée") : msg));
            } else if (s == StepStatus::Done && on_status_ && !msg.empty()) {
                on_status_(msg);
            }
        });
    });

    std::thread([this, runner_ptr] {
        runner_ptr->run();
        app_.post([this] {
            setup_running_ = false;
            append_log("");
            append_log("=== Préparation terminée ===");
            if (on_status_) on_status_("✓ Préparation terminée");
        });
    }).detach();
}

void SetupPanel::retry_selected_step() {
    if (!runner_) {
        if (on_status_) on_status_("Erreur : lancez d'abord une préparation complète");
        return;
    }
    if (setup_running_) {
        if (on_status_) on_status_("Erreur : une préparation est déjà en cours");
        return;
    }
    const size_t idx = step_list_->selected_index();
    if (idx >= runner_->steps().size()) return;
    const StepId id = runner_->steps()[idx].id;

    setup_running_ = true;
    if (on_status_) on_status_("Relance de l'étape...");

    SetupRunner* runner_ptr = runner_.get();
    std::thread([this, runner_ptr, id] {
        runner_ptr->run_step(id);
        app_.post([this] {
            setup_running_ = false;
            append_log("=== Étape relancée ===");
            if (on_status_) on_status_("✓ Étape relancée");
        });
    }).detach();
}

void SetupPanel::set_step_status(size_t index, StepStatus s, const std::string& msg) {
    auto items = step_list_->items();
    if (index >= items.size()) return;
    items[index].prefix_icon = status_prefix(s);
    items[index].detail = step_status_text(s);
    if (!msg.empty()) items[index].detail += " — " + msg;
    step_list_->set_items(std::move(items));
}

void SetupPanel::append_log(const std::string& line) {
    log_buffer_ += line + "\n";
    // Cap pour ne pas saturer un TextArea : on tronque à 5 000 lignes.
    constexpr size_t kMaxLines = 5000;
    size_t lines = std::count(log_buffer_.begin(), log_buffer_.end(), '\n');
    if (lines > kMaxLines) {
        size_t cut = log_buffer_.find('\n', log_buffer_.size() / 3);
        if (cut != std::string::npos) log_buffer_.erase(0, cut + 1);
    }
    log_view_->set_text(log_buffer_);
}

} // namespace yocto