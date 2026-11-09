/* @file commands/BuiltinCommands.cpp
 *
 * Enregistrement explicite des commandes : l'ordre détermine celui de l'aide.
 * (Un enregistrement explicite plutôt que statique garantit que les commandes
 * existent aussi quand le code est lié depuis une archive, comme les tests.)
 */

#include "commands/BuildCommands.hpp"
#include "commands/DeliverCommands.hpp"
#include "commands/DepsCommands.hpp"
#include "commands/GitCommands.hpp"
#include "commands/GlobalContext.hpp"
#include "commands/ProjectCommands.hpp"
#include "commands/ToolCommands.hpp"

namespace commands {

void RegisterBuiltinCommands(GlobalContext& g) {
    // Projet
    g.Register(std::make_shared<NewCommand>());
    g.Register(std::make_shared<InitCommand>());
    g.Register(std::make_shared<AddCommand>());
    g.Register(std::make_shared<RemoveCommand>());
    g.Register(std::make_shared<RenameCommand>());
    g.Register(std::make_shared<ListCommand>());
    g.Register(std::make_shared<InfoCommand>());
    g.Register(std::make_shared<GraphCommand>());
    // Dépendances
    g.Register(std::make_shared<DepsCommand>());
    // Construction
    g.Register(std::make_shared<BuildCommand>());
    g.Register(std::make_shared<RunCommand>());
    g.Register(std::make_shared<TestCommand>());
    g.Register(std::make_shared<CheckCommand>());
    g.Register(std::make_shared<CleanCommand>());
    g.Register(std::make_shared<SizeCommand>());
    // Livraison
    g.Register(std::make_shared<PackageCommand>());
    g.Register(std::make_shared<InstallCommand>());
    g.Register(std::make_shared<DeployCommand>());
    // Outils
    g.Register(std::make_shared<ConfigCommand>());
    g.Register(std::make_shared<ExportGroup>());
    g.Register(std::make_shared<ToolchainGroup>());
    g.Register(std::make_shared<GitGroup>());
    g.Register(std::make_shared<DoctorCommand>());
    g.Register(std::make_shared<TuiCommand>());
    g.Register(std::make_shared<HelpCommand>());
    g.Register(std::make_shared<VersionCommand>());
}

} // namespace commands
