/* @file commands/ToolCommands.hpp
 *
 *   config show|get|set|unset|add|remove|validate|keys|edit   fichiers toml
 *   export compdb|ninja|xml                                    fichiers pour outils tiers
 *   toolchain list|show|which|add|use|install|remove           compilateurs
 *   doctor [--fix]                                             diagnostic
 *   tui                                                        interface interactive
 *   help [commande|sujet]                                      aide
 *   version
 */
#pragma once

#include "commands/Command.hpp"

namespace commands {

// --- config ------------------------------------------------------------------

/// Options communes : choix du fichier édité (--layer, --module, --app).
class ConfigFileCommand : public Command {
public:
    void ConfigureArgs(args::Arguments& a) override;
};

class ConfigShowCommand : public ConfigFileCommand {
public:
    std::string GetName() const override { return "show"; }
    std::string GetDescription() const override { return "Afficher les valeurs (fichier + défauts)"; }
    std::string GetUsage() const override { return "[préfixe…] [--layer L|--module M|--app A]"; }
    void ConfigureArgs(args::Arguments& a) override;
    int Run(Context& ctx) override;
};

class ConfigGetCommand : public ConfigFileCommand {
public:
    std::string GetName() const override { return "get"; }
    std::string GetDescription() const override { return "Lire une valeur (sortie brute, pour scripts)"; }
    std::string GetUsage() const override { return "<section.clé> [--layer L|--module M|--app A]"; }
    int Run(Context& ctx) override;
};

class ConfigSetCommand : public ConfigFileCommand {
public:
    explicit ConfigSetCommand(std::string mode) : mode_(std::move(mode)) {}
    std::string GetName() const override { return mode_; }
    std::string GetDescription() const override;
    std::string GetUsage() const override;
    std::string GetDetails() const override;
    std::vector<std::string> GetExamples() const override;
    void ConfigureArgs(args::Arguments& a) override;
    int Run(Context& ctx) override;

private:
    std::string mode_;   ///< set | unset | add | remove
};

class ConfigValidateCommand : public Command {
public:
    std::string GetName() const override { return "validate"; }
    std::string GetDescription() const override { return "Valider tous les fichiers de configuration"; }
    int Run(Context& ctx) override;
};

class ConfigKeysCommand : public Command {
public:
    std::string GetName() const override { return "keys"; }
    std::string GetDescription() const override { return "Lister les clés connues d'un type de fichier"; }
    std::string GetUsage() const override { return "[project|layer|module|app]"; }
    int Run(Context& ctx) override;
};

class ConfigEditCommand : public ConfigFileCommand {
public:
    std::string GetName() const override { return "edit"; }
    std::string GetDescription() const override { return "Ouvrir le fichier dans $EDITOR puis le valider"; }
    std::string GetUsage() const override { return "[--layer L|--module M|--app A]"; }
    int Run(Context& ctx) override;
};

class ConfigCommand : public CommandGroup {
public:
    std::string GetName() const override { return "config"; }
    std::string GetDescription() const override { return "Lire et modifier project.toml, layer.toml, module.toml, application.toml"; }
    std::string GetDetails() const override {
        return "Les modifications conservent les commentaires et la mise en forme, sont contrôlées\n"
               "par le schéma (types, valeurs possibles) puis le projet entier est revalidé : en cas\n"
               "d'erreur, le fichier reste intact. Fichier visé : project.toml par défaut, sinon\n"
               "--layer <nom>, --module <layer.module> ou --app <nom>.";
    }
    Category GetCategory() const override { return Category::Tools; }
    std::vector<std::shared_ptr<Command>> GetSubCommands() const override {
        return {std::make_shared<ConfigShowCommand>(), std::make_shared<ConfigGetCommand>(),
                std::make_shared<ConfigSetCommand>("set"), std::make_shared<ConfigSetCommand>("unset"),
                std::make_shared<ConfigSetCommand>("add"), std::make_shared<ConfigSetCommand>("remove"),
                std::make_shared<ConfigValidateCommand>(), std::make_shared<ConfigKeysCommand>(),
                std::make_shared<ConfigEditCommand>()};
    }
};

// --- export ------------------------------------------------------------------

class ExportCommand : public Command {
public:
    explicit ExportCommand(std::string what) : what_(std::move(what)) {}
    std::string GetName() const override { return what_; }
    std::string GetDescription() const override;
    std::string GetUsage() const override;
    void ConfigureArgs(args::Arguments& a) override;
    int Run(Context& ctx) override;

private:
    std::string what_;   ///< compdb | ninja | xml
};

class ExportGroup : public CommandGroup {
public:
    std::string GetName() const override { return "export"; }
    std::string GetDescription() const override { return "Exporter compile_commands.json, build.ninja ou une archive XML"; }
    Category GetCategory() const override { return Category::Tools; }
    std::vector<std::shared_ptr<Command>> GetSubCommands() const override {
        return {std::make_shared<ExportCommand>("compdb"), std::make_shared<ExportCommand>("ninja"),
                std::make_shared<ExportCommand>("xml")};
    }
};

// --- toolchain ---------------------------------------------------------------

class ToolchainCommand : public Command {
public:
    explicit ToolchainCommand(std::string sub) : sub_(std::move(sub)) {}
    std::string GetName() const override { return sub_; }
    std::string GetDescription() const override;
    std::string GetUsage() const override;
    std::vector<std::string> GetExamples() const override;
    void ConfigureArgs(args::Arguments& a) override;
    int Run(Context& ctx) override;

private:
    std::string sub_;
};

class ToolchainGroup : public CommandGroup {
public:
    std::string GetName() const override { return "toolchain"; }
    std::string GetDescription() const override { return "Compilateurs : détection, profils, versions gcc (apt)"; }
    std::vector<std::string> GetAliases() const override { return {"tc"}; }
    Category GetCategory() const override { return Category::Tools; }
    std::vector<std::shared_ptr<Command>> GetSubCommands() const override {
        std::vector<std::shared_ptr<Command>> r;
        for (auto s : {"list", "show", "which", "add", "use", "install", "remove"})
            r.push_back(std::make_shared<ToolchainCommand>(s));
        return r;
    }
};

// --- divers ------------------------------------------------------------------

class DoctorCommand : public Command {
public:
    std::string GetName() const override { return "doctor"; }
    std::string GetDescription() const override { return "Diagnostiquer configuration, outils, toolchains et #include"; }
    std::string GetUsage() const override { return "[--fix] [--strict]"; }
    std::string GetDetails() const override {
        return "Vérifie : validité des fichiers toml (clés inconnues comprises), outils (ar, pkg-config,\n"
               "ninja, tar, dpkg-deb, rsync, valgrind, qemu…), toolchains (compilation C/C++, sanitizers,\n"
               "émulateur) et dépendances implicites (#include d'un module non déclaré).\n"
               "--fix ajoute les dépendances manquantes ; --strict les compte comme des erreurs (CI).";
    }
    Category GetCategory() const override { return Category::Tools; }
    void ConfigureArgs(args::Arguments& a) override;
    int Run(Context& ctx) override;
};

class TuiCommand : public Command {
public:
    std::string GetName() const override { return "tui"; }
    std::string GetDescription() const override { return "Interface interactive : configuration et exécution"; }
    std::string GetDetails() const override {
        return "Arborescence du projet, formulaires d'édition des fichiers toml (générés depuis le\n"
               "schéma), éditeur TOML brut avec validation, et terminal intégré pour lancer les\n"
               "commandes (build, run, test, package…). F1 aide, Ctrl+S enregistrer, Ctrl+Q quitter.\n"
               "Souris : clic pour sélectionner, double-clic pour ouvrir. F9 choisit le thème (mémorisé\n"
               "dans ~/.config/cpp-maker/tui-theme) ; ~/.config/cpp-maker/tui.tcss personnalise le style.";
    }
    std::vector<std::string> GetAliases() const override { return {"ui"}; }
    std::vector<std::string> GetExamples() const override {
        return {"cpp-maker tui", "cpp-maker tui --theme nord", "cpp-maker tui --css ma-feuille.tcss", "cpp-maker tui --themes"};
    }
    Category GetCategory() const override { return Category::Tools; }
    void ConfigureArgs(args::Arguments& a) override;
    int Run(Context& ctx) override;
};

class HelpCommand : public Command {
public:
    std::string GetName() const override { return "help"; }
    std::string GetDescription() const override { return "Aide d'une commande ou d'un sujet (files, modes, sanitize, selectors)"; }
    std::string GetUsage() const override { return "[commande…|sujet]"; }
    Category GetCategory() const override { return Category::Tools; }
    int Run(Context& ctx) override;
};

class VersionCommand : public Command {
public:
    std::string GetName() const override { return "version"; }
    std::string GetDescription() const override { return "Afficher la version"; }
    Category GetCategory() const override { return Category::Tools; }
    int Run(Context& ctx) override;
};

} // namespace commands
