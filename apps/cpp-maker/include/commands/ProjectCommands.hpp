/* @file commands/ProjectCommands.hpp
 *
 * Structure du projet :
 *   new <nom>                         créer un projet
 *   init                              adopter le dossier courant
 *   add layer|module|app …            ajouter une couche, un module, une application
 *   remove layer|module|app <nom>     retirer (corbeille, ou --purge)
 *   rename layer|module|app <a> <b>   renommer et mettre à jour les références
 *   list [layers|modules|apps]        lister
 *   info [<cible>]                    détail du projet ou d'une cible
 *   graph [--dot]                     graphe des dépendances
 */
#pragma once

#include "commands/Command.hpp"

namespace commands {

class NewCommand : public Command {
public:
    std::string GetName() const override { return "new"; }
    std::string GetDescription() const override { return "Créer un nouveau projet C/C++"; }
    std::string GetUsage() const override { return "<nom> [options]"; }
    std::string GetDetails() const override;
    std::vector<std::string> GetExamples() const override {
        return {"cpp-maker new hello                     # layer core + module + application",
                "cpp-maker new outils --layout lib       # bibliothèque seule",
                "cpp-maker new demo --lang c --c-std c11",
                "cpp-maker new jeu --layer engine --module render --app jeu"};
    }
    Category GetCategory() const override { return Category::Project; }
    void ConfigureArgs(args::Arguments& a) override;
    int Run(Context& ctx) override;
};

class InitCommand : public Command {
public:
    std::string GetName() const override { return "init"; }
    std::string GetDescription() const override { return "Créer project.toml dans le dossier courant"; }
    std::string GetDetails() const override {
        return "Adopte un dossier existant : écrit project.toml et crée libs/ et apps/ s'ils\n"
               "n'existent pas. Les sources existantes ne sont pas modifiées.";
    }
    Category GetCategory() const override { return Category::Project; }
    void ConfigureArgs(args::Arguments& a) override;
    int Run(Context& ctx) override;
};

// --- add -------------------------------------------------------------------

class AddLayerCommand : public Command {
public:
    std::string GetName() const override { return "layer"; }
    std::string GetDescription() const override { return "Ajouter un layer (libs/<nom>/layer.toml)"; }
    std::string GetUsage() const override { return "<nom> [options]"; }
    std::vector<std::string> GetExamples() const override {
        return {"cpp-maker add layer core", "cpp-maker add layer ui --deps core.*"};
    }
    void ConfigureArgs(args::Arguments& a) override;
    int Run(Context& ctx) override;
};

class AddModuleCommand : public Command {
public:
    std::string GetName() const override { return "module"; }
    std::string GetDescription() const override { return "Ajouter un module (bibliothèque) à un layer"; }
    std::string GetUsage() const override { return "<layer> <nom> [options] | <layer>.<nom> [options]"; }
    std::vector<std::string> GetExamples() const override {
        return {"cpp-maker add module core log", "cpp-maker add module core.math --type header-only",
                "cpp-maker add module net http --type shared --deps core.log"};
    }
    void ConfigureArgs(args::Arguments& a) override;
    int Run(Context& ctx) override;
};

class AddAppCommand : public Command {
public:
    std::string GetName() const override { return "app"; }
    std::string GetDescription() const override { return "Ajouter une application (exécutable)"; }
    std::string GetUsage() const override { return "<nom> [options]"; }
    std::vector<std::string> GetAliases() const override { return {"application"}; }
    std::vector<std::string> GetExamples() const override {
        return {"cpp-maker add app outil --deps core.log,core.math"};
    }
    void ConfigureArgs(args::Arguments& a) override;
    int Run(Context& ctx) override;
};

class AddCommand : public CommandGroup {
public:
    std::string GetName() const override { return "add"; }
    std::string GetDescription() const override { return "Ajouter un layer, un module ou une application"; }
    Category GetCategory() const override { return Category::Project; }
    std::vector<std::shared_ptr<Command>> GetSubCommands() const override {
        return {std::make_shared<AddLayerCommand>(), std::make_shared<AddModuleCommand>(), std::make_shared<AddAppCommand>()};
    }
};

// --- remove ----------------------------------------------------------------

class RemoveTargetCommand : public Command {
public:
    explicit RemoveTargetCommand(std::string kind) : kind_(std::move(kind)) {}
    std::string GetName() const override { return kind_; }
    std::string GetDescription() const override;
    std::string GetUsage() const override { return "<nom> [--purge] [--force] [--yes]"; }
    void ConfigureArgs(args::Arguments& a) override;
    int Run(Context& ctx) override;

private:
    std::string kind_;   ///< layer | module | app
};

class RemoveCommand : public CommandGroup {
public:
    std::string GetName() const override { return "remove"; }
    std::string GetDescription() const override { return "Retirer un layer, un module ou une application"; }
    std::string GetDetails() const override {
        return "Le dossier est déplacé dans .cpp-maker-trash/ (récupérable) ; --purge le supprime\n"
               "définitivement. Une cible dont d'autres dépendent n'est retirée qu'avec --force\n"
               "(les références sont alors supprimées des dépendants).";
    }
    std::vector<std::string> GetAliases() const override { return {"rm"}; }
    Category GetCategory() const override { return Category::Project; }
    std::vector<std::shared_ptr<Command>> GetSubCommands() const override {
        return {std::make_shared<RemoveTargetCommand>("layer"), std::make_shared<RemoveTargetCommand>("module"),
                std::make_shared<RemoveTargetCommand>("app")};
    }
};

// --- rename ----------------------------------------------------------------

class RenameTargetCommand : public Command {
public:
    explicit RenameTargetCommand(std::string kind) : kind_(std::move(kind)) {}
    std::string GetName() const override { return kind_; }
    std::string GetDescription() const override;
    std::string GetUsage() const override { return "<ancien> <nouveau>"; }
    int Run(Context& ctx) override;

private:
    std::string kind_;
};

class RenameCommand : public CommandGroup {
public:
    std::string GetName() const override { return "rename"; }
    std::string GetDescription() const override { return "Renommer un layer, un module ou une application"; }
    std::string GetDetails() const override {
        return "Renomme le dossier, met à jour le champ name et toutes les références\n"
               "(dependencies.modules, layers.order, layer.modules, deploy.*.targets).\n"
               "Le code source (#include, espaces de noms) n'est pas modifié.";
    }
    std::vector<std::string> GetAliases() const override { return {"mv"}; }
    Category GetCategory() const override { return Category::Project; }
    std::vector<std::shared_ptr<Command>> GetSubCommands() const override {
        return {std::make_shared<RenameTargetCommand>("layer"), std::make_shared<RenameTargetCommand>("module"),
                std::make_shared<RenameTargetCommand>("app")};
    }
};

// --- inspection --------------------------------------------------------------

class ListCommand : public Command {
public:
    std::string GetName() const override { return "list"; }
    std::string GetDescription() const override { return "Lister les layers, modules et applications"; }
    std::string GetUsage() const override { return "[layers|modules|apps] [--names]"; }
    std::vector<std::string> GetAliases() const override { return {"ls"}; }
    Category GetCategory() const override { return Category::Project; }
    void ConfigureArgs(args::Arguments& a) override;
    int Run(Context& ctx) override;
};

class InfoCommand : public Command {
public:
    std::string GetName() const override { return "info"; }
    std::string GetDescription() const override { return "Afficher le projet ou le détail d'une cible"; }
    std::string GetUsage() const override { return "[<cible>|<layer>]"; }
    std::vector<std::string> GetExamples() const override {
        return {"cpp-maker info", "cpp-maker info core.log", "cpp-maker info core"};
    }
    Category GetCategory() const override { return Category::Project; }
    int Run(Context& ctx) override;
};

class GraphCommand : public Command {
public:
    std::string GetName() const override { return "graph"; }
    std::string GetDescription() const override { return "Graphe des dépendances (arbre ou Graphviz)"; }
    std::string GetUsage() const override { return "[<cible>…] [--dot] [--all]"; }
    std::vector<std::string> GetExamples() const override {
        return {"cpp-maker graph", "cpp-maker graph --dot | dot -Tsvg -o graphe.svg"};
    }
    Category GetCategory() const override { return Category::Project; }
    void ConfigureArgs(args::Arguments& a) override;
    int Run(Context& ctx) override;
};

} // namespace commands
