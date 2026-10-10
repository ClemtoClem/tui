#include "tabs/FilesystemsTab.hpp"
#include "tabs/ProcessesTab.hpp"
#include "tabs/ResourcesTab.hpp"

#include <tui/App.hpp>
#include <tui/terminal/PosixTerminalBackend.hpp>
#include <tui/widget/layout/Tabs.hpp>
#include <tui/widget/layout/Vertical.hpp>
#include <tui/widgets/StatusBar.hpp>

#include <chrono>
#include <memory>

int main() {
    using namespace tui;
    using namespace sysmon;

    App app(std::make_unique<PosixTerminalBackend>());

    auto processes_tab = std::make_shared<ProcessesTab>();
    auto resources_tab = std::make_shared<ResourcesTab>();
    auto fs_tab = std::make_shared<FilesystemsTab>();

    auto tabs = std::make_shared<Tabs>();
    tabs->add_tab("Processus", processes_tab->root());
    tabs->add_tab("Ressources", resources_tab->root());
    tabs->add_tab("Systemes de fichiers", fs_tab->root());

    auto status = std::make_shared<StatusBar>();
    status->set_left("Tab: changer de zone  Ctrl+<-/->: onglet  Haut/Bas: naviguer  clic: trier  q: quitter");

    auto root = std::make_shared<Vertical>();
    root->add_child(tabs, LayoutParams::stretch(1));
    root->add_child(status, LayoutParams::fixed(1));
    app.set_root(root);

    processes_tab->table()->set_on_quit([&app] { app.quit(0); });
    fs_tab->table()->set_on_quit([&app] { app.quit(0); });
    resources_tab->scrollable()->set_on_quit([&app] { app.quit(0); });

    app.focus_manager().set_focus(processes_tab->table());

    // Premier releve immediat pour ne pas afficher un ecran vide pendant 1s.
    processes_tab->refresh(0.0);
    resources_tab->refresh(1.0);
    fs_tab->refresh();

    auto last = std::chrono::steady_clock::now();
    app.add_timer(std::chrono::seconds(1), true, [&] {
        auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now - last).count();
        last = now;
        processes_tab->refresh(dt);
        resources_tab->refresh(dt);
        fs_tab->refresh();
        app.need_repaint();
    });

    // --- Raccourci global : Ctrl+Q quitte depuis n'importe quel widget ---
    app.set_global_key_handler([&app](const KeyEvent& e) {
        if (e.key == Key::Char && e.ctrl &&
            (e.codepoint == U'q' || e.codepoint == U'Q')) {
            app.quit(0);
            return true;
        }
        return false;
    });

    return app.run();
}
