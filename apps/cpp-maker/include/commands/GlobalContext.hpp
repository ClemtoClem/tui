/* @file commands/GlobalContext.hpp
 *
 * Registre des commandes et aiguillage de la ligne de commande :
 *
 *   cpp-maker [-C <dossier>] [--no-color] <commande> [<sous-commande>…] [args]
 *
 * Les mots en tête de ligne descendent l'arborescence des commandes ; les
 * mots restants sont des positionnels pour la commande atteinte.
 */
#pragma once

#include "commands/Command.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace commands {

class GlobalContext {
public:
    static GlobalContext& Instance();

    void Register(std::shared_ptr<Command> cmd);
    [[nodiscard]] std::shared_ptr<Command> Find(const std::string& name) const;
    [[nodiscard]] const std::vector<std::shared_ptr<Command>>& All() const { return commands_; }

    /// Descend la route ; renvoie la commande atteinte et le nombre de mots consommés.
    [[nodiscard]] std::shared_ptr<Command> ResolvePath(const std::vector<std::string>& words, size_t& consumed,
                                                       std::string& path) const;

    int Dispatch(int argc, char* argv[]);
    int Dispatch(std::vector<std::string> argv);

    void PrintRootHelp() const;
    void PrintCommandHelp(const Command& cmd, const std::string& path) const;

private:
    GlobalContext() = default;
    std::vector<std::shared_ptr<Command>> commands_;
};

/// Enregistre toutes les commandes intégrées (appelé par main()).
void RegisterBuiltinCommands(GlobalContext& g);

/// Ajoute --help/-h à toutes les commandes (ajouté par le dispatcher).
void add_help_option(args::Arguments& a);

} // namespace commands
