/* apps/yocto-tui/main.cpp
 *
 * yocto-tui — assistant de construction d'images Linux avec Yocto
 * Project, suivant l'organisation du « Yocto-lab » de Christophe
 * Blaess : https://www.blaess.fr/christophe/yocto-lab/
 *
 * Onglet 1 — Configuration & Préparation : profils persistés dans
 *   configs/config_yocto_<nom>.ini (création, chargement,
 *   duplication, sauvegarde, suppression) ; préparation complète du
 *   lab (prérequis, paquets, arborescence layers/ + builds/ + cache/,
 *   clone de poky, figeage du tag, oe-init-build-env + local.conf,
 *   script env.sh).
 *
 * Onglet 2 — Build : bitbake <image> en arrière-plan, progression des
 *   tâches, temps écoulé, annulation par SIGINT.
 *
 * Onglet 3 — QEMU & Terminal : détection de l'image produite dans
 *   tmp/deploy/images/<machine>, lancement de runqemu (nographic ou
 *   fenêtre) dans un terminal interactif intégré.
 */

#include "Theme.hpp"

#include "config/ConfigManager.hpp"
#include "config/YoctoConfig.hpp"
#include "panels/BuildPanel.hpp"
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

        setup_panel_ = std::make_shared<SetupPanel>(app_, configs_);
        setup_panel_->set_on_status([this](const std::string& m) { set_status(m); });
        setup_panel_->set_on_config_changed(
            [this](const YoctoConfig& c) { on_config_changed(c); });

        build_panel_ = std::make_shared<BuildPanel>(app_, config_);
        build_panel_->set_on_status([this](const std::string& m) { set_status(m); });

        qemu_panel_ = std::make_shared<QemuPanel>(config_);
        qemu_panel_->set_on_status([this](const std::string& m) { set_status(m); });
        qemu_panel_->set_on_run_command([this](const std::string& c) { run_in_terminal(c); });

        tabs_ = std::make_shared<tui::Tabs>();
        tabs_->set_bar_style(tui::TabsBarStyle::Boxed);
        tabs_->set_colors(kBorder, tui::Color::White(), kMuted);
        tabs_->add_tab(" 1. Configuration & Préparation ", setup_panel_->root());
        tabs_->add_tab(" 2. Build ", build_panel_->root());
        tabs_->add_tab(" 3. QEMU & Terminal ", build_qemu_page());

        auto status_bar = std::make_shared<tui::StatusBar>();
        status_bar->set_left("Ctrl+←/Ctrl+→ : onglets | Tab : focus suivant | Ctrl+Q : quitter");
        status_bar->set_right("yocto-tui");

        auto root = std::make_shared<tui::Vertical>();
        root->add_child(tabs_, tui::LayoutParams::stretch(1));
        root->add_child(status_msg_, tui::LayoutParams::fixed(1));
        root->add_child(status_bar, tui::LayoutParams::fixed(1));

        app_.set_root(root);

        // Raccourci global : Ctrl+Q quitte depuis n'importe quel widget.
        app_.set_global_key_handler([this](const tui::KeyEvent& e) {
            if (e.key == tui::Key::Char && e.ctrl &&
                (e.codepoint == U'q' || e.codepoint == U'Q')) {
                app_.quit(0);
                return true;
            }
            return false;
        });

        // Pompe la sortie du shell embarqué, met à jour l'horloge du
        // build et re-scanne le répertoire de déploiement de l'image.
        app_.add_timer(std::chrono::milliseconds(50), true,
                       [this] { terminal_->pump(); });
        app_.add_timer(std::chrono::milliseconds(1000), true,
                       [this] { build_panel_->tick(); });
        app_.add_timer(std::chrono::milliseconds(3000), true,
                       [this] { qemu_panel_->refresh(); });

        // Reprend la dernière configuration utilisée, sinon la
        // première disponible : la session redémarre où on l'a laissée.
        std::string last = configs_.last_used();
        if (last.empty()) {
            auto names = configs_.list();
            if (!names.empty()) last = names.front();
        }
        setup_panel_->reload(last);
        if (last.empty()) {
            set_status("Créez ou chargez une configuration dans l'onglet 1 pour commencer.");
        }

        terminal_->start();
        app_.focus_manager().set_focus(terminal_);
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

    void set_status(const std::string& msg) {
        const bool is_error = msg.rfind("Erreur", 0) == 0 ||
                              msg.rfind("Échec", 0) == 0 ||
                              msg.rfind("✗", 0) == 0;
        const bool is_ok = msg.rfind("✓", 0) == 0;
        status_msg_->set_text(msg);
        status_msg_->set_foreground(is_error ? kError : (is_ok ? kSuccess : kMuted));
    }

    void on_config_changed(const YoctoConfig& cfg) {
        config_ = cfg;
        configs_.set_last_used(cfg.name);
        build_panel_->set_config(cfg);
        qemu_panel_->set_config(cfg);
        qemu_panel_->refresh();
    }

    void run_in_terminal(const std::string& cmd) {
        tabs_->set_active_index(2);
        app_.focus_manager().set_focus(terminal_);
        terminal_send_line(terminal_, cmd);
        set_status("Commande envoyée au terminal intégré.");
    }

    tui::App& app_;
    ConfigManager configs_;
    YoctoConfig config_;

    std::shared_ptr<tui::Tabs>     tabs_;
    std::shared_ptr<tui::Terminal> terminal_;
    std::shared_ptr<tui::Label>   status_msg_;
    std::shared_ptr<SetupPanel>  setup_panel_;
    std::shared_ptr<BuildPanel>  build_panel_;
    std::shared_ptr<QemuPanel>   qemu_panel_;
};

} // namespace

int main() {
    tui::App app(std::make_unique<tui::PosixTerminalBackend>());
    Application application(app);
    application.build();
    return app.run();
}