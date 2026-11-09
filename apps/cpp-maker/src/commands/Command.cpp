/* @file commands/Command.cpp */

#include "commands/Command.hpp"

#include "commands/GlobalContext.hpp"
#include "core/Term.hpp"
#include "core/Util.hpp"

namespace commands {

std::string_view category_title(Category c) {
    switch (c) {
    case Category::Project: return "PROJET";
    case Category::Dependencies: return "DÉPENDANCES";
    case Category::Build: return "CONSTRUCTION";
    case Category::Delivery: return "LIVRAISON";
    case Category::Tools: return "OUTILS";
    }
    return "";
}

std::string Context::positional(size_t i, const std::string& what) const {
    auto p = positionals();
    if (i >= p.size()) usage(what + " manquant");
    return p[i];
}

std::string Context::positional_or(size_t i, const std::string& def) const {
    auto p = positionals();
    return i < p.size() ? p[i] : def;
}

void Context::expect_max_positionals(size_t n) const {
    auto p = positionals();
    if (p.size() > n) usage("argument inattendu : " + p[n]);
}

model::Workspace& Context::workspace(const model::LoadOptions& o) {
    if (!ws_) ws_ = model::Workspace::load(cwd_, o);
    return *ws_;
}

void Context::reload_workspace(const model::LoadOptions& o) { ws_ = model::Workspace::load(cwd_, o); }

void Context::usage(const std::string& message) const { throw util::UsageError(message); }

int CommandGroup::Run(Context& ctx) {
    auto pos = ctx.positionals();
    if (!pos.empty()) {
        std::vector<std::string> names;
        for (auto& s : GetSubCommands()) names.push_back(s->GetName());
        auto s = util::suggest(pos[0], names);
        ctx.usage(std::format("sous-commande inconnue '{}'{} (choix : {})", pos[0],
                              s.empty() ? "" : " — vouliez-vous dire '" + s + "' ?", util::join(names, ", ")));
    }
    GlobalContext::Instance().PrintCommandHelp(*this, ctx.path());
    return 0;
}

} // namespace commands
