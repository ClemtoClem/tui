/* apps/yocto-tui/panels/QemuPanel.hpp */
#pragma once

#include "../config/YoctoConfig.hpp"

#include <tui/widget/Widget.hpp>
#include <tui/widget/layout/Border.hpp>
#include <tui/widgets/Button.hpp>
#include <tui/widgets/Label.hpp>
#include <tui/widgets/ListView.hpp>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace yocto {

/// État de l'image compilée pour la machine cible, tel que détecté
/// dans <build_dir>/tmp/deploy/images/<machine>.
struct ImageStatus {
    bool ready = false;  // un fichier .qemuboot.conf est présent
    std::filesystem::path deploy_dir;
    std::vector<std::pair<std::string, std::size_t>> files;  // nom, taille
    std::string message;
};

/// Onglet « QEMU » : détecte l'image produite par le build, affiche
/// les fichiers déployés et propose le lancement de runqemu (nographic
/// ou fenêtre graphique) via le terminal intégré de l'onglet 3.
class QemuPanel {
public:
    explicit QemuPanel(YoctoConfig config);

    [[nodiscard]] std::shared_ptr<tui::Widget> root() const { return panel_; }

    /// Appelé par main quand la configuration active change.
    void set_config(const YoctoConfig& cfg);

    /// Rescanne tmp/deploy/images/<machine> et met à jour l'affichage.
    void refresh();

    /// La commande complète (env + runqemu) est envoyée par main au
    /// terminal intégré.
    void set_on_run_command(std::function<void(const std::string&)> cb) {
        on_run_command_ = std::move(cb);
    }
    void set_on_status(std::function<void(const std::string&)> cb) {
        on_status_ = std::move(cb);
    }

private:
    void build_ui();
    [[nodiscard]] ImageStatus detect() const;
    [[nodiscard]] std::string qemu_command(const std::string& mode) const;

    YoctoConfig config_;

    std::shared_ptr<tui::Border>  panel_;
    std::shared_ptr<tui::Label>   machine_label_;
    std::shared_ptr<tui::Label>   ready_label_;
    std::shared_ptr<tui::Label>   deploy_label_;
    std::shared_ptr<tui::ListView> file_list_;
    std::shared_ptr<tui::Button>  nographic_button_;
    std::shared_ptr<tui::Button>  graphical_button_;
    std::shared_ptr<tui::Button>  env_button_;

    std::function<void(const std::string&)> on_run_command_;
    std::function<void(const std::string&)> on_status_;
};

} // namespace yocto