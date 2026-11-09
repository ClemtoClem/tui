/**
 * @file main.cpp
 * @brief Exemple "code-editor" : IDE minimal démontrant les cinq
 * panneaux/outils (éditeur, terminal, fichiers, git, paramètres) du
 * module ui, tous redimensionnables et navigables à la souris/clavier.
 *
 * Disposition (façon IDE classique) :
 *   [ Fichiers/Git/Parametres ] | [ Editeur ]
 *                               | [ Terminal ]
 * Les deux séparateurs (SplitPane) se redimensionnent à la souris (glisser)
 * ou au clavier (flèches, une fois le séparateur focusé via Tab). Le
 * sélecteur de gauche est un Tabs standard (clic, ou Ctrl+Gauche/Droite
 * une fois focusé) ; l'éditeur a son propre système d'onglets de fichiers
 * (CodeEditor, Ctrl+PageUp/PageDown une fois focusé, clic sur un onglet
 * ou sur son x pour fermer).
 *
 * Quitter : 'q' depuis l'explorateur de fichiers ou les listes
 * staged/changes du panneau Git (même convention que les autres
 * exemples) - jamais depuis l'éditeur de texte, pour ne pas quitter par
 * erreur en tapant du code.
 */

#include "Theme.hpp"
#include "panels/EditorPanel.hpp"
#include "panels/FileBrowser.hpp"
#include "panels/GitPanel.hpp"
#include "panels/SettingsPanel.hpp"

#include <tui/App.hpp>
#include <tui/terminal/PosixTerminalBackend.hpp>
#include <tui/widget/layout/SplitPane.hpp>
#include <tui/widget/layout/Tabs.hpp>
#include <tui/widget/layout/Vertical.hpp>
#include <tui/widgets/StatusBar.hpp>
#include <tui/widgets/Terminal.hpp>

#include <chrono>
#include <filesystem>
#include <memory>

using namespace tui;
using namespace ide;

namespace {

std::shared_ptr<Border> wrap_terminal_panel(std::shared_ptr<Terminal> term) {
    auto panel = std::make_shared<Border>();
    panel->set_border_style(BorderStyle::Rounded);
    panel->set_color(kBorder);
    panel->set_title("Terminal");
    panel->set_child(std::move(term));
    return panel;
}

} // namespace

int main(int argc, char** argv) {
    std::string root_path = argc > 1 ? argv[1] : std::filesystem::current_path().string();

    App app(std::make_unique<PosixTerminalBackend>());

    // --- Panneaux ------------------------------------------------------
    FileBrowser file_browser(root_path);
    GitPanel git_panel(root_path);
    SettingsPanel settings_panel(AppSettings{});
    EditorPanel editor_panel;
    editor_panel.set_repo_dir(root_path);

    auto terminal = std::make_shared<Terminal>();
    auto status_bar = std::make_shared<StatusBar>();

    editor_panel.set_on_status([&](const std::string& msg) { status_bar->set_center(msg); });

    file_browser.set_on_open_file([&](const std::string& path) {
        editor_panel.open_file(path);
        auto ta = editor_panel.editor()->active_editor();
        if (ta) app.focus_manager().set_focus(ta);
    });
    file_browser.set_on_quit([&app] { app.quit(0); });
    git_panel.set_on_quit([&app] { app.quit(0); });

    settings_panel.set_on_change([&](const AppSettings& s) {
        editor_panel.set_show_blame(s.show_blame);
        editor_panel.set_confirm_before_close(s.confirm_before_close);
        status_bar->set_center("Parametres mis a jour.");
    });

    // --- Barre latérale : Fichiers / Git / Parametres ------------------
    auto sidebar_tabs = std::make_shared<Tabs>();
    sidebar_tabs->set_bar_style(TabsBarStyle::Boxed);
    sidebar_tabs->set_colors(kBorder, Color::White(), kMuted);
    sidebar_tabs->add_tab("Fichiers", file_browser.root());
    sidebar_tabs->add_tab("Git", git_panel.root());
    sidebar_tabs->add_tab("Parametres", settings_panel.root());

    // --- Zone centrale : editeur (haut) / terminal (bas) ---------------
    auto main_split = std::make_shared<SplitPane>(SplitPane::Axis::TopBottom);
    main_split->set_first(editor_panel.root());
    main_split->set_second(wrap_terminal_panel(terminal));
    main_split->set_ratio(0.72);

    auto root_split = std::make_shared<SplitPane>(SplitPane::Axis::LeftRight);
    root_split->set_first(sidebar_tabs);
    root_split->set_second(main_split);
    root_split->set_ratio(0.26);

    status_bar->set_left("Tab: changer de zone   Ctrl+S: enregistrer   Ctrl+Alt+Bas: multicurseur   q: quitter (Fichiers/Git)");
    status_bar->set_right("code-editor (tui)");

    auto root = std::make_shared<Vertical>();
    root->add_child(root_split, LayoutParams::stretch(1));
    root->add_child(status_bar, LayoutParams::fixed(1));
    app.set_root(root);

    // --- Terminal intégré : shell dans un pty, drainé périodiquement ---
    terminal->start();
    app.add_timer(std::chrono::milliseconds(33), true, [terminal] { terminal->pump(); });

    app.focus_manager().set_focus(file_browser.tree());

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
