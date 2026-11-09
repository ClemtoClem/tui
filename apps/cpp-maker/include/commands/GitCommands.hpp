/* @file commands/GitCommands.hpp
 *
 *   git status                       état du dépôt, groupé par cible
 *   git add <cible…>|--all           indexer les fichiers de cibles
 *   git unstage <cible…>|--all       désindexer
 *   git commit -m <message> [-a]     valider (le crochet pre-commit s'exécute)
 *   git log [<cible>] [-n N]         historique (d'une cible)
 *   git diff [<cible>] [--staged]    différences (d'une cible)
 *   git push | pull [-- args]        synchronisation
 *   git init                         dépôt + .gitignore + crochet pre-commit
 *   git pre-commit [--all] [-n]      exécuter les règles [git.pre_commit.*]
 *   git hook install|uninstall|status
 *   git rule list|add|remove         règles pre-commit des fichiers toml
 */
#pragma once

#include "commands/Command.hpp"

namespace commands {

/// Sous-commande git simple (status, add, unstage, commit, log, diff, push, pull, init, pre-commit).
class GitActionCommand : public Command {
public:
    explicit GitActionCommand(std::string name) : name_(std::move(name)) {}
    std::string GetName() const override { return name_; }
    std::string GetDescription() const override;
    std::string GetUsage() const override;
    std::string GetDetails() const override;
    std::vector<std::string> GetExamples() const override;
    void ConfigureArgs(args::Arguments& a) override;
    int Run(Context& ctx) override;

private:
    std::string name_;
};

class GitHookCommand : public Command {
public:
    explicit GitHookCommand(std::string name) : name_(std::move(name)) {}
    std::string GetName() const override { return name_; }
    std::string GetDescription() const override;
    void ConfigureArgs(args::Arguments& a) override;
    int Run(Context& ctx) override;

private:
    std::string name_;
};

class GitHookGroup : public CommandGroup {
public:
    std::string GetName() const override { return "hook"; }
    std::string GetDescription() const override { return "Installer ou retirer le crochet git pre-commit"; }
    std::vector<std::shared_ptr<Command>> GetSubCommands() const override {
        return {std::make_shared<GitHookCommand>("install"), std::make_shared<GitHookCommand>("uninstall"),
                std::make_shared<GitHookCommand>("status")};
    }
};

class GitRuleCommand : public Command {
public:
    explicit GitRuleCommand(std::string name) : name_(std::move(name)) {}
    std::string GetName() const override { return name_; }
    std::string GetDescription() const override;
    std::string GetUsage() const override;
    std::vector<std::string> GetExamples() const override;
    void ConfigureArgs(args::Arguments& a) override;
    int Run(Context& ctx) override;

private:
    std::string name_;
};

class GitRuleGroup : public CommandGroup {
public:
    std::string GetName() const override { return "rule"; }
    std::string GetDescription() const override { return "Règles pre-commit ([git.pre_commit.<nom>] des fichiers toml)"; }
    std::vector<std::string> GetAliases() const override { return {"rules"}; }
    std::vector<std::shared_ptr<Command>> GetSubCommands() const override {
        return {std::make_shared<GitRuleCommand>("list"), std::make_shared<GitRuleCommand>("add"),
                std::make_shared<GitRuleCommand>("remove")};
    }
};

class GitGroup : public CommandGroup {
public:
    std::string GetName() const override { return "git"; }
    std::string GetDescription() const override { return "Git : état par cible, commit, règles et crochet pre-commit"; }
    std::string GetDetails() const override;
    Category GetCategory() const override { return Category::Tools; }
    std::vector<std::shared_ptr<Command>> GetSubCommands() const override {
        std::vector<std::shared_ptr<Command>> r;
        for (auto n : {"status", "add", "unstage", "commit", "log", "diff", "push", "pull", "init", "pre-commit"})
            r.push_back(std::make_shared<GitActionCommand>(n));
        r.push_back(std::make_shared<GitHookGroup>());
        r.push_back(std::make_shared<GitRuleGroup>());
        return r;
    }
};

} // namespace commands
