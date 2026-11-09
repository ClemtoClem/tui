/* @file src/main.cpp
 *
 * yocto-tui — assistant de construction d'images Linux avec Yocto
 * Project, suivant l'organisation du « Yocto-lab » de Christophe
 * Blaess : https://www.blaess.fr/christophe/yocto-lab/
 *
 * F1 — Configuration & Préparation : profils persistés dans
 *   configs/config_yocto_<nom>.ini ; préparation complète du lab
 *   (prérequis, paquets, arborescence, clone de poky, figeage du tag,
 *   oe-init-build-env + local.conf, script env.sh).
 *
 * F2 — Projet : layer personnel et recette d'image, couches externes
 *   (meta-openembedded...), ajouts à local.conf, applications custom
 *   avec leurs sources (scripts, C/C++, Makefile, CMake ; inline,
 *   répertoire hôte ou git), fichiers Linux de la cible, fragments
 *   Kconfig (BusyBox, noyau), patchs (devtool ou .patch), et édition
 *   directe des fichiers Yocto du lab.
 *
 * F3 — Générer & appliquer : aperçu (fichiers nouveaux / modifiés /
 *   supprimés, diff), validation, puis exécution des étapes (couches,
 *   écriture du layer, local.conf / bblayers.conf, bitbake-layers,
 *   devtool), en simulation ou pour de vrai.
 *
 * F4 — Build : bitbake (image, SDK, recettes...) en arrière-plan,
 *   progression des tâches, annulation par SIGINT.
 *
 * F5 — QEMU & Terminal : image produite, runqemu (nographic, slirp,
 *   fenêtre), SSH vers la cible, terminal interactif intégré.
 */

#include "Theme.hpp"

#include "config/ConfigManager.hpp"
#include "config/YoctoConfig.hpp"
#include "panels/ApplyPanel.hpp"
#include "panels/BuildPanel.hpp"
#include "panels/ProjectPanel.hpp"
#include "panels/QemuPanel.hpp"
#include "panels/SetupPanel.hpp"

#include <tui/App.hpp>
#include <tui/core/Text.hpp>
#include <tui/terminal/PosixTerminalBackend.hpp>
#include <tui/widget/layout/Border.hpp>
#include <tui/widget/layout/Horizontal.hpp>
#include <tui/widget/layout/Tabs.hpp>
#include <tui/widget/layout/Vertical.hpp>
#include <tui/widgets/Label.hpp>
#include <tui/widgets/StatusBar.hpp>
#include <tui/widgets/Terminal.hpp>

#include <chrono>
#include <memory>
#include <string>

using namespace yocto;

namespace {

enum TabIndex : size_t { kTabSetup = 0, kTabProject, kTabApply, kTabBuild, kTabQemu, kTabCount };

/// Envoie une ligne complète (texte + Entrée) au PTY du widget Terminal
/// en simulant les KeyEvent que l'utilisateur aurait tapés : Terminal
/// n'expose pas d'API d'écriture directe (voir encode_key() dans la
/// bibliothèque tui).
void terminal_send_line(const std::shared_ptr<tui::Terminal>& term,
                        const std::string& line) {
    if (!term) return;
    for (char32_t ch : tui::TextHelper::decode_utf8(line)) {
        term->on_key(tui::KeyEvent{tui::Key::Char, ch, false, false, false});
    }
    term->on_key(tui::KeyEvent{tui::Key::Enter, 0, false, false, false});
}

class Application {
public:
    explicit Application(tui::App& app) : app_(app) {}

    void build() {
        config_.apply_defaults();

        terminal_ = std::make_shared<tui::Terminal>();
        status_msg_ = std::make_shared<tui::Label>("");
        status_msg_->set_foreground(kMuted);
        auto status = [this](const std::string& m) { set_status(m); };

        setup_panel_ = std::make_shared<SetupPanel>(app_, configs_);
        setup_panel_->set_on_status(status);
        setup_panel_->set_on_config_changed(
            [this](const YoctoConfig& c) { on_config_changed(c); });

        project_panel_ = std::make_shared<ProjectPanel>();
        project_panel_->set_on_status(status);
        project_panel_->set_on_changed([this](const ProjectSpec& p) { on_project_changed(p); });
        project_panel_->set_on_save([this] { save_config(); });

        apply_panel_ = std::make_shared<ApplyPanel>(app_, config_);
        apply_panel_->set_on_status(status);
        apply_panel_->set_on_request_build([this](const std::string& target) {
            switch_tab(kTabBuild);
            build_panel_->start_target(target);
        });

        build_panel_ = std::make_shared<BuildPanel>(app_, config_);
        build_panel_->set_on_status(status);

        qemu_panel_ = std::make_shared<QemuPanel>(config_);
        qemu_panel_->set_on_status(status);
        qemu_panel_->set_on_run_command([this](const std::string& c) { run_in_terminal(c); });

        tabs_ = std::make_shared<tui::Tabs>();
        tabs_->set_bar_style(tui::TabsBarStyle::Boxed);
        tabs_->set_colors(kBorder, tui::Color::White(), kMuted);
        tabs_->add_tab(" F1 Configuration & préparation ", setup_panel_->root());
        tabs_->add_tab(" F2 Projet ", project_panel_->root());
        tabs_->add_tab(" F3 Générer & appliquer ", apply_panel_->root());
        tabs_->add_tab(" F4 Build ", build_panel_->root());
        tabs_->add_tab(" F5 QEMU & terminal ", build_qemu_page());

        auto status_bar = std::make_shared<tui::StatusBar>();
        status_bar->set_left("F1-F5 : onglets | Alt+←/→ : sous-onglets du projet | Tab : focus | "
                             "Ctrl+S : enregistrer | Ctrl+Q : quitter");
        status_bar->set_right("yocto-tui");

        auto root = std::make_shared<tui::Vertical>();
        root->add_child(tabs_, tui::LayoutParams::stretch(1));
        root->add_child(status_msg_, tui::LayoutParams::fixed(1));
        root->add_child(status_bar, tui::LayoutParams::fixed(1));

        app_.set_root(root);
        app_.set_global_key_handler([this](const tui::KeyEvent& e) { return on_global_key(e); });

        // Pompe la sortie du shell embarqué, met à jour l'horloge du
        // build, re-scanne le répertoire de déploiement de l'image et
        // détecte les changements d'onglet (clavier ou souris).
        app_.add_timer(std::chrono::milliseconds(50), true,
                       [this] { terminal_->pump(); });
        app_.add_timer(std::chrono::milliseconds(1000), true,
                       [this] { build_panel_->tick(); });
        app_.add_timer(std::chrono::milliseconds(3000), true,
                       [this] { qemu_panel_->refresh(); });
        app_.add_timer(std::chrono::milliseconds(200), true, [this] {
            if (tabs_->active_index() != last_tab_) {
                last_tab_ = tabs_->active_index();
                on_tab_activated(last_tab_);
            }
        });

        // Reprend la dernière configuration utilisée, sinon la
        // première disponible : la session redémarre où on l'a laissée.
        std::string last = configs_.last_used();
        if (last.empty()) {
            auto names = configs_.list();
            if (!names.empty()) last = names.front();
        }
        setup_panel_->reload(last);
        if (last.empty()) {
            project_panel_->load(config_);
            set_status("Créez ou chargez une configuration (F1), puis décrivez votre projet (F2).");
        }

        terminal_->start();
        app_.focus_manager().set_focus(tabs_);
    }

private:
    std::shared_ptr<tui::Widget> build_qemu_page() {
        auto term_border = std::make_shared<tui::Border>();
        term_border->set_border_style(tui::BorderStyle::Rounded);
        term_border->set_color(kBorder);
        term_border->set_title("Terminal — QEMU nographic : Ctrl+A puis x pour quitter");
        term_border->set_child(terminal_);

        auto row = std::make_shared<tui::Horizontal>();
        row->add_child(qemu_panel_->root(), tui::LayoutParams::fixed(48));
        row->add_child(term_border, tui::LayoutParams::stretch(1));
        return row;
    }

    bool on_global_key(const tui::KeyEvent& e) {
        const bool ctrl_char = e.key == tui::Key::Char && e.ctrl;
        if (ctrl_char && (e.codepoint == U'q' || e.codepoint == U'Q')) {
            app_.quit(0);
            return true;
        }
        if (ctrl_char && (e.codepoint == U's' || e.codepoint == U'S')) {
            if (tabs_->active_index() == kTabProject && project_panel_->handle_save_shortcut()) return true;
            save_config();
            return true;
        }
        // F1..F5 : onglets principaux, quel que soit le widget focalisé.
        static constexpr tui::Key kFKeys[] = {tui::Key::F1, tui::Key::F2, tui::Key::F3, tui::Key::F4, tui::Key::F5};
        for (size_t i = 0; i < kTabCount; ++i) {
            if (e.key == kFKeys[i]) {
                switch_tab(i);
                return true;
            }
        }
        if (e.alt && (e.key == tui::Key::Left || e.key == tui::Key::Right) &&
            tabs_->active_index() == kTabProject) {
            project_panel_->cycle_subtab(e.key == tui::Key::Left ? -1 : 1);
            app_.focus_manager().set_focus(project_panel_->tabs());
            return true;
        }
        return false;
    }

    void switch_tab(size_t index) {
        tabs_->set_active_index(index);
        // Le focus reste sinon sur un widget de l'onglet masqué.
        app_.focus_manager().set_focus(index == kTabQemu ? std::shared_ptr<tui::Widget>(terminal_)
                                                         : std::shared_ptr<tui::Widget>(tabs_));
    }

    void on_tab_activated(size_t index) {
        if (index == kTabApply) apply_panel_->refresh_preview();
    }

    void set_status(const std::string& msg) {
        const bool is_error = msg.rfind("Erreur", 0) == 0 ||
                              msg.rfind("Échec", 0) == 0 ||
                              msg.rfind("✗", 0) == 0;
        const bool is_ok = msg.rfind("✓", 0) == 0;
        status_msg_->set_text(msg);
        status_msg_->set_foreground(is_error ? kError : (is_ok ? kSuccess : kMuted));
    }

    /// Changement de configuration (chargement, création, sauvegarde
    /// depuis l'onglet 1).
    void on_config_changed(const YoctoConfig& cfg) {
        config_ = cfg;
        configs_.set_last_used(cfg.name);
        if (cfg.name == project_panel_->loaded_name() && project_panel_->dirty()) {
            // Même profil rechargé : on garde le projet en cours d'édition.
            config_.project = project_panel_->spec();
            setup_panel_->set_project(config_.project);
            project_panel_->set_environment(config_);
        } else {
            project_panel_->load(config_);
        }
        push_config();
        qemu_panel_->refresh();
    }

    /// Modification dans l'onglet Projet (non encore enregistrée).
    void on_project_changed(const ProjectSpec& spec) {
        config_.project = spec;
        setup_panel_->set_project(spec);
        push_config();
    }

    void push_config() {
        build_panel_->set_config(config_);
        qemu_panel_->set_config(config_);
        apply_panel_->set_config(config_);
    }

    void save_config() {
        config_.project = project_panel_->spec();
        std::string error;
        if (!configs_.save(config_, error)) {
            set_status("Erreur : enregistrement impossible : " + error);
            return;
        }
        project_panel_->mark_saved();
        setup_panel_->set_project(config_.project);
        configs_.set_last_used(config_.name);
        set_status("✓ Configuration enregistrée : " + configs_.path_for(config_.name).string());
    }

    void run_in_terminal(const std::string& cmd) {
        switch_tab(kTabQemu);
        terminal_send_line(terminal_, cmd);
        set_status("Commande envoyée au terminal intégré.");
    }

    tui::App& app_;
    ConfigManager configs_;
    YoctoConfig config_;
    size_t last_tab_ = kTabSetup;

    std::shared_ptr<tui::Tabs>     tabs_;
    std::shared_ptr<tui::Terminal> terminal_;
    std::shared_ptr<tui::Label>    status_msg_;
    std::shared_ptr<SetupPanel>    setup_panel_;
    std::shared_ptr<ProjectPanel>  project_panel_;
    std::shared_ptr<ApplyPanel>    apply_panel_;
    std::shared_ptr<BuildPanel>    build_panel_;
    std::shared_ptr<QemuPanel>     qemu_panel_;
};

} // namespace

int main() {
    tui::App app(std::make_unique<tui::PosixTerminalBackend>());
    Application application(app);
    application.build();
    return app.run();
}
