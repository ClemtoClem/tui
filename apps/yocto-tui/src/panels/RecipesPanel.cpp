/* @file panels/RecipesPanel.cpp */

#include "panels/RecipesPanel.hpp"
#include "Theme.hpp"

#include <tui/widget/layout/Horizontal.hpp>
#include <tui/widget/layout/Vertical.hpp>

#include <algorithm>
#include <cctype>
#include <sstream>

using tui::Border;
using tui::BorderStyle;
using tui::Button;
using tui::Horizontal;
using tui::Input;
using tui::Label;
using tui::LayoutParams;
using tui::ListItem;
using tui::ListView;
using tui::ListViewStyle;
using tui::TextArea;
using tui::Vertical;

namespace yocto {

namespace {

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Découpe "meta-oe                 2.2.53" en (layer, version).
// La version peut être absente (recettes sans PV défini) : on retourne
// alors une chaîne vide, plutôt que d'avaler la ligne suivante.
void split_layer_version(const std::string& line, std::string& layer, std::string& version) {
    size_t first = line.find_first_not_of(" \t");
    if (first == std::string::npos) { layer.clear(); version.clear(); return; }
    size_t ws = line.find_first_of(" \t", first);
    if (ws == std::string::npos) {
        layer = line.substr(first);
        version.clear();
        return;
    }
    layer = line.substr(first, ws - first);
    size_t vfirst = line.find_first_not_of(" \t", ws);
    version = (vfirst == std::string::npos) ? std::string{} : line.substr(vfirst);
}

} // namespace

RecipesPanel::RecipesPanel(BitBake& bitbake) : bitbake_(bitbake) {
    build_ui();
}

void RecipesPanel::build_ui() {
    // --- Colonne gauche : recherche + liste des recettes ---
    search_input_ = std::make_shared<Input>();
    search_input_->set_placeholder("Filtrer les recettes...");
    search_input_->set_on_change([this](const std::string& q) { apply_filter(q); });

    recipe_list_ = std::make_shared<ListView>();
    recipe_list_->set_style(ListViewStyle::Detailed);
    recipe_list_->set_colors(kAccent, kFg, kMuted);
    recipe_list_->set_show_scrollbar(true);
    recipe_list_->set_on_select([this](size_t i) { show_recipe_details(i); });

    auto left = std::make_shared<Vertical>();
    left->add_child(search_input_, LayoutParams::fixed(1));
    left->add_child(recipe_list_, LayoutParams::stretch(1));

    auto left_panel = std::make_shared<Border>();
    left_panel->set_border_style(BorderStyle::Rounded);
    left_panel->set_color(kBorder);
    left_panel->set_title("Recettes");
    left_panel->set_child(left);

    // --- Colonne droite : détail + actions ---
    details_view_ = std::make_shared<TextArea>();
    details_view_->set_read_only(true);
    details_view_->set_show_scrollbar(true);
    details_view_->set_accent_color(kAccent);

    menuconfig_button_ = std::make_shared<Button>("menuconfig");
    menuconfig_button_->set_on_click([this] { run_menuconfig(); });

    devtool_button_ = std::make_shared<Button>("devtool modify");
    devtool_button_->set_on_click([this] { run_devtool_modify(); });

    build_button_ = std::make_shared<Button>("Construire");
    build_button_->set_on_click([this] { run_build(); });

    info_button_ = std::make_shared<Button>("Variables (bitbake -e)");
    info_button_->set_on_click([this] { run_show_info(); });

    auto buttons = std::make_shared<Horizontal>();
    buttons->add_child(menuconfig_button_, LayoutParams{tui::LayoutMode::Auto, 0, tui::CrossAlign::Start});
    buttons->add_child(devtool_button_,    LayoutParams{tui::LayoutMode::Auto, 0, tui::CrossAlign::Start});
    buttons->add_child(build_button_,      LayoutParams{tui::LayoutMode::Auto, 0, tui::CrossAlign::Start});
    buttons->add_child(info_button_,       LayoutParams{tui::LayoutMode::Auto, 0, tui::CrossAlign::Start});

    auto right = std::make_shared<Vertical>();
    right->add_child(details_view_, LayoutParams::stretch(1));
    right->add_child(buttons, LayoutParams::fixed(1));

    auto right_panel = std::make_shared<Border>();
    right_panel->set_border_style(BorderStyle::Rounded);
    right_panel->set_color(kBorder);
    right_panel->set_title("Détails de la recette");
    right_panel->set_child(right);

    auto columns = std::make_shared<Horizontal>();
    columns->add_child(left_panel, LayoutParams::fixed(38));
    columns->add_child(right_panel, LayoutParams::stretch(1));

    status_label_ = std::make_shared<Label>("Prêt");
    status_label_->set_foreground(kMuted);

    auto outer = std::make_shared<Vertical>();
    outer->add_child(columns, LayoutParams::stretch(1));
    outer->add_child(status_label_, LayoutParams::fixed(1));

    panel_ = std::make_shared<Border>();
    panel_->set_border_style(BorderStyle::Rounded);
    panel_->set_color(kBorder);
    panel_->set_child(outer);
}

// --- Chargement et parsing ----------------------------------------------

void RecipesPanel::refresh() {
    status_label_->set_foreground(kMuted);
    status_label_->set_text("Chargement des recettes (bitbake-layers show-recipes)...");

    // Ne PAS passer par run() : bitbake-layers n'est pas une sous-commande
    // de bitbake, mais un binaire à part à lancer dans l'environnement.
    auto result = bitbake_.run_shell("bitbake-layers show-recipes");
    if (result.exit_code != 0) {
        status_label_->set_foreground(kError);
        status_label_->set_text("Échec de bitbake-layers show-recipes (voir Terminal)");
        if (on_status_) on_status_(result.output);
        return;
    }

    parse_recipes(result.output);
    status_label_->set_foreground(kMuted);
    status_label_->set_text(std::to_string(all_recipes_.size()) + " recettes disponibles");
}

void RecipesPanel::parse_recipes(const std::string& output) {
    all_recipes_.clear();
    std::istringstream iss(output);
    std::string line;

    // `bitbake-layers show-recipes` produit plusieurs sections
    // ("=== Skipped recipes ===", "=== Available recipes ===",
    //  "=== Matching recipes ==="). Seule la section "Available" nous
    // intéresse : c'est elle qui contient nom/layer/version.
    bool in_available = false;
    RecipeInfo current;
    bool has_current = false;

    auto flush = [&] {
        if (has_current && !current.name.empty()) all_recipes_.push_back(current);
        current = RecipeInfo{};
        has_current = false;
    };

    while (std::getline(iss, line)) {
        if (!line.empty() && line.rfind("===", 0) == 0) {
            flush();
            in_available = line.find("Available recipes") != std::string::npos;
            continue;
        }
        if (!in_available) continue;
        if (line.empty()) continue;

        // Ligne non indentée = nouveau nom de recette (terminée ou non
        // par ':' selon les versions de bitbake-layers).
        if (line[0] != ' ' && line[0] != '\t') {
            flush();
            has_current = true;
            current.name = line;
            if (!current.name.empty() && current.name.back() == ':') current.name.pop_back();
            continue;
        }

        // Ligne indentée = "<layer>  <version>". On garde la première
        // (la plus prioritaire) et on ignore les suivantes.
        if (has_current && current.layer.empty()) {
            split_layer_version(line, current.layer, current.version);
        }
    }
    flush();

    // Ré-applique le filtre courant (peut être vide si l'utilisateur
    // n'a encore rien tapé dans la barre de recherche).
    const std::string query = search_input_ ? search_input_->text() : std::string{};
    apply_filter(query);
}

// --- Filtrage et sélection -----------------------------------------------

void RecipesPanel::apply_filter(const std::string& query) {
    const std::string lower_q = to_lower(query);

    filtered_indices_.clear();
    std::vector<ListItem> items;
    items.reserve(all_recipes_.size());

    for (size_t i = 0; i < all_recipes_.size(); ++i) {
        const RecipeInfo& r = all_recipes_[i];
        if (!lower_q.empty() && to_lower(r.name).find(lower_q) == std::string::npos) continue;
        filtered_indices_.push_back(i);

        std::string detail = r.layer;
        if (!r.version.empty()) {
            if (!detail.empty()) detail += "  ";
            detail += r.version;
        }
        items.push_back(ListItem{r.name, detail, /*selectable=*/true, /*prefix=*/""});
    }

    recipe_list_->set_items(std::move(items));

    if (filtered_indices_.empty()) {
        details_view_->set_text(all_recipes_.empty()
            ? "Aucune recette chargée — utilisez « Rafraîchir » (appuyez sur F5).\n"
            : "Aucune recette ne correspond au filtre « " + query + " ».");
        return;
    }

    // set_items() a pu laisser la sélection hors bornes (elle est
    // re-clampée en interne), mais n'a pas déclenché on_select() :
    // on force donc un affichage initial cohérent.
    recipe_list_->set_selected_index(0);
}

void RecipesPanel::show_recipe_details(size_t filtered_index) {
    if (filtered_index >= filtered_indices_.size()) return;
    const RecipeInfo& r = all_recipes_[filtered_indices_[filtered_index]];

    std::ostringstream oss;
    oss << "Recette   : " << r.name << "\n";
    oss << "Layer     : " << (r.layer.empty() ? "(inconnu)" : r.layer) << "\n";
    oss << "Version   : " << (r.version.empty() ? "(non définie)" : r.version) << "\n";
    if (!r.file_path.empty())   oss << "Fichier   : " << r.file_path << "\n";
    if (!r.license.empty())     oss << "Licence   : " << r.license << "\n";
    if (!r.description.empty()) oss << "\nDescription :\n" << r.description << "\n";

    oss << "\nActions disponibles :\n";
    oss << "  • menuconfig        : configuration graphique (Kconfig) —\n";
    oss << "                        pertinent pour virtual/kernel, u-boot,\n";
    oss << "                        busybox ou toute recette avec un menuconfig.\n";
    oss << "  • devtool modify    : extrait les sources dans un workspace\n";
    oss << "                        éditable (build/workspace/sources/).\n";
    oss << "  • Construire        : lance `bitbake " << r.name << "`.\n";
    oss << "  • Variables         : `bitbake -e " << r.name << "` (tronqué).\n";

    details_view_->set_text(oss.str());
}

const RecipeInfo* RecipesPanel::selected_recipe() const {
    const size_t idx = recipe_list_->selected_index();
    if (idx >= filtered_indices_.size()) return nullptr;
    return &all_recipes_[filtered_indices_[idx]];
}

// --- Actions ------------------------------------------------------------

void RecipesPanel::run_menuconfig() {
    const RecipeInfo* r = selected_recipe();
    if (!r) return;

    status_label_->set_foreground(kMuted);
    status_label_->set_text("Lancement de menuconfig pour " + r->name + "...");
    if (on_status_) on_status_("menuconfig : " + r->name + " (interactif — utilisez le Terminal)");

    // menuconfig est interactif : dans notre architecture, la bonne
    // pratique est de basculer l'utilisateur sur l'onglet Terminal et
    // d'y injecter la commande. On se contente ici d'un run bloquant qui
    // produira l'erreur "no tty" si aucun terminal n'est attaché, ce qui
    // est exactement ce qu'on veut signaler à l'utilisateur.
    auto result = bitbake_.run({"-c", "menuconfig", r->name});

    const bool ok = result.exit_code == 0;
    status_label_->set_foreground(status_color(result.exit_code));
    status_label_->set_text(ok
        ? "menuconfig terminé pour " + r->name
        : "menuconfig échoué (interactif : lancez-le depuis le Terminal)");

    if (!ok && on_status_) on_status_("bitbake -c menuconfig " + r->name);
}

void RecipesPanel::run_devtool_modify() {
    const RecipeInfo* r = selected_recipe();
    if (!r) return;

    status_label_->set_foreground(kMuted);
    status_label_->set_text("devtool modify " + r->name + "...");

    auto result = bitbake_.run_shell("devtool modify " + r->name);
    const bool ok = result.exit_code == 0;
    status_label_->set_foreground(status_color(result.exit_code));
    status_label_->set_text(ok
        ? "Workspace devtool créé : build/workspace/sources/" + r->name
        : "devtool modify a échoué pour " + r->name);

    if (!ok && on_status_) on_status_(result.output);
}

void RecipesPanel::run_build() {
    const RecipeInfo* r = selected_recipe();
    if (!r) return;

    status_label_->set_foreground(kMuted);
    status_label_->set_text("Construction de " + r->name + "...");

    auto result = bitbake_.run({r->name});
    const bool ok = result.exit_code == 0;
    status_label_->set_foreground(status_color(result.exit_code));
    status_label_->set_text(ok
        ? "Construction terminée : " + r->name
        : "Construction échouée : " + r->name);
}

void RecipesPanel::run_show_info() {
    const RecipeInfo* r = selected_recipe();
    if (!r) return;

    status_label_->set_foreground(kMuted);
    status_label_->set_text("Lecture des variables de " + r->name + "...");

    auto result = bitbake_.run({"-e", r->name});
    if (result.exit_code != 0) {
        status_label_->set_foreground(kError);
        status_label_->set_text("bitbake -e " + r->name + " a échoué");
        return;
    }

    // `bitbake -e` produit facilement plusieurs milliers de lignes. On
    // tronque à 500 pour garder la vue détail réactive — l'utilisateur
    // qui a besoin de la sortie intégrale utilisera le Terminal.
    constexpr size_t kMaxLines = 500;
    std::istringstream iss(result.output);
    std::ostringstream oss;
    std::string line;
    size_t count = 0;
    while (std::getline(iss, line) && count < kMaxLines) {
        oss << line << "\n";
        ++count;
    }
    if (count == kMaxLines) {
        oss << "\n[--- tronqué à " << kMaxLines << " lignes — "
            << result.output.size() << " octets au total ---]\n";
    }
    details_view_->set_text(oss.str());

    status_label_->set_foreground(kMuted);
    status_label_->set_text("Variables de " + r->name + " affichées (tronquées)");
}

} // namespace yocto