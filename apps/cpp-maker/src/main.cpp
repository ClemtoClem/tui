/* @file src/main.cpp
 *
 * cpp-maker — création, compilation, tests et livraison de projets C/C++,
 * en ligne de commande ou via l'interface TUI (`cpp-maker tui`).
 *
 * Toutes les commandes sont des classes (dossier commands/) enregistrées dans
 * le GlobalContext, qui analyse la ligne de commande et les exécute.
 */

#include "commands/GlobalContext.hpp"

int main(int argc, char* argv[]) {
    auto& g = commands::GlobalContext::Instance();
    commands::RegisterBuiltinCommands(g);
    return g.Dispatch(argc, argv);
}
