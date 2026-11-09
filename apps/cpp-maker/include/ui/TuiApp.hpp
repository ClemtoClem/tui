/* @file ui/TuiApp.hpp
 *
 * Interface interactive de cpp-maker (bibliothèque tui du dépôt).
 */
#pragma once

#include <filesystem>
#include <memory>
#include <string>

namespace tui { class App; }

namespace ui {

struct TuiOptions {
    std::string theme;           ///< thème (vide : mémorisé, sinon planor)
    std::filesystem::path css;   ///< feuille TCSS supplémentaire (rechargée à chaud)
};

/// Construit l'interface sur `app` (backend au choix : terminal réel ou
/// headless pour les tests). L'objet renvoyé doit vivre pendant app.run().
std::shared_ptr<void> build_tui(tui::App& app, const std::filesystem::path& cwd, const TuiOptions& opts = {});

/// Lance l'interface sur le projet contenant `cwd` ; renvoie le code de sortie.
int run_tui(const std::filesystem::path& cwd, const TuiOptions& opts = {});

} // namespace ui
