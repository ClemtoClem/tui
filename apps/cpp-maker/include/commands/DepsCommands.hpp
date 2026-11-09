/* @file commands/DepsCommands.hpp
 *
 *   deps list [<cible>]                     dépendances (et versions git verrouillées)
 *   deps add <cible> <dep>… [--pkg-config|--system|--module]
 *   deps remove <cible> <dep>…
 *   deps fetch                              résout les dépendances git (cpp-maker.lock)
 *   deps update                             ignore le verrou et réécrit cpp-maker.lock
 *   deps clean                              vide le cache git local
 *
 * <cible> : module, application, layer (dépendances héritées) ou « project ».
 */
#pragma once

#include "commands/Command.hpp"

namespace commands {

class DepsListCommand : public Command {
public:
    std::string GetName() const override { return "list"; }
    std::string GetDescription() const override { return "Afficher les dépendances (d'une cible ou de tout le projet)"; }
    std::string GetUsage() const override { return "[<cible>]"; }
    int Run(Context& ctx) override;
};

class DepsEditCommand : public Command {
public:
    explicit DepsEditCommand(bool add) : add_(add) {}
    std::string GetName() const override { return add_ ? "add" : "remove"; }
    std::string GetDescription() const override {
        return add_ ? "Ajouter des dépendances à une cible" : "Retirer des dépendances d'une cible";
    }
    std::string GetUsage() const override { return "<cible> <dépendance>… [--module|--pkg-config|--system]"; }
    std::string GetDetails() const override;
    std::vector<std::string> GetExamples() const override;
    std::vector<std::string> GetAliases() const override { return add_ ? std::vector<std::string>{} : std::vector<std::string>{"rm"}; }
    void ConfigureArgs(args::Arguments& a) override;
    int Run(Context& ctx) override;

private:
    bool add_;
};

class DepsFetchCommand : public Command {
public:
    explicit DepsFetchCommand(bool update) : update_(update) {}
    std::string GetName() const override { return update_ ? "update" : "fetch"; }
    std::string GetDescription() const override {
        return update_ ? "Mettre à jour les dépendances git (ignore cpp-maker.lock)"
                       : "Résoudre les dépendances git (respecte cpp-maker.lock)";
    }
    int Run(Context& ctx) override;

private:
    bool update_;
};

class DepsCleanCommand : public Command {
public:
    std::string GetName() const override { return "clean"; }
    std::string GetDescription() const override { return "Vider le cache local des dépôts git"; }
    int Run(Context& ctx) override;
};

class DepsCommand : public CommandGroup {
public:
    std::string GetName() const override { return "deps"; }
    std::string GetDescription() const override { return "Gérer les dépendances (modules, pkg-config, système, git)"; }
    Category GetCategory() const override { return Category::Dependencies; }
    std::vector<std::shared_ptr<Command>> GetSubCommands() const override {
        return {std::make_shared<DepsListCommand>(), std::make_shared<DepsEditCommand>(true),
                std::make_shared<DepsEditCommand>(false), std::make_shared<DepsFetchCommand>(false),
                std::make_shared<DepsFetchCommand>(true), std::make_shared<DepsCleanCommand>()};
    }
};

} // namespace commands
