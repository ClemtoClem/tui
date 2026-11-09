/* @file commands/Command.hpp
 *
 * Patron Commande : chaque commande de cpp-maker est une classe distincte.
 *
 *  - GetName / GetDescription / GetUsage / GetDetails / GetExamples
 *    alimentent l'aide (`cpp-maker help <commande>`, `--help`) ;
 *  - GetSubCommands déclare une arborescence (« add module », « deps add ») ;
 *  - ConfigureArgs enregistre les options avant l'analyse de la ligne ;
 *  - Run exécute la commande et renvoie le code de sortie.
 *
 * Le contexte d'exécution (Context) donne accès aux arguments, au dossier
 * courant et au projet chargé à la demande.
 */
#pragma once

#include "io/args.hpp"
#include "model/Workspace.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace commands {

enum class Category { Project, Dependencies, Build, Delivery, Tools };
std::string_view category_title(Category c);

class Command;

class Context {
public:
    Context(args::Arguments& a, fs::path cwd, std::string path, const Command& cmd)
        : args_(a), cwd_(std::move(cwd)), path_(std::move(path)), cmd_(cmd) {}

    [[nodiscard]] args::Arguments& args() { return args_; }
    [[nodiscard]] const args::Arguments& args() const { return args_; }
    [[nodiscard]] const fs::path& cwd() const { return cwd_; }
    /// Route complète (« add module »).
    [[nodiscard]] const std::string& path() const { return path_; }
    [[nodiscard]] const Command& command() const { return cmd_; }

    [[nodiscard]] std::vector<std::string> positionals() const { return args_.positionals(); }
    /// i-ème positionnel, ou erreur d'usage avec `what` comme nom attendu.
    [[nodiscard]] std::string positional(size_t i, const std::string& what) const;
    [[nodiscard]] std::string positional_or(size_t i, const std::string& def) const;
    void expect_max_positionals(size_t n) const;

    template <typename T>
    [[nodiscard]] T get(const std::string& name) const { return args_.get<T>(name); }
    [[nodiscard]] bool flag(const std::string& name) const { return args_.get<bool>(name); }
    [[nodiscard]] bool is_set(const std::string& name) const { return args_.is_set(name); }

    /// Projet du dossier courant (chargé une seule fois).
    model::Workspace& workspace(const model::LoadOptions& o = {});
    void reload_workspace(const model::LoadOptions& o = {});

    [[noreturn]] void usage(const std::string& message) const;

private:
    args::Arguments& args_;
    fs::path cwd_;
    std::string path_;
    const Command& cmd_;
    std::optional<model::Workspace> ws_;
};

class Command {
public:
    virtual ~Command() = default;

    [[nodiscard]] virtual std::string GetName() const = 0;
    [[nodiscard]] virtual std::string GetDescription() const = 0;
    /// Arguments après le nom (« <layer> <nom> [options] »).
    [[nodiscard]] virtual std::string GetUsage() const { return "[options]"; }
    [[nodiscard]] virtual std::string GetDetails() const { return {}; }
    [[nodiscard]] virtual std::vector<std::string> GetExamples() const { return {}; }
    [[nodiscard]] virtual std::vector<std::string> GetAliases() const { return {}; }
    [[nodiscard]] virtual Category GetCategory() const { return Category::Tools; }

    /// Sous-commandes (vide si la commande est terminale).
    [[nodiscard]] virtual std::vector<std::shared_ptr<Command>> GetSubCommands() const { return {}; }

    /// Enregistrement des options AVANT l'analyse de la ligne de commande.
    virtual void ConfigureArgs(args::Arguments& /*a*/) {}

    virtual int Run(Context& ctx) = 0;
};

/// Commande conteneur : sans sous-commande reconnue, affiche son aide.
class CommandGroup : public Command {
public:
    [[nodiscard]] std::string GetUsage() const override { return "<sous-commande> [options]"; }
    int Run(Context& ctx) override;
};

} // namespace commands
