/* @file commands/ReviewCommands.cpp */

#include "commands/ReviewCommands.hpp"

#include "core/Term.hpp"
#include "core/Util.hpp"
#include "model/Editor.hpp"

#include <ctime>

namespace commands {

namespace {

const model::LoadOptions kLight{.resolve_git = false, .update_git = false, .validate = false};

std::string today() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[16];
    std::strftime(buf, sizeof buf, "%Y-%m-%d", &tm);
    return buf;
}

/// Cibles visées : les positionnels (sélecteurs) sinon tout le projet.
std::vector<std::string> review_targets(Context& ctx, const model::Workspace& ws) {
    auto sel = ctx.positionals();
    std::vector<std::string> ids;
    if (sel.empty()) {
        for (auto& t : ws.targets)
            if (!t.from_git) ids.push_back(t.id);
        return ids;
    }
    for (auto& s : sel) util::push_unique(ids, ws.resolve_selector(s));
    return ids;
}

std::string status_glyph(const model::ReviewConfig& r) {
    if (r.status == "approved") return term::green("✓ approuvée");
    if (r.status == "rejected") return term::red("✗ rejetée");
    if (r.status == "requested") return term::yellow("● demandée");
    return term::dim("— non examinée");
}

} // namespace

int ReviewStatusCommand::Run(Context& ctx) {
    auto& ws = ctx.workspace(kLight);
    auto ids = review_targets(ctx, ws);
    if (ids.empty()) {
        term::outln("Aucune cible dans ce projet.");
        return 0;
    }
    term::outln("{} {}", term::bold("Approbation de"), term::cyan(ws.project.name));
    size_t approved = 0;
    for (auto& id : ids) {
        auto* t = ws.find(id);
        if (!t) continue;
        const auto& r = t->review;
        if (r.is_approved()) ++approved;
        std::string detail;
        if (!r.reviewer.empty()) detail += " · " + r.reviewer;
        if (!r.date.empty()) detail += " · " + r.date;
        if (!r.notes.empty()) detail += " — " + r.notes;
        term::outln("  {:<22} {:<14} {:<10} {}", status_glyph(r), t->id, t->type, detail);
    }
    term::outln("\n{} approuvée(s) sur {}{}", approved, ids.size(),
                ws.project.require_approval
                    ? term::yellow(" — [build].require_approval = true : les cibles non approuvées sont bloquées")
                    : "");
    return 0;
}

std::string ReviewSetCommand::GetDescription() const {
    if (mode_ == "approve") return "Approuver des cibles";
    if (mode_ == "reject") return "Rejeter des cibles";
    return "Demander la revue de cibles";
}

std::vector<std::string> ReviewSetCommand::GetExamples() const {
    if (mode_ == "approve") return {"cpp-maker review approve core.log", "cpp-maker review approve all --by alice"};
    if (mode_ == "reject") return {"cpp-maker review reject net.http --note \"tests en échec\""};
    return {"cpp-maker review request core.*"};
}

void ReviewSetCommand::ConfigureArgs(args::Arguments& a) {
    if (mode_ != "request") a.add("--by").set_value(std::string()).metavar("NOM").help("Reviseur qui statue");
    a.add("--note").set_value(std::string()).metavar("TEXTE").help("Commentaire");
}

int ReviewSetCommand::Run(Context& ctx) {
    auto& ws = ctx.workspace(kLight);
    auto ids = review_targets(ctx, ws);
    if (ids.empty()) util::fail("aucune cible visée (cpp-maker review status)");
    bool request = mode_ == "request";
    std::string status = mode_ == "approve" ? "approved" : mode_ == "reject" ? "rejected" : "requested";
    std::string by = request ? std::string() : ctx.get<std::string>("--by");
    std::string note = ctx.get<std::string>("--note");

    model::Transaction tx(ws.root);
    size_t done = 0;
    for (auto& id : ids) {
        auto* t = ws.find(id);
        if (!t) continue;
        if (t->from_git) {
            term::warn("'{}' provient d'une dépendance git (ignorée)", id);
            continue;
        }
        auto& d = tx.doc(t->file);
        d.set({"review"}, "status", toml::Value(status));
        if (request) {
            d.remove({"review"}, "reviewer");
            d.remove({"review"}, "date");
        } else {
            d.set({"review"}, "date", toml::Value(today()));
            if (by.empty()) d.remove({"review"}, "reviewer");
            else d.set({"review"}, "reviewer", toml::Value(by));
        }
        if (note.empty()) d.remove({"review"}, "notes");
        else d.set({"review"}, "notes", toml::Value(note));
        ++done;
    }
    if (done == 0) return 0;
    tx.commit();
    term::ok("{} : {}", mode_, util::join(ids, ", "));
    return 0;
}

} // namespace commands
