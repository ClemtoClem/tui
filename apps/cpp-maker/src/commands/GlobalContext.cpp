/* @file commands/GlobalContext.cpp */

#include "commands/GlobalContext.hpp"

#include "core/Redirect.hpp"
#include "core/Term.hpp"
#include "core/Util.hpp"
#include "core/Version.hpp"
#include "io/toml.hpp"

#include <algorithm>

#include <unistd.h>

namespace commands {

GlobalContext& GlobalContext::Instance() {
    static GlobalContext inst;
    return inst;
}

void GlobalContext::Register(std::shared_ptr<Command> cmd) {
    for (auto& c : commands_)
        if (c->GetName() == cmd->GetName()) {
            c = std::move(cmd);
            return;
        }
    commands_.push_back(std::move(cmd));
}

std::shared_ptr<Command> GlobalContext::Find(const std::string& name) const {
    for (auto& c : commands_) {
        if (c->GetName() == name) return c;
        for (auto& a : c->GetAliases())
            if (a == name) return c;
    }
    return nullptr;
}

std::shared_ptr<Command> GlobalContext::ResolvePath(const std::vector<std::string>& words, size_t& consumed,
                                                    std::string& path) const {
    consumed = 0;
    if (words.empty()) return nullptr;
    auto cmd = Find(words[0]);
    if (!cmd) return nullptr;
    consumed = 1;
    path = cmd->GetName();
    for (size_t i = 1; i < words.size(); ++i) {
        auto subs = cmd->GetSubCommands();
        if (subs.empty()) break;
        std::shared_ptr<Command> next;
        for (auto& s : subs) {
            if (s->GetName() == words[i]) next = s;
            for (auto& a : s->GetAliases())
                if (a == words[i]) next = s;
            if (next) break;
        }
        if (!next) break;
        cmd = next;
        consumed = i + 1;
        path += " " + cmd->GetName();
    }
    return cmd;
}

void add_help_option(args::Arguments& a) {
    a.add("--help").set_value(false).flags("-h").help("Affiche cette aide");
}

int GlobalContext::Dispatch(int argc, char* argv[]) {
    std::vector<std::string> v;
    for (int i = 1; i < argc; ++i) v.emplace_back(argv[i]);
    return Dispatch(std::move(v));
}

int GlobalContext::Dispatch(std::vector<std::string> argv) {
    // --- Options globales placées avant la commande
    size_t i = 0;
    while (i < argv.size()) {
        const std::string& a = argv[i];
        if (a == "-C" || a == "--directory") {
            if (i + 1 >= argv.size()) {
                term::error("{} : dossier manquant", a);
                return 2;
            }
            if (::chdir(argv[i + 1].c_str()) != 0) {
                term::error("dossier inaccessible : {}", argv[i + 1]);
                return 1;
            }
            i += 2;
        } else if (a.starts_with("--directory=")) {
            if (::chdir(a.substr(12).c_str()) != 0) {
                term::error("dossier inaccessible : {}", a.substr(12));
                return 1;
            }
            ++i;
        } else if (a == "--no-color") {
            term::set_color(false);
            ++i;
        } else if (a == "--color") {
            term::set_color(true);
            ++i;
        } else {
            break;
        }
    }
    argv.erase(argv.begin(), argv.begin() + static_cast<long>(i));

    // --output / --stdout / --stderr : globales, avant ou après la commande.
    std::string redirect_error;
    auto rq = redirect::extract(argv, redirect_error);
    if (!redirect_error.empty()) {
        term::error("{}", redirect_error);
        return 2;
    }
    std::error_code cwd_ec;
    redirect::Session redirection;
    if (auto e = redirection.start(rq, fs::current_path(cwd_ec)); !e.empty()) {
        term::error("{}", e);
        return 1;
    }

    if (argv.empty() || argv[0] == "-h" || argv[0] == "--help") {
        PrintRootHelp();
        return 0;
    }
    if (argv[0] == "--version" || argv[0] == "-V") {
        term::outln("{} {}", cppmaker::kProgram, cppmaker::kVersion);
        return 0;
    }

    std::vector<std::string> words;
    for (auto& a : argv) {
        if (a.starts_with("-")) break;
        words.push_back(a);
    }
    size_t consumed = 0;
    std::string path;
    auto cmd = ResolvePath(words, consumed, path);
    if (!cmd) {
        std::vector<std::string> names;
        for (auto& c : commands_) names.push_back(c->GetName());
        auto s = util::suggest(argv[0], names);
        term::error("commande inconnue '{}'{}", argv[0], s.empty() ? "" : " — vouliez-vous dire '" + s + "' ?");
        term::errln("{}", term::dim("cpp-maker help pour la liste des commandes"));
        return 2;
    }

    args::Arguments a;
    add_help_option(a);
    try {
        cmd->ConfigureArgs(a);
        a.parse(argv);
    } catch (const args::Exception& e) {
        term::error("{}", e.what());
        term::errln("{}", term::dim(std::format("usage : cpp-maker {} {}   (cpp-maker {} --help)", path, cmd->GetUsage(), path)));
        return 2;
    }
    a.set_dispatch_depth(consumed);
    if (a.get<bool>("--help")) {
        PrintCommandHelp(*cmd, path);
        return 0;
    }

    std::error_code ec;
    Context ctx(a, fs::current_path(ec), path, *cmd);
    try {
        return cmd->Run(ctx);
    } catch (const util::UsageError& e) {
        term::error("{}", e.what());
        term::errln("{}", term::dim(std::format("usage : cpp-maker {} {}   (cpp-maker {} --help)", path, cmd->GetUsage(), path)));
        return 2;
    } catch (const toml::ParseError& e) {
        term::error("{}", e.what());
        return 1;
    } catch (const util::Error& e) {
        term::error("{}", e.what());
        return 1;
    } catch (const args::Exception& e) {
        term::error("{}", e.what());
        return 2;
    } catch (const std::exception& e) {
        term::error("erreur interne : {}", e.what());
        return 3;
    }
}

void GlobalContext::PrintRootHelp() const {
    term::outln("{} {} — création, compilation, tests et livraison de projets C et C++\n",
                term::bold("cpp-maker"), cppmaker::kVersion);
    term::outln("{}", term::bold("USAGE"));
    term::outln("  cpp-maker [-C <dossier>] [--no-color] <commande> [options]");
    term::outln("  options globales : --output F (stdout+stderr), --stdout F, --stderr F");
    term::outln("                     (un nom sans dossier va dans [paths].out, défaut .out)");
    term::outln("  cpp-maker help <commande>          aide détaillée d'une commande");
    term::outln("  cpp-maker tui                      interface interactive (édition des fichiers toml)\n");
    for (auto cat : {Category::Project, Category::Dependencies, Category::Build, Category::Delivery, Category::Tools}) {
        std::vector<std::pair<std::string, std::string>> rows;
        for (auto& c : commands_) {
            if (c->GetCategory() != cat) continue;
            std::string left = c->GetName();
            auto subs = c->GetSubCommands();
            if (!subs.empty()) {
                std::vector<std::string> names;
                for (auto& s : subs) names.push_back(s->GetName());
                left += " " + util::join(names, "|");
            }
            rows.emplace_back(left, c->GetDescription());
        }
        if (rows.empty()) continue;
        term::outln("{}", term::bold(std::string(category_title(cat))));
        size_t w = 0;
        for (auto& r : rows) w = std::max(w, r.first.size());
        w = std::min<size_t>(w, 38);
        for (auto& [l, r] : rows) {
            if (l.size() > w) term::outln("  {}\n  {}   {}", term::cyan(l), std::string(w, ' '), r);
            else term::outln("  {}{}   {}", term::cyan(l), std::string(w - l.size(), ' '), r);
        }
        term::outln();
    }
    term::outln("{}", term::bold("FICHIERS"));
    term::outln("  project.toml                        racine du projet (build, modes, toolchains, paquets…)");
    term::outln("  libs/<layer>/layer.toml             couche d'architecture regroupant des modules");
    term::outln("  libs/<layer>/<module>/module.toml   bibliothèque (static, shared, header-only)");
    term::outln("  apps/<app>/application.toml         exécutable");
    term::outln("\n  {}", term::dim("cpp-maker help files | modes | sanitize | selectors"));
}

void GlobalContext::PrintCommandHelp(const Command& cmd, const std::string& path) const {
    term::outln("{} {} {}\n", term::bold("cpp-maker"), term::bold(path), cmd.GetUsage());
    term::outln("  {}", cmd.GetDescription());
    if (auto al = cmd.GetAliases(); !al.empty()) term::outln("  {}", term::dim("alias : " + util::join(al, ", ")));
    if (auto d = cmd.GetDetails(); !d.empty()) {
        term::outln();
        for (auto& line : util::split(d, '\n', true)) term::outln("  {}", line);
    }
    auto subs = cmd.GetSubCommands();
    if (!subs.empty()) {
        term::outln("\n{}", term::bold("SOUS-COMMANDES"));
        size_t w = 0;
        for (auto& s : subs) w = std::max(w, s->GetName().size() + 1 + s->GetUsage().size());
        w = std::min<size_t>(w, 40);
        for (auto& s : subs) {
            std::string left = s->GetName() + " " + s->GetUsage();
            if (left.size() > w) term::outln("  {}\n  {}   {}", term::cyan(left), std::string(w, ' '), s->GetDescription());
            else term::outln("  {}{}   {}", term::cyan(left), std::string(w - left.size(), ' '), s->GetDescription());
        }
    }
    args::Arguments a;
    add_help_option(a);
    const_cast<Command&>(cmd).ConfigureArgs(a);
    term::outln("\n{}", term::bold("OPTIONS"));
    term::out("{}", a.help());
    if (auto ex = cmd.GetExamples(); !ex.empty()) {
        term::outln("\n{}", term::bold("EXEMPLES"));
        for (auto& e : ex) term::outln("  {}", e);
    }
    if (!subs.empty()) term::outln("\n  {}", term::dim("cpp-maker " + path + " <sous-commande> --help"));
}

} // namespace commands
