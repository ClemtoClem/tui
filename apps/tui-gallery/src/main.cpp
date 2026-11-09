/* @file apps/tui-gallery/src/main.cpp
 *
 * Galerie de libtui : montre les widgets, la feuille de style par défaut
 * (apparence Textual), les thèmes et la souris (clic = focus, survol).
 *
 *   tui-gallery [fichier.tcss]
 *
 * Un fichier .tcss passé en argument est chargé par-dessus la feuille par
 * défaut et rechargé automatiquement à chaque modification.
 *
 * Raccourcis : Ctrl+Q quitter, Ctrl+T thème suivant, Ctrl+S feuille par
 * défaut on/off, Tab / clic pour naviguer.
 */

#include "Gallery.hpp"

#include <iostream>

using namespace tui;

int main(int argc, char** argv) {
    App app(std::make_unique<PosixTerminalBackend>());
    Gallery gallery(app);
    if (argc > 1 && !app.load_css_file(argv[1], /*watch=*/true)) {
        std::cerr << "tui-gallery : impossible de charger " << argv[1] << "\n";
        for (auto& e : app.css_errors()) std::cerr << "  " << e.to_string() << "\n";
        return 1;
    }
    app.add_timer(std::chrono::milliseconds(100), true, [&] { gallery.tick(); });
    return app.run();
}
