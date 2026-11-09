/* @file yocto-tui/panels/RecipesPanel.hpp */

#pragma once

#include "../utils/BitBake.hpp"

#include <tui/widget/Widget.hpp>
#include <tui/widget/layout/Border.hpp>
#include <tui/widgets/Button.hpp>
#include <tui/widgets/Input.hpp>
#include <tui/widgets/Label.hpp>
#include <tui/widgets/ListView.hpp>
#include <tui/widgets/TextArea.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace yocto {

/// Une recette telle que rapportée par `bitbake-layers show-recipes`.
/// On ne stocke que la couche/version la plus prioritaire (la première
/// rencontrée) : afficher toutes les couches fournissant la même recette
/// n'apporte rien dans la vue principale, et la liste devient très
/// longue sur un projet avec meta-oe.
struct RecipeInfo {
    std::string name;
    std::string version;
    std::string layer;
    // Rempli uniquement à la demande (via `bitbake -e <recipe>`) car
    // parser toutes les recettes au démarrage serait trop lent.
    std::string file_path;
    std::string description;
    std::string license;
};

/// Panneau "Recettes" : liste filtrable de toutes les recettes
/// disponibles dans les layers configurés, avec vue de détail à droite
/// et raccourcis vers les opérations BitBake courantes (menuconfig,
/// devtool modify, bitbake <recipe>, bitbake -e <recipe>).
class RecipesPanel {
public:
    explicit RecipesPanel(BitBake& bitbake);

    [[nodiscard]] std::shared_ptr<tui::Widget> root() const { return panel_; }

    /// Relance `bitbake-layers show-recipes` et repeuple la liste.
    /// Bloquant (quelques centaines de ms sur un projet Yocto typique).
    void refresh();

    void set_on_status(std::function<void(const std::string&)> cb) {
        on_status_ = std::move(cb);
    }

private:
    void build_ui();
    void parse_recipes(const std::string& output);
    void apply_filter(const std::string& query);
    void show_recipe_details(size_t filtered_index);

    void run_menuconfig();
    void run_devtool_modify();
    void run_build();
    void run_show_info();

    [[nodiscard]] const RecipeInfo* selected_recipe() const;

    BitBake& bitbake_;

    std::shared_ptr<tui::Border>   panel_;
    std::shared_ptr<tui::Input>    search_input_;
    std::shared_ptr<tui::ListView> recipe_list_;
    std::shared_ptr<tui::TextArea> details_view_;
    std::shared_ptr<tui::Label>    status_label_;
    std::shared_ptr<tui::Button>   menuconfig_button_;
    std::shared_ptr<tui::Button>   devtool_button_;
    std::shared_ptr<tui::Button>   build_button_;
    std::shared_ptr<tui::Button>   info_button_;

    std::vector<RecipeInfo> all_recipes_;
    std::vector<size_t>     filtered_indices_;

    std::function<void(const std::string&)> on_status_;
};

} // namespace yocto