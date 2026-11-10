/* @file commands/ReviewCommands.hpp
 *
 *   review status [cible…]                    état d'approbation des cibles
 *   review approve <cible…> [--by NOM] [--note TEXTE]
 *   review reject  <cible…> [--by NOM] [--note TEXTE]
 *   review request <cible…> [--note TEXTE]
 *
 * L'état est stocké dans [review] de module.toml / application.toml. Quand
 * [build].require_approval est actif (ou --require-approval), build / test /
 * run / package / install / deploy refusent les cibles non approuvées.
 */
#pragma once

#include "commands/Command.hpp"

namespace commands {

class ReviewStatusCommand : public Command {
public:
    std::string GetName() const override { return "status"; }
    std::string GetDescription() const override { return "Afficher l'état d'approbation des cibles"; }
    std::string GetUsage() const override { return "[cible…]"; }
    std::vector<std::string> GetAliases() const override { return {"st"}; }
    Category GetCategory() const override { return Category::Tools; }
    int Run(Context& ctx) override;
};

/// approve | reject | request (même logique d'écriture de [review]).
class ReviewSetCommand : public Command {
public:
    explicit ReviewSetCommand(std::string mode) : mode_(std::move(mode)) {}
    std::string GetName() const override { return mode_; }
    std::string GetDescription() const override;
    std::string GetUsage() const override {
        return mode_ == "request" ? "<cible…> [--note TEXTE]" : "<cible…> [--by NOM] [--note TEXTE]";
    }
    std::vector<std::string> GetExamples() const override;
    Category GetCategory() const override { return Category::Tools; }
    void ConfigureArgs(args::Arguments& a) override;
    int Run(Context& ctx) override;

private:
    std::string mode_;   ///< approve | reject | request
};

class ReviewCommand : public CommandGroup {
public:
    std::string GetName() const override { return "review"; }
    std::string GetDescription() const override { return "Approuver, rejeter ou demander la revue de cibles"; }
    std::string GetDetails() const override {
        return "Une cible passe par les états none → requested → approved ou rejected. L'état est\n"
               "écrit dans [review] du module.toml / application.toml (commentaires conservés).\n"
               "Avec [build].require_approval = true, build/test/run/package/install/deploy refusent\n"
               "les cibles non approuvées (--require-approval force, --ignore-approval passe outre).";
    }
    Category GetCategory() const override { return Category::Tools; }
    std::vector<std::shared_ptr<Command>> GetSubCommands() const override {
        return {std::make_shared<ReviewStatusCommand>(), std::make_shared<ReviewSetCommand>("approve"),
                std::make_shared<ReviewSetCommand>("reject"), std::make_shared<ReviewSetCommand>("request")};
    }
};

} // namespace commands
