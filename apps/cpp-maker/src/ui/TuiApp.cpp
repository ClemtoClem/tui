/* @file ui/TuiApp.cpp
 *
 * Interface interactive de cpp-maker.
 *
 *   F2  Configuration  arbre du projet + formulaire généré depuis le schéma
 *                      pour l'élément sélectionné, boutons d'action
 *                      (ajouter / renommer / supprimer…)
 *   F3  Fichier TOML   édition brute du fichier de l'élément sélectionné ;
 *                      n'est écrit que si le projet reste valide
 *   F4  Exécution      options (mode, toolchain, cible, sanitizers…) et
 *                      commandes lancées dans un terminal intégré
 *
 * Les actions structurelles (add, rename, remove, config set…) réutilisent
 * la ligne de commande : cpp-maker se relance lui-même, si bien que la TUI
 * et la CLI appliquent exactement les mêmes règles.
 */

#include "ui/TuiApp.hpp"

#include "Theme.hpp"
#include "core/Process.hpp"
#include "core/Term.hpp"
#include "core/Util.hpp"
#include "core/Version.hpp"
#include "model/Editor.hpp"
#include "model/Schema.hpp"
#include "model/Workspace.hpp"
#include "ui/Form.hpp"
#include "ui/Widgets.hpp"
#include "vcs/Git.hpp"

#include <tui/App.hpp>
#include <tui/core/SyntaxHighlight.hpp>
#include <tui/core/Text.hpp>
#include <tui/terminal/PosixTerminalBackend.hpp>
#include <tui/widget/layout/Border.hpp>
#include <tui/widget/layout/Horizontal.hpp>
#include <tui/widget/layout/ScrollableContainer.hpp>
#include <tui/widget/layout/Stack.hpp>
#include <tui/widget/layout/Tabs.hpp>
#include <tui/widget/layout/Vertical.hpp>
#include <tui/widgets/Button.hpp>
#include <tui/widgets/CodeEditor.hpp>
#include <tui/widgets/Dialog.hpp>
#include <tui/widgets/Label.hpp>
#include <tui/widgets/Footer.hpp>
#include <tui/widgets/Header.hpp>
#include <tui/widgets/ListView.hpp>
#include <tui/widgets/Terminal.hpp>

#include <chrono>
#include <cstdlib>
#include <map>
#include <set>

#include <unistd.h>
#include <optional>

namespace ui {

namespace {

using namespace cpp_maker;
using tui::LayoutParams;

const model::LoadOptions kLoad{.resolve_git = false, .update_git = false, .validate = false};

enum Tab : size_t { kTabConfig = 0, kTabToml, kTabExec, kTabGit };

/// Panneau encadré : cadre arrondi, s'allume quand son contenu a le focus (CSS).
std::shared_ptr<tui::Border> framed(std::shared_ptr<tui::Widget> w, const std::string& title) {
    auto b = std::make_shared<tui::Border>();
    b->set_border_style(tui::BorderStyle::Rounded);
    b->set_title(" " + title + " ");
    b->set_child(std::move(w));
    return b;
}

std::shared_ptr<tui::Label> label(const std::string& text, Tone tone = Tone::Normal, bool wrap = false) {
    auto l = std::make_shared<tui::Label>(text);
    set_tone(*l, tone);
    l->set_wrap(wrap);
    return l;
}

std::shared_ptr<TextBlock> text_block(const std::string& text, Tone tone) {
    auto t = std::make_shared<TextBlock>(text);
    set_tone(*t, tone);
    return t;
}

/// Bouton avec variante Textual (« -primary », « -success », « -error »…).
std::shared_ptr<tui::Button> button(const std::string& text, std::function<void()> fn, const std::string& variant = {}) {
    auto b = std::make_shared<tui::Button>(text);
    if (!variant.empty()) b->add_class(variant);
    b->set_on_click(std::move(fn));
    return b;
}

/// Variante de bouton déduite du libellé d'une action.
std::string variant_for(const std::string& text) {
    if (text.starts_with("+ ") || text.starts_with("Créer") || text.starts_with("Approuver")) return "-success";
    if (text.starts_with("Supprimer") || text.starts_with("Retirer") || text.starts_with("Rejeter")) return "-error";
    if (text.starts_with("Compiler") || text.starts_with("Exécuter") || text.starts_with("Valider") ||
        text.starts_with("Déployer") || text.starts_with("Demander"))
        return "-primary";
    return {};
}

/// Marqueur d'approbation affiché dans l'arbre à côté du nom d'une cible.
std::string review_suffix(const model::TargetConfig& t) {
    if (!t.review.is_set()) return "";
    if (t.review.status == "approved") return "  ✓";
    if (t.review.status == "rejected") return "  ✗";
    return "  ●";
}

/// Feuille de style de l'interface (par-dessus la feuille par défaut de libtui).
/// Les couleurs viennent toutes du thème : changer de thème change tout.
constexpr const char* kCss = R"TCSS(
/* --- tons sémantiques (classes posées par set_tone) --- */
.-accent  { color: $text-accent; }
.-info    { color: $text-secondary; }
.-muted   { color: $text-muted; }
.-success { color: $text-success; }
.-warning { color: $text-warning; }
.-error   { color: $text-error; }

Header { background: $panel; icon-color: $accent; }
#status { padding: 0 1; color: $text-muted; }
#title { color: $text-accent; text-style: bold; }
#help { background: $boost; padding: 0 1; }

/* --- arbre de navigation --- */
NavTree {
    background: $surface;
    accent-color: $text-accent;
    marker-color: $accent;
    info-color: $text-secondary;
    muted-color: $text-muted;
    success-color: $text-success;
    warning-color: $text-warning;
    error-color: $text-error;
    hover-background: $boost;
    cursor-background: $block-cursor-blurred-background;
    cursor-blurred-background: $block-cursor-blurred-background;
    cursor-blurred-color: $text;
}
NavTree:focus {
    cursor-background: $block-cursor-background;
    cursor-color: $block-cursor-foreground;
    cursor-text-style: bold;
}

/* --- champs --- */
ChoiceField { background: $surface-lighten-1; arrow-color: $text-accent; }
ChoiceField:hover { background: $surface-lighten-2; }
ChoiceField:focus { background: $primary-darken-1; color: auto 95%; arrow-color: $accent-lighten-2; text-style: bold; }
Input { background: $surface-lighten-1; }
Input:hover { background: $surface-lighten-2; }
Input:focus { background: $primary-background-darken-1; }
.section { color: $text-accent; text-style: bold; }
.field-label { color: $text-muted; }

/* --- boutons : barre d'actions compacte, listes de commandes alignées --- */
.actions Button { padding: 0 1; min-width: 0; }
.commands Button { width: 100%; text-align: left; padding: 0 1; }

Border { border-color: $panel-lighten-2; }
Border:focus-within { border-color: $accent; border-title-color: $accent; }
)TCSS";

/// Fichier du thème mémorisé ($XDG_CONFIG_HOME/cpp-maker/tui-theme).
fs::path config_dir() {
    if (const char* x = std::getenv("XDG_CONFIG_HOME"); x && *x) return fs::path(x) / "cpp-maker";
    if (const char* h = std::getenv("HOME"); h && *h) return fs::path(h) / ".config" / "cpp-maker";
    return {};
}

std::string saved_theme() {
    auto dir = config_dir();
    std::string text;
    if (dir.empty() || !util::read_file(dir / "tui-theme", text)) return {};
    return util::trim(text);
}

void save_theme(const std::string& name) {
    auto dir = config_dir();
    if (dir.empty()) return;
    std::error_code ec;
    fs::create_directories(dir, ec);
    util::write_file(dir / "tui-theme", name + "\n");
}

/// Champ d'une boîte de dialogue : texte libre ou choix.
struct PromptField {
    std::string label;
    std::string value;
    std::vector<std::string> choices;   ///< vide = saisie libre
};

/// Fichiers de configuration présents sous la racine (même si le projet est invalide).
std::vector<fs::path> config_files(const fs::path& root, const std::vector<std::string>& layers_dirs, const std::vector<std::string>& apps_dirs) {
    std::vector<fs::path> r;
    std::error_code ec;
    if (fs::exists(root / model::kProjectFile, ec)) r.push_back(root / model::kProjectFile);
    for (auto& libs : layers_dirs)
        for (auto& L : fs::directory_iterator(root / libs, ec)) {
            if (!L.is_directory()) continue;
            if (fs::exists(L.path() / model::kLayerFile, ec)) r.push_back(L.path() / model::kLayerFile);
            for (auto& m : fs::directory_iterator(L.path(), ec))
                if (m.is_directory() && fs::exists(m.path() / model::kModuleFile, ec)) r.push_back(m.path() / model::kModuleFile);
        }
    for (auto& apps : apps_dirs)
        for (auto& a : fs::directory_iterator(root / apps, ec))
            if (a.is_directory() && fs::exists(a.path() / model::kApplicationFile, ec)) r.push_back(a.path() / model::kApplicationFile);
    std::sort(r.begin(), r.end());
    return r;
}

std::string signature_of(const std::vector<fs::path>& files) {
    std::string s;
    std::error_code ec;
    for (auto& f : files) s += f.string() + ":" + std::to_string(fs::last_write_time(f, ec).time_since_epoch().count()) + ";";
    return s;
}

std::string first_line(const std::string& s) {
    auto t = util::trim(term::strip_ansi(s));
    return t.substr(0, t.find('\n'));
}

/// Fichier de configuration TOML (réécrit via une transaction revalidée) ?
bool is_config_file(const fs::path& f) {
    auto n = f.filename().string();
    return n == model::kProjectFile || n == model::kLayerFile || n == model::kModuleFile || n == model::kApplicationFile;
}

// ============================================================================

class CppMakerTui {
public:
    CppMakerTui(tui::App& app, fs::path cwd, TuiOptions opts)
        : app_(app), cwd_(std::move(cwd)), self_(util::self_exe()), opts_(std::move(opts)) {
        // Exécutable relancé pour les actions (surcharge possible : CPP_MAKER_EXE).
        if (const char* e = std::getenv("CPP_MAKER_EXE"); e && *e) self_ = e;
    }

    void build() {
        build_config_page();
        build_toml_page();
        build_exec_page();
        build_git_page();

        tabs_ = std::make_shared<tui::Tabs>();
        tabs_->add_tab("Configuration", config_page_);
        tabs_->add_tab("Fichier TOML", toml_page_);
        tabs_->add_tab("Exécution", exec_page_);
        tabs_->add_tab("Git", git_page_);
        tabs_->set_on_change([this](size_t i) {
            if (i == last_tab_) return;
            last_tab_ = i;
            on_tab(i);
        });

        header_ = std::make_shared<tui::Header>("cpp-maker", std::string(cppmaker::kVersion));
        header_->set_icon("◆");
        header_->set_on_icon_click([this] { show_help(); });

        status_ = label("", Tone::Muted);
        status_->set_id("status");

        // Pied : raccourcis cliquables (les onglets se choisissent aussi à la souris).
        footer_ = std::make_shared<tui::Footer>();
        footer_->set_bindings({
            {"^S", "Enregistrer", [this] { save(); }},
            {"^Z", "Annuler", [this] { revert(); }},
            {"F5", "Compiler", [this] { run_in_terminal("build", true); }},
            {"F6", "Exécuter", [this] { run_in_terminal("run", true); }},
            {"F7", "Tester", [this] { run_in_terminal("test", true); }},
            {"F9", "Thème", [this] { choose_theme(); }},
            {"F1", "Aide", [this] { show_help(); }},
            {"^Q", "Quitter", [this] { request_quit(); }},
        });

        auto main = std::make_shared<tui::Vertical>();
        main->add_child(header_, LayoutParams::fixed(1));
        main->add_child(tabs_, LayoutParams::stretch(1));
        main->add_child(status_, LayoutParams::fixed(1));
        main->add_child(footer_, LayoutParams::fixed(1));

        dialog_ = std::make_shared<tui::Dialog>();
        dialog_->set_focus_manager(&app_.focus_manager());
        auto root = std::make_shared<tui::Stack>();
        root->add_child(main);
        root->add_child(dialog_);
        app_.set_root(root);
        app_.set_global_key_handler([this](const tui::KeyEvent& e) { return on_key(e); });

        app_.add_timer(std::chrono::milliseconds(40), true, [this] { term_->pump(); });
        app_.add_timer(std::chrono::milliseconds(1500), true, [this] { watch_files(); });
        app_.add_timer(std::chrono::milliseconds(3000), true, [this] {
            if (tabs_->active_index() == kTabGit && !dialog_->showing()) {
                std::string keep = git_nav_->selected_id();
                refresh_git();
                git_nav_->select_id(keep);
            }
        });
        setup_style();

        auto root_dir = model::Workspace::find_root(cwd_);
        if (root_dir) {
            root_ = *root_dir;
            reload("project");
        } else {
            root_ = cwd_;
            show_no_project();
        }
        app_.focus_manager().set_focus(nav_);
    }

private:
    tui::App& app_;
    fs::path cwd_, root_, self_;
    TuiOptions opts_;
    std::shared_ptr<tui::Header> header_;
    std::shared_ptr<tui::Footer> footer_;
    std::function<void()> dialog_cancel_;
    std::optional<model::Workspace> ws_;
    std::string load_error_;
    std::vector<std::string> errors_;

    std::shared_ptr<tui::Tabs> tabs_;
    size_t last_tab_ = 0;
    std::shared_ptr<tui::Label> status_;
    std::shared_ptr<tui::Dialog> dialog_;
    std::function<bool(const std::vector<std::string>&)> dialog_ok_;
    std::vector<std::shared_ptr<FormInput>> dialog_inputs_;
    std::vector<std::shared_ptr<ChoiceField>> dialog_choices_;
    std::vector<int> dialog_kinds_;   // 0 = saisie, 1 = choix

    // --- Configuration
    std::shared_ptr<tui::Widget> config_page_;
    std::shared_ptr<NavTree> nav_;
    std::shared_ptr<tui::Label> title_;
    std::shared_ptr<TextBlock> desc_, help_;
    std::shared_ptr<tui::Horizontal> actions_;
    std::shared_ptr<tui::Vertical> actions_host_;
    std::shared_ptr<tui::ScrollableVertical> form_host_;
    std::unique_ptr<Form> form_;
    fs::path form_file_;
    std::string current_id_;

    // --- TOML
    std::shared_ptr<tui::Widget> toml_page_;
    std::shared_ptr<tui::CodeEditor> editor_;
    std::shared_ptr<tui::Label> editor_title_;
    fs::path editor_file_;
    bool editor_open_ = false;

    // --- Exécution
    std::shared_ptr<tui::Widget> exec_page_;

    // --- Git
    std::shared_ptr<tui::Widget> git_page_;
    std::shared_ptr<NavTree> git_nav_;
    std::shared_ptr<tui::Label> git_branch_;
    std::shared_ptr<TextBlock> git_info_;
    std::map<std::string, std::vector<fs::path>> git_paths_;   ///< élément → fichiers
    bool git_repo_ = false;

    // --- Règles pre-commit (arbre de configuration)
    struct RuleNode {
        fs::path file;
        schema::FileKind kind;
        std::string name, scope, scope_option, scope_value;
    };
    std::vector<RuleNode> rule_nodes_;
    std::shared_ptr<tui::Terminal> term_;
    std::shared_ptr<ChoiceField> opt_mode_, opt_tc_, opt_target_, opt_san_, opt_verbose_, opt_keep_;
    std::shared_ptr<FormInput> cmdline_;
    bool term_ready_ = false;

    std::string signature_;
    bool quit_armed_ = false;

    // ========================================================================
    // Style et thème
    // ========================================================================

    void setup_style() {
        app_.use_default_stylesheet();
        app_.set_css(kCss, "<cpp-maker>");
        // Thème : option --theme, sinon celui mémorisé, sinon planor (palette historique).
        std::string theme = !opts_.theme.empty() ? opts_.theme : saved_theme();
        if (theme.empty() || !app_.set_theme(theme)) app_.set_theme("planor");
        // CSS utilisateur : --css, et ~/.config/cpp-maker/tui.tcss s'il existe (rechargés à chaud).
        std::error_code ec;
        if (auto dir = config_dir(); !dir.empty() && fs::exists(dir / "tui.tcss", ec)) app_.load_css_file(dir / "tui.tcss", true);
        if (!opts_.css.empty()) app_.load_css_file(opts_.css, true);
        app_.set_on_theme_change([this](const std::string&) { apply_theme_colors(); });
        apply_theme_colors();
    }

    /// Widgets à couleurs fixées par code (éditeur) : suivent la palette du thème.
    void apply_theme_colors() {
        const auto& t = app_.role_theme();
        editor_->set_colors(t.resolve(tui::ColorRole::Accent), t.resolve(tui::ColorRole::Foreground),
                            t.resolve(tui::ColorRole::Muted));
    }

    /// Sélecteur de thème : aperçu immédiat, Entrée / double-clic pour garder,
    /// Échap pour revenir au thème précédent.
    void choose_theme() {
        auto names = tui::themes::names();
        std::string before = app_.theme_name();
        auto list = std::make_shared<tui::ListView>();
        std::vector<tui::ListItem> items;
        size_t cur = 0;
        for (size_t i = 0; i < names.size(); ++i) {
            const auto* spec = tui::themes::find(names[i]);
            items.push_back({names[i], "", true, spec && spec->dark ? "●" : "○"});
            if (names[i] == before) cur = i;
        }
        list->set_items(items);
        list->set_selected_index(cur);
        list->set_on_select([this, names](size_t i) {
            if (i < names.size()) app_.set_theme(names[i]);
        });
        auto keep = [this] {
            save_theme(app_.theme_name());
            dialog_cancel_ = nullptr;
            close_dialog();
            set_status("✓ thème " + app_.theme_name() + " (mémorisé)", Tone::Success);
        };
        list->set_on_activate([keep](size_t) { keep(); });
        auto body = std::make_shared<tui::Vertical>();
        body->add_child(label("● sombre · ○ clair · aperçu immédiat", Tone::Muted), LayoutParams::fixed(1));
        body->add_child(list, LayoutParams::stretch(1));
        auto buttons = std::make_shared<tui::Horizontal>();
        buttons->add_class("actions");
        buttons->add_child(button("Garder", keep, "-primary"), LayoutParams::auto_size());
        buttons->add_child(label(" "), LayoutParams::fixed(1));
        buttons->add_child(button("Annuler", [this] { close_dialog(); }), LayoutParams::auto_size());
        body->add_child(buttons, LayoutParams::fixed(1));
        dialog_ok_ = nullptr;
        dialog_kinds_.clear();
        dialog_cancel_ = [this, before] { app_.set_theme(before); };
        dialog_->set_title(" Thème ");
        dialog_->set_content(body);
        dialog_->set_size(tui::Size{40, std::min<int>(static_cast<int>(names.size()) + 4, 24)});
        dialog_->show();
        app_.focus_manager().set_focus(list);
    }

    void request_quit() {
        bool dirty = (form_ && form_->dirty()) || (editor_open_ && editor_->tab_count() && editor_->is_dirty(0));
        if (dirty && !quit_armed_) {
            quit_armed_ = true;
            set_status("Modifications non enregistrées : Ctrl+Q à nouveau pour quitter quand même", Tone::Warning);
            return;
        }
        term_->stop();
        app_.quit(0);
    }

    // ========================================================================
    // Construction des pages
    // ========================================================================

    void build_config_page() {
        nav_ = std::make_shared<NavTree>();
        nav_->set_on_select([this](const std::string& id) { on_select(id); });
        nav_->set_on_activate([this](const std::string& id) {
            if (id == "new") return new_project();
            if (auto f = form_ ? form_->first_field() : nullptr) app_.focus_manager().set_focus(f);
        });

        title_ = label("", Tone::Accent);
        title_->set_id("title");
        desc_ = text_block("", Tone::Muted);
        help_ = text_block("", Tone::Info);
        help_->set_id("help");
        actions_host_ = std::make_shared<tui::Vertical>();
        form_host_ = std::make_shared<tui::ScrollableVertical>();

        auto right = std::make_shared<tui::Vertical>();
        right->add_child(title_, LayoutParams::fixed(1));
        right->add_child(desc_, LayoutParams::fixed(2));
        right->add_child(actions_host_, LayoutParams::fixed(1));
        right->add_child(label(""), LayoutParams::fixed(1));
        right->add_child(framed(form_host_, "Paramètres"), LayoutParams::stretch(1));
        right->add_child(help_, LayoutParams::fixed(2));
        right->set_inline_style("padding: 0 1");

        auto row = std::make_shared<tui::Horizontal>();
        row->add_child(framed(nav_, "Projet"), LayoutParams::fixed(36));
        row->add_child(right, LayoutParams::stretch(1));
        config_page_ = row;
    }

    void build_toml_page() {
        editor_ = std::make_shared<tui::CodeEditor>();
        editor_title_ = label("Sélectionnez un élément dans l'onglet Configuration.", Tone::Muted);
        auto v = std::make_shared<tui::Vertical>();
        v->add_child(editor_title_, LayoutParams::fixed(1));
        v->add_child(editor_, LayoutParams::stretch(1));
        v->add_child(label("Ctrl+S : enregistrer (un fichier TOML n'est écrit que si tout le projet reste valide) · "
                           "Ctrl+Z (hors éditeur) : recharger le fichier", Tone::Muted),
                     LayoutParams::fixed(1));
        toml_page_ = v;
    }

    void build_exec_page() {
        term_ = std::make_shared<tui::Terminal>();
        opt_mode_ = std::make_shared<ChoiceField>();
        opt_tc_ = std::make_shared<ChoiceField>();
        opt_target_ = std::make_shared<ChoiceField>();
        opt_san_ = std::make_shared<ChoiceField>();
        opt_san_->set_choices({"", "address,undefined", "address", "undefined", "thread", "leak", "memory", "all"},
                              {"aucun", "address,undefined", "address", "undefined", "thread", "leak", "memory (clang)", "all (une passe/runtime)"});
        opt_verbose_ = std::make_shared<ChoiceField>();
        opt_verbose_->set_choices({"", "-v"}, {"non", "oui"});
        opt_keep_ = std::make_shared<ChoiceField>();
        opt_keep_->set_choices({"", "-k"}, {"non", "oui"});

        auto left = std::make_shared<tui::Vertical>();
        left->add_class("commands");
        auto opt_row = [&](const std::string& name, std::shared_ptr<tui::Widget> w) {
            auto r = std::make_shared<tui::Horizontal>();
            r->add_child(label(name, Tone::Muted), LayoutParams::fixed(12));
            r->add_child(std::move(w), LayoutParams::stretch(1));
            left->add_child(r, LayoutParams::fixed(1));
        };
        opt_row("Mode", opt_mode_);
        opt_row("Toolchain", opt_tc_);
        opt_row("Cible", opt_target_);
        opt_row("Sanitizers", opt_san_);
        opt_row("Verbeux", opt_verbose_);
        opt_row("Continuer", opt_keep_);
        left->add_child(label(""), LayoutParams::fixed(1));
        struct B { const char* label; const char* cmd; };
        for (auto b : {B{"Compiler        F5", "build"}, B{"Exécuter        F6", "run"}, B{"Tester          F7", "test"},
                       B{"Tests multi-modes", "check"}, B{"Nettoyer", "clean"}, B{"Paquets", "package"},
                       B{"Tailles (ELF)", "size"}, B{"compile_commands", "export compdb"}, B{"Graphe", "graph"},
                       B{"Valider la config", "config validate"}, B{"Diagnostic", "doctor"}}) {
            std::string cmd = b.cmd;
            left->add_child(button(b.label, [this, cmd] { run_in_terminal(cmd, true); },
                                   cmd == "build" || cmd == "run" || cmd == "test" ? "-primary" : ""),
                            LayoutParams::fixed(1));
        }
        left->add_child(label(""), LayoutParams::fixed(1));
        left->add_child(label("Commande libre (Entrée) :", Tone::Muted), LayoutParams::fixed(1));
        cmdline_ = std::make_shared<FormInput>();
        cmdline_->set_placeholder("cpp-maker …");
        cmdline_->set_on_submit([this](const std::string& t) {
            std::string line = util::trim(t);
            if (line.starts_with("cpp-maker ")) line = line.substr(10);
            if (!line.empty()) run_in_terminal(line, false);
            cmdline_->set_text("");
        });
        left->add_child(cmdline_, LayoutParams::fixed(1));
        left->add_child(label(""), LayoutParams::stretch(1));

        auto row = std::make_shared<tui::Horizontal>();
        row->add_child(framed(left, "Commandes"), LayoutParams::fixed(42));
        row->add_child(framed(term_, "Terminal (Tab/Maj+Tab pour sortir)"), LayoutParams::stretch(1));
        exec_page_ = row;
    }

    void build_git_page() {
        git_nav_ = std::make_shared<NavTree>();
        git_nav_->set_on_activate([this](const std::string&) { git_toggle_selected(); });
        git_branch_ = label("", Tone::Accent);
        git_branch_->set_inline_style("text-style: bold; padding: 0 1");
        git_info_ = text_block("", Tone::Muted);

        auto left = std::make_shared<tui::Vertical>();
        left->add_child(git_branch_, LayoutParams::fixed(1));
        left->add_child(framed(git_nav_, "Modifications (Entrée : indexer / désindexer)"), LayoutParams::stretch(1));

        auto right = std::make_shared<tui::Vertical>();
        right->add_class("commands");
        struct B { const char* label; std::function<void()> fn; };
        std::vector<B> buttons = {
            {"Indexer la sélection", [this] { git_stage_selected(true); }},
            {"Désindexer la sélection", [this] { git_stage_selected(false); }},
            {"Tout indexer", [this] { git_stage_all(); }},
            {"Valider (commit)…", [this] { git_commit_dialog(); }},
            {"Simuler le pre-commit", [this] { run_in_terminal("git pre-commit -n", false); }},
            {"Pre-commit sur tout", [this] { run_in_terminal("git pre-commit --all", false); }},
            {"Règles pre-commit…", [this] { switch_tab(kTabConfig); nav_->select_id("group:rules"); show_node("group:rules"); }},
            {"Journal", [this] { run_in_terminal("git log" + git_selected_target(), false); }},
            {"Différences", [this] { run_in_terminal("git diff" + git_selected_target(), false); }},
            {"Pousser (push)", [this] { run_in_terminal("git push", false); }},
            {"Tirer (pull)", [this] { run_in_terminal("git pull", false); }},
            {"Installer le crochet", [this] { run_self({"git", "hook", "install"}, current_id_); refresh_git(); }},
            {"Initialiser (git init)", [this] { run_self({"git", "init"}, current_id_); refresh_git(); }},
            {"Actualiser", [this] { refresh_git(); }},
        };
        for (auto& b : buttons) {
            std::string l = b.label;
            right->add_child(button(l, b.fn, l.starts_with("Valider") ? "-primary" : l.starts_with("Indexer") || l.starts_with("Tout") ? "-success" : ""),
                             LayoutParams::fixed(1));
        }
        right->add_child(label(""), LayoutParams::fixed(1));
        right->add_child(git_info_, LayoutParams::stretch(1));

        auto row = std::make_shared<tui::Horizontal>();
        row->add_child(left, LayoutParams::stretch(1));
        row->add_child(framed(right, "Actions"), LayoutParams::fixed(44));
        git_page_ = row;
    }

    // ========================================================================
    // Chargement
    // ========================================================================

    void reload(const std::string& select) {
        try {
            ws_ = model::Workspace::load_root(root_, kLoad);
            load_error_.clear();
            errors_ = ws_->validate();
        } catch (const std::exception& e) {
            ws_.reset();
            load_error_ = e.what();
            errors_.clear();
        }
        header_->set_subtitle(ws_ ? std::format("{} {}", ws_->project.name, ws_->project.version) : "aucun projet");
        signature_ = signature_of(config_files(root_, ws_ ? ws_->project.layers_dirs : std::vector<std::string>{"libs"}, ws_ ? ws_->project.apps_dirs : std::vector<std::string>{"apps"}));
        rebuild_nav();
        refresh_exec_options();
        std::string target = select.empty() ? current_id_ : select;
        nav_->select_id(target);
        current_id_.clear();
        show_node(nav_->selected_id());
        if (!load_error_.empty()) set_status("✗ " + first_line(load_error_), Tone::Error);
        else if (!errors_.empty())
            set_status(std::format("✗ {} erreur(s) de configuration — sélectionnez « Projet » pour le détail", errors_.size()), Tone::Error);
    }

    void rebuild_nav() {
        std::vector<NavItem> items;
        if (!ws_) {
            items.push_back({"error", "⚠ projet illisible", 0, Tone::Error, ""});
            for (auto& f : config_files(root_, {"libs"}, {"apps"}))
                items.push_back({"file:" + f.string(), util::display(f, root_), 1, Tone::Normal, ""});
            nav_->set_items(items);
            return;
        }
        auto& ws = *ws_;
        auto& p = ws.project;
        items.push_back({"project", "◆ " + p.name, 0, Tone::Accent, p.version});
        items.push_back({"readme:project", "README.md", 1, Tone::Muted, ""});
        items.push_back({"project:build", "Compilation", 1, Tone::Normal, ""});
        items.push_back({"project:tests", "Tests", 1, Tone::Normal, ""});
        items.push_back({"project:package", "Paquets", 1, Tone::Normal, ""});
        items.push_back({"group:modes", "Modes", 1, Tone::Info, std::to_string(p.modes.size())});
        for (auto& m : p.modes) items.push_back({"mode:" + m.name, m.name, 2, m.builtin ? Tone::Muted : Tone::Normal, m.builtin ? "prédéfini" : ""});
        items.push_back({"group:toolchains", "Toolchains", 1, Tone::Info, std::to_string(p.toolchains.size())});
        for (auto& [n, t] : p.toolchains) items.push_back({"toolchain:" + n, n, 2, Tone::Normal, n == p.default_toolchain ? "défaut" : ""});
        items.push_back({"group:deploys", "Déploiements", 1, Tone::Info, std::to_string(p.deploys.size())});
        for (auto& [n, d] : p.deploys) items.push_back({"deploy:" + n, n, 2, Tone::Normal, d.method});
        items.push_back({"group:generate", "Génération de code", 1, Tone::Info, std::to_string(p.generate.size())});
        for (auto& g : p.generate) items.push_back({"generate:" + g.name, g.name, 2, Tone::Normal, ""});
        rule_nodes_.clear();
        auto rules = vcs::all_rules(ws);
        items.push_back({"group:rules", "Règles pre-commit", 1, Tone::Info, std::to_string(rules.size())});
        for (auto& r : rules) {
            RuleNode n;
            n.file = r.rule->file;
            n.name = r.rule->name;
            n.scope = r.scope;
            std::string fname = n.file.filename().string();
            n.kind = fname == model::kLayerFile ? schema::FileKind::Layer
                     : fname == model::kModuleFile ? schema::FileKind::Module
                     : fname == model::kApplicationFile ? schema::FileKind::Application : schema::FileKind::Project;
            if (!r.layer.empty()) { n.scope_option = "--layer"; n.scope_value = r.layer; }
            if (!r.target.empty()) {
                n.scope_option = ws.find(r.target)->is_app() ? "--app" : "--module";
                n.scope_value = r.target;
            }
            items.push_back({"rule:" + std::to_string(rule_nodes_.size()), n.name, 2, r.rule->enabled ? Tone::Normal : Tone::Muted,
                             r.scope == "projet" ? (r.rule->enabled ? "" : "désactivée") : r.scope});
            rule_nodes_.push_back(std::move(n));
        }
        items.push_back({"group:layers", "Layers", 0, Tone::Accent, std::to_string(ws.layers.size())});
        for (auto& L : ws.layers) {
            int r = ws.layer_rank(L.name);
            items.push_back({"layer:" + L.name, "▸ " + L.name, 1, Tone::Info, r >= 0 ? std::format("couche {}", r) : ""});
            items.push_back({"readme:layer:" + L.name, "README.md", 2, Tone::Muted, ""});
            for (auto* m : ws.modules_of(L.name)) {
                auto tone = m->review.status == "approved" ? Tone::Success
                            : m->review.status == "rejected" ? Tone::Error
                            : m->review.status == "requested" ? Tone::Warning : Tone::Normal;
                items.push_back({"module:" + m->id, m->name + review_suffix(*m), 2, tone, m->type});
                if (!m->from_git) items.push_back({"readme:module:" + m->id, "README.md", 3, Tone::Muted, ""});
            }
        }
        auto apps = ws.applications();
        items.push_back({"group:apps", "Applications", 0, Tone::Accent, std::to_string(apps.size())});
        for (auto* a : apps) {
            auto tone = a->review.status == "approved" ? Tone::Success
                        : a->review.status == "rejected" ? Tone::Error
                        : a->review.status == "requested" ? Tone::Warning : Tone::Normal;
            items.push_back({"app:" + a->id, a->name + review_suffix(*a), 1, tone, ""});
            items.push_back({"readme:app:" + a->id, "README.md", 2, Tone::Muted, ""});
        }
        nav_->set_items(items);
    }

    void refresh_exec_options() {
        std::vector<std::string> modes, tcs, targets = {""}, target_labels = {"(dossier courant / tout)"};
        if (ws_) {
            for (auto& m : ws_->project.modes) modes.push_back(m.name);
            std::stable_partition(modes.begin(), modes.end(), [&](auto& m) { return m == ws_->project.default_mode; });
            tcs.push_back(ws_->project.default_toolchain);
            for (auto& [n, t] : ws_->project.toolchains) util::push_unique(tcs, n);
            util::push_unique(tcs, std::vector<std::string>{"host", "gcc", "clang", "all"});
            targets.push_back("all");
            target_labels.push_back("all (tout le projet)");
            for (auto& t : ws_->targets) {
                targets.push_back(t.id);
                target_labels.push_back(t.id + (t.is_app() ? "  (app)" : "  (" + t.type + ")"));
            }
            for (auto& L : ws_->layers) {
                targets.push_back(L.name + ".*");
                target_labels.push_back(L.name + ".*  (layer)");
            }
        } else {
            modes = {"debug"};
            tcs = {"host"};
        }
        opt_mode_->set_choices(modes);
        opt_tc_->set_choices(tcs);
        opt_target_->set_choices(targets, target_labels);
    }

    // ========================================================================
    // Sélection et formulaires
    // ========================================================================

    void on_select(const std::string& id) {
        if (form_ && form_->dirty()) {
            nav_->select_id(current_id_);
            set_status("Modifications non enregistrées : Ctrl+S pour enregistrer, Ctrl+Z pour les abandonner", Tone::Warning);
            return;
        }
        show_node(id);
    }

    void set_actions(const std::vector<std::pair<std::string, std::function<void()>>>& actions) {
        actions_ = std::make_shared<tui::Horizontal>();
        actions_->add_class("actions");
        for (auto& [text, fn] : actions) {
            auto f = fn;
            actions_->add_child(button(text, [f] { f(); }, variant_for(text)), LayoutParams::auto_size());
            actions_->add_child(label(" "), LayoutParams::fixed(1));
        }
        actions_->add_child(label(""), LayoutParams::stretch(1));
        for (auto& c : actions_host_->children()) actions_host_->remove_child(c);
        actions_host_->add_child(actions_, LayoutParams::fixed(1));
    }

    void set_form(schema::FileKind kind, const fs::path& file, std::vector<FormSection> sections, std::set<std::string> hidden = {}) {
        form_ = std::make_unique<Form>(kind, std::move(sections), std::move(hidden));
        form_file_ = file;
        form_->set_on_help([this](const std::string& h) { help_->set_text(h); });
        form_->set_on_change([this] {
            if (form_ && form_->dirty()) set_status("● modifié — Ctrl+S pour enregistrer", Tone::Warning);
        });
        try {
            std::error_code ec;
            auto doc = fs::exists(file, ec) ? toml::Document::load(file) : toml::Document::from_text("", file);
            form_->load(doc);
        } catch (const std::exception& e) {
            set_status("✗ " + first_line(e.what()), Tone::Error);
        }
        form_host_->set_child(form_->widget());
        form_host_->scroll_to(0, 0);
    }

    void clear_form(const std::string& text = {}) {
        form_.reset();
        form_file_.clear();
        auto v = std::make_shared<tui::Vertical>();
        v->add_child(text_block(text, Tone::Muted), LayoutParams::stretch(1));
        form_host_->set_child(v);
    }

    void show_node(const std::string& id) {
        current_id_ = id;
        help_->set_text("");
        set_actions({});
        if (!ws_) {
            std::string err = util::replace_all(load_error_, root_.string() + "/", "");
            title_->set_text("Projet illisible");
            desc_->set_text(err);
            set_tone(*desc_, Tone::Error);
            clear_form("Corrigez le fichier dans l'onglet F3 (sélectionnez-le à gauche), puis Ctrl+S.\n\n" + err);
            if (id.starts_with("file:")) open_in_editor(id.substr(5));
            return;
        }
        auto& ws = *ws_;
        auto& p = ws.project;
        auto P = schema::FileKind::Project;
        auto key = id.substr(id.find(':') + 1);
        auto run_btn = [this](const std::string& cmd) { return [this, cmd] { run_in_terminal(cmd, false); }; };

        if (id.starts_with("readme:")) {
            fs::path file;
            std::string rest = id.substr(7);
            if (rest == "project") file = root_ / "README.md";
            else if (rest.starts_with("layer:")) {
                if (auto* L = ws.find_layer(rest.substr(6))) file = ws.root / L->dir / "README.md";
            } else if (rest.starts_with("module:")) {
                if (auto* t = ws.find(rest.substr(7)); t && !t->from_git) file = ws.abs(*t) / "README.md";
            } else if (rest.starts_with("app:")) {
                if (auto* t = ws.find(rest.substr(4)); t) file = ws.abs(*t) / "README.md";
            }
            if (file.empty()) return clear_form();
            std::error_code ec;
            title_->set_text("README.md");
            desc_->set_text(util::display(file, root_) + (fs::exists(file, ec) ? "" : "  (nouveau)"));
            set_tone(*desc_, Tone::Muted);
            set_actions({{"Éditer (F3)", [this] { switch_tab(kTabToml); }}});
            clear_form("README de cet élément — éditez-le dans l'onglet « Fichier TOML » (F3), puis Ctrl+S pour enregistrer.");
            open_in_editor(file);
            return;
        }

        if (id == "project") {
            title_->set_text("Projet " + p.name);
            desc_->set_text(errors_.empty() ? util::display(p.file, root_) + " — configuration valide"
                                            : util::join(errors_, "\n"));
            set_tone(*desc_, errors_.empty() ? Tone::Muted : Tone::Error);
            set_actions({{"Valider", run_btn("config validate")}, {"Diagnostic", run_btn("doctor")},
                         {"Informations", run_btn("info")}});
            set_form(P, p.file, {{{"project"}, "project", ""}, {{"paths"}, "paths", ""}, {{"layers"}, "layers", ""},
                                 {{"dependencies"}, "dependencies", ""}});
            open_in_editor(p.file);
            return;
        }
        set_tone(*desc_, Tone::Muted);
        auto simple = [&](const std::string& sec, const std::string& title, const std::string& desc) {
            title_->set_text(title);
            desc_->set_text(desc);
            set_form(P, p.file, {{{sec}, sec, ""}});
            open_in_editor(p.file);
        };
        if (id == "project:build") return simple("build", "Compilation", "Options communes à toutes les cibles : mode et toolchain par défaut, avertissements, defines, options.");
        if (id == "project:tests") return simple("tests", "Tests", "Framework (builtin : #include <cpp-maker/test.hpp>), délai et environnement des exécutables de test.");
        if (id == "project:package") return simple("package", "Paquets", "Formats produits par « cpp-maker package » dans dist/ et métadonnées Debian.");

        if (id == "group:modes") {
            title_->set_text("Modes de compilation");
            desc_->set_text("Prédéfinis : debug, release, relwithdebinfo, minsize, asan, tsan, ubsan, msan, lsan, valgrind. "
                            "Sélectionner un mode prédéfini permet de le surcharger.");
            set_actions({{"+ Mode", [this] { add_mode(); }}});
            return clear_form("Choisissez un mode à gauche, ou créez-en un.");
        }
        if (id.starts_with("mode:")) {
            auto* m = p.find_mode(key);
            title_->set_text("Mode " + key);
            desc_->set_text(std::format("{}flags : {} · defines : {}{}", m && m->builtin ? "prédéfini — " : "",
                                        m ? util::join(m->flags, " ") : "", m ? util::join(m->defines, " ") : "",
                                        m && !m->runner.empty() ? " · lanceur " + m->runner : ""));
            std::vector<std::pair<std::string, std::function<void()>>> acts = {
                {"Compiler", [this, key] { run_in_terminal("build " + key, false); }},
                {"Tester", [this, key] { run_in_terminal("test " + key, false); }}};
            if (m && !m->builtin) acts.push_back({"Supprimer", [this, key] { remove_table("build.modes." + key, "mode " + key); }});
            set_actions(acts);
            set_form(P, p.file, {{{"build", "modes", key}, "build.modes.*", ""}});
            open_in_editor(p.file);
            return;
        }
        if (id == "group:toolchains") {
            title_->set_text("Toolchains");
            desc_->set_text("Intégrées : host, gcc, clang, et tout triplet du PATH (aarch64-linux-gnu…). "
                            "Déclarez ici les compilateurs croisés ou une version précise.");
            set_actions({{"+ Toolchain", [this] { add_toolchain(); }}, {"Détectées", run_btn("toolchain list")}});
            return clear_form("Choisissez une toolchain à gauche, ou déclarez-en une.");
        }
        if (id.starts_with("toolchain:")) {
            title_->set_text("Toolchain " + key);
            desc_->set_text("Compilateurs, outils et cible ; « cpp-maker toolchain show " + key + " » affiche la résolution.");
            set_actions({{"Résolution", [this, key] { run_in_terminal("toolchain show " + key, false); }},
                         {"Par défaut", [this, key] { run_self({"config", "set", "build.default_toolchain", key}, "project"); }},
                         {"Supprimer", [this, key] { remove_table("toolchains." + key, "toolchain " + key); }}});
            set_form(P, p.file, {{{"toolchains", key}, "toolchains.*", ""}});
            open_in_editor(p.file);
            return;
        }
        if (id == "group:deploys") {
            title_->set_text("Déploiements");
            desc_->set_text("Cibles de « cpp-maker deploy <nom> » : rsync, scp, copie locale ou script.");
            set_actions({{"+ Déploiement", [this] { add_deploy(); }}});
            return clear_form("Choisissez un déploiement à gauche, ou créez-en un.");
        }
        if (id.starts_with("deploy:")) {
            title_->set_text("Déploiement " + key);
            desc_->set_text("Variables utilisables dans path/after : {name} {version} {toolchain} {mode} {host} {path}.");
            set_actions({{"Simuler", [this, key] { run_in_terminal("deploy " + key + " --dry-run", false); }},
                         {"Déployer", [this, key] { run_in_terminal("deploy " + key, false); }},
                         {"Supprimer", [this, key] { remove_table("deploy." + key, "déploiement " + key); }}});
            set_form(P, p.file, {{{"deploy", key}, "deploy.*", ""}});
            open_in_editor(p.file);
            return;
        }
        if (id == "group:generate") {
            title_->set_text("Génération de code");
            desc_->set_text("Commandes exécutées avant le build quand une entrée a changé (protoc, scripts…).");
            set_actions({{"+ Étape", [this] { add_generate(); }}});
            return clear_form("Choisissez une étape à gauche, ou créez-en une.");
        }
        if (id.starts_with("generate:")) {
            title_->set_text("Étape de génération " + key);
            desc_->set_text("Exécutée depuis la racine du projet ; « always » la relance à chaque fois.");
            set_actions({{"Supprimer", [this, key] { remove_table("generate." + key, "étape " + key); }}});
            set_form(P, p.file, {{{"generate", key}, "generate.*", ""}});
            open_in_editor(p.file);
            return;
        }
        if (id == "group:layers") {
            title_->set_text("Layers");
            desc_->set_text(p.layer_order.empty() ? "Aucun ordre de couches : les dépendances entre layers sont libres."
                                                  : "Ordre des couches (bas → haut) : " + util::join(p.layer_order, " → "));
            set_actions({{"+ Layer", [this] { add_layer(); }}, {"Graphe", run_btn("graph")}});
            return clear_form("Un layer regroupe des modules (bibliothèques) : libs/<layer>/<module>/.");
        }
        if (id.starts_with("layer:")) {
            auto* L = ws.find_layer(key);
            if (!L) return clear_form();
            title_->set_text("Layer " + key);
            desc_->set_text(util::display(ws.root / L->dir, root_) + " — dépendances et options héritées par ses " +
                            std::to_string(L->discovered.size()) + " module(s)");
            set_actions({{"+ Module", [this, key] { add_module(key); }},
                         {"Compiler", [this, key] { run_in_terminal("build " + key + ".*", false); }},
                         {"Tester", [this, key] { run_in_terminal("test " + key + ".*", false); }},
                         {"Renommer", [this, key] { rename("layer", key); }},
                         {"Supprimer", [this, key] { remove("layer", key); }}});
            set_form(schema::FileKind::Layer, L->file,
                     {{{"layer"}, "layer", ""}, {{"dependencies"}, "dependencies", ""}, {{"flags"}, "flags", ""}, {{"forbid"}, "forbid", ""}},
                     {"layer.name"});
            open_in_editor(L->file);
            return;
        }
        if (id.starts_with("module:") || id.starts_with("app:")) {
            auto* t = ws.find(key);
            if (!t) return clear_form();
            bool app = t->is_app();
            title_->set_text((app ? "Application " : "Module ") + t->id);
            std::string deps = t->dep_ids.empty() ? "aucune dépendance" : "dépend de " + util::join(t->dep_ids, ", ");
            auto users = ws.dependents(t->id);
            std::string rev;
            if (t->review.is_set()) {
                rev = " · revue : " + t->review.status;
                if (!t->review.reviewer.empty()) rev += " par " + t->review.reviewer;
                if (!t->review.date.empty()) rev += " (" + t->review.date + ")";
            }
            desc_->set_text(std::format("{} · {}{}{}", util::display(ws.abs(*t), root_), deps,
                                        users.empty() ? "" : " · utilisé par " + util::join(users, ", "), rev));
            std::vector<std::pair<std::string, std::function<void()>>> acts = {
                {"Compiler", [this, key] { run_in_terminal("build " + key, true); }},
                {"Tester", [this, key] { run_in_terminal("test " + key, true); }}};
            if (app) acts.push_back({"Exécuter", [this, key] { run_in_terminal("run " + key, true); }});
            acts.push_back({"Infos", [this, key] { run_in_terminal("info " + key, false); }});
            std::string sel = (app ? "app:" : "module:") + key;
            acts.push_back({"Approuver", [this, key, sel] { run_self({"review", "approve", key}, sel); }});
            acts.push_back({"Rejeter", [this, key, sel] { run_self({"review", "reject", key}, sel); }});
            acts.push_back({"Demander revue", [this, key, sel] { run_self({"review", "request", key}, sel); }});
            acts.push_back({"Renommer", [this, key, app] { rename(app ? "app" : "module", key); }});
            acts.push_back({"Supprimer", [this, key, app] { remove(app ? "app" : "module", key); }});
            set_actions(acts);
            std::vector<FormSection> secs;
            if (app) secs = {{{"application"}, "application", ""}, {{"run"}, "run", ""}};
            else secs = {{{"module"}, "module", ""}};
            for (auto s : {"dependencies", "sources", "include", "flags", "tests", "review"}) secs.push_back({{s}, s, ""});
            set_form(app ? schema::FileKind::Application : schema::FileKind::Module, t->file, secs,
                     {"module.name", "application.name"});
            open_in_editor(t->file);
            return;
        }
        if (id == "group:rules") {
            title_->set_text("Règles pre-commit");
            auto state = git_repo_or_check() ? vcs::hook_state(ws.root) : vcs::HookState::Absent;
            desc_->set_text(std::format("Exécutées par « git commit » quand un fichier indexé de leur portée est concerné. "
                                        "Crochet : {}. Mises à jour par rename/remove ; doctor les vérifie.",
                                        state == vcs::HookState::Ours ? "installé" : state == vcs::HookState::Foreign ? "tiers (non géré)" : "absent"));
            set_actions({{"+ Règle", [this] { add_rule(); }},
                         {"Simuler", run_btn("git pre-commit -n --all")},
                         {"Installer le crochet", [this] { run_self({"git", "hook", "install"}, "group:rules"); }},
                         {"Vérifier (doctor)", run_btn("doctor")}});
            return clear_form("Portée : project.toml = tous les fichiers indexés ; layer.toml / module.toml / application.toml = "
                              "ceux du layer ou de la cible. Variables : {targets} (cibles touchées), {files}, {root}.");
        }
        if (id.starts_with("rule:")) {
            size_t i = std::stoul(key);
            if (i >= rule_nodes_.size()) return clear_form();
            auto n = rule_nodes_[i];
            title_->set_text("Règle pre-commit " + n.name);
            desc_->set_text("Portée : " + n.scope + " — " + util::display(n.file, root_));
            set_actions({{"Simuler", run_btn("git pre-commit -n --all")},
                         {"Supprimer", [this, n] {
                              std::vector<std::string> a = {"git", "rule", "remove", n.name};
                              if (!n.scope_option.empty()) a.insert(a.end(), {n.scope_option, n.scope_value});
                              run_self(a, "group:rules");
                          }}});
            set_form(n.kind, n.file, {{{"git", "pre_commit", n.name}, "git.pre_commit.*", ""}});
            open_in_editor(n.file);
            return;
        }
        if (id == "group:apps") {
            title_->set_text("Applications");
            desc_->set_text("Exécutables : apps/<nom>/ ; elles utilisent les modules des layers.");
            set_actions({{"+ Application", [this] { add_app(); }}});
            return clear_form("Choisissez une application à gauche, ou créez-en une.");
        }
        clear_form();
    }

    // ========================================================================
    // Enregistrement
    // ========================================================================

    void save() {
        if (tabs_->active_index() == kTabToml) return save_editor();
        if (!form_) {
            set_status("Rien à enregistrer ici.", Tone::Muted);
            return;
        }
        if (!form_->dirty()) {
            set_status("Aucune modification.", Tone::Muted);
            return;
        }
        try {
            model::Transaction tx(root_);
            form_->apply(tx.doc(form_file_));
            tx.commit(true);
            std::string id = current_id_;
            form_.reset();
            reload(id);
            set_status("✓ " + util::display(form_file_.empty() ? root_ : form_file_, root_) + " enregistré", Tone::Success);
        } catch (const std::exception& e) {
            set_status("✗ " + first_line(e.what()) + " — rien n'a été écrit", Tone::Error);
            help_->set_text(util::replace_all(term::strip_ansi(e.what()), root_.string() + "/", ""));
        }
    }

    void open_in_editor(const fs::path& file) {
        if (editor_open_ && editor_->tab_count() > 0 && editor_->is_dirty(0)) return;   // préserve une édition en cours
        while (editor_->tab_count() > 0) editor_->close_tab(0);
        std::string text;
        util::read_file(file, text);
        editor_->add_tab(util::display(file, root_), tui::detect_language(file.filename().string()), text);
        editor_file_ = file;
        editor_open_ = true;
        editor_title_->set_text("Fichier : " + util::display(file, root_) + (fs::exists(file) ? "" : "  (nouveau)"));
        set_tone(*editor_title_, Tone::Normal);
    }

    void save_editor() {
        if (!editor_open_ || editor_->tab_count() == 0) return;
        std::string text = editor_->editor_at(0)->text();
        if (!is_config_file(editor_file_)) {
            // Fichier libre (README.md…) : écriture directe, sans validation TOML.
            util::write_file(editor_file_, text);
            editor_->mark_saved(0);
            set_status("✓ " + util::display(editor_file_, root_) + " enregistré", Tone::Success);
            return;
        }
        try {
            model::Transaction tx(root_);
            tx.doc(editor_file_).replace_text(text);
            tx.commit(true);
            editor_->mark_saved(0);
            reload(current_id_);
            set_status("✓ " + util::display(editor_file_, root_) + " enregistré", Tone::Success);
        } catch (const std::exception& e) {
            set_status("✗ " + first_line(e.what()) + " — fichier non écrit", Tone::Error);
            editor_title_->set_text("✗ " + util::replace_all(term::strip_ansi(e.what()), root_.string() + "/", ""));
            set_tone(*editor_title_, Tone::Error);
        }
    }

    void revert() {
        if (tabs_->active_index() == kTabToml) {
            if (editor_open_ && editor_->tab_count()) {
                editor_open_ = false;
                open_in_editor(editor_file_);
                set_status("Fichier rechargé depuis le disque.", Tone::Muted);
            }
            return;
        }
        std::string id = current_id_;
        form_.reset();
        reload(id);
        set_status("Modifications abandonnées.", Tone::Muted);
    }

    void watch_files() {
        auto files = config_files(root_, ws_ ? ws_->project.layers_dirs : std::vector<std::string>{"libs"}, ws_ ? ws_->project.apps_dirs : std::vector<std::string>{"apps"});
        auto sig = signature_of(files);
        if (sig == signature_) return;
        bool busy = (form_ && form_->dirty()) || (editor_open_ && editor_->tab_count() && editor_->is_dirty(0)) || dialog_->showing();
        if (busy) {
            set_status("Fichiers modifiés hors de la TUI — Ctrl+Z pour recharger (vos modifications seraient perdues)", Tone::Warning);
            signature_ = sig;
            return;
        }
        editor_open_ = false;
        reload(current_id_);
        set_status("↻ configuration rechargée (fichiers modifiés)", Tone::Info);
    }

    // ========================================================================
    // Actions (via la ligne de commande de cpp-maker)
    // ========================================================================

    bool run_self(std::vector<std::string> args, const std::string& select_after) {
        std::vector<std::string> cmd = {self_.string(), "--no-color", "-C", root_.string()};
        cmd.insert(cmd.end(), args.begin(), args.end());
        auto r = proc::capture(cmd);
        std::string out = util::trim(term::strip_ansi(r.output));
        if (r.exit_code != 0) {
            set_status("✗ " + first_line(out), Tone::Error);
            help_->set_text(out);
            return false;
        }
        form_.reset();
        editor_open_ = false;
        reload(select_after);
        auto lines = util::split(out, '\n');
        set_status(lines.empty() ? "✓ fait" : first_line(lines.front()), Tone::Success);
        help_->set_text(out);
        return true;
    }

    void prompt(const std::string& title, const std::vector<PromptField>& fields,
                std::function<bool(const std::vector<std::string>&)> ok) {
        dialog_inputs_.clear();
        dialog_choices_.clear();
        dialog_kinds_.clear();
        auto body = std::make_shared<tui::Vertical>();
        std::shared_ptr<tui::Widget> first;
        for (auto& f : fields) {
            auto row = std::make_shared<tui::Horizontal>();
            row->add_child(label(f.label, Tone::Muted), LayoutParams::fixed(24));
            if (f.choices.empty()) {
                auto in = std::make_shared<FormInput>();
                in->set_text(f.value);
                in->set_on_submit([this](const std::string&) { confirm_dialog(); });
                row->add_child(in, LayoutParams::stretch(1));
                dialog_inputs_.push_back(in);
                dialog_choices_.push_back(nullptr);
                dialog_kinds_.push_back(0);
                if (!first) first = in;
            } else {
                auto c = std::make_shared<ChoiceField>();
                c->set_choices(f.choices);
                c->set_value(f.value);
                row->add_child(c, LayoutParams::stretch(1));
                dialog_inputs_.push_back(nullptr);
                dialog_choices_.push_back(c);
                dialog_kinds_.push_back(1);
                if (!first) first = c;
            }
            body->add_child(row, LayoutParams::fixed(1));
        }
        body->add_child(label(""), LayoutParams::fixed(1));
        auto buttons = std::make_shared<tui::Horizontal>();
        buttons->add_class("actions");
        auto okb = button("Valider", [this] { confirm_dialog(); }, "-primary");
        auto cancel = button("Annuler", [this] { close_dialog(); });
        buttons->add_child(okb, LayoutParams::auto_size());
        buttons->add_child(label("  "), LayoutParams::fixed(2));
        buttons->add_child(cancel, LayoutParams::auto_size());
        buttons->add_child(label("   Entrée : valider · Échap : annuler", Tone::Muted), LayoutParams::stretch(1));
        body->add_child(buttons, LayoutParams::fixed(1));
        dialog_ok_ = std::move(ok);
        dialog_cancel_ = nullptr;
        dialog_->set_title(" " + title + " ");
        dialog_->set_content(body);
        dialog_->set_size(tui::Size{72, static_cast<int>(fields.size()) + 4});
        dialog_->show();
        if (first) app_.focus_manager().set_focus(first);
    }

    void confirm_dialog() {
        std::vector<std::string> values;
        for (size_t i = 0; i < dialog_kinds_.size(); ++i)
            values.push_back(dialog_kinds_[i] == 0 ? util::trim(dialog_inputs_[i]->text()) : dialog_choices_[i]->value());
        auto ok = dialog_ok_;
        dialog_cancel_ = nullptr;
        close_dialog();
        if (ok && !ok(values)) {
            // Échec : l'erreur est dans la barre d'état ; la boîte reste fermée.
        }
    }

    void close_dialog() {
        if (auto cancel = std::move(dialog_cancel_)) {
            dialog_cancel_ = nullptr;
            cancel();
        }
        if (dialog_->showing()) dialog_->hide();
        app_.focus_manager().set_focus(nav_);
    }

    std::vector<std::string> layer_names() const {
        std::vector<std::string> r;
        if (ws_)
            for (auto& L : ws_->layers) r.push_back(L.name);
        return r;
    }

    bool git_repo_or_check() const { return ws_ && util::have("git") && vcs::toplevel(ws_->root).has_value(); }

    void add_layer() {
        std::vector<std::string> dirs = ws_ ? ws_->project.layers_dirs : std::vector<std::string>{"libs"};
        prompt("Nouveau layer",
               {{"Nom", "", {}}, {"Description", "", {}}, {"Dépendances héritées", "", {}}, {"Dossier", dirs.front(), dirs}},
               [this](const std::vector<std::string>& v) {
                   std::vector<std::string> a = {"add", "layer", v[0], "--dir", v[3]};
                   if (!v[1].empty()) a.insert(a.end(), {"--description", v[1]});
                   if (!v[2].empty()) a.insert(a.end(), {"--deps", v[2]});
                   return run_self(a, "layer:" + v[0]);
               });
    }

    void add_module(const std::string& layer) {
        auto layers = layer_names();
        prompt("Nouveau module",
               {{"Layer", layer.empty() && !layers.empty() ? layers[0] : layer, layers},
                {"Nom", "", {}},
                {"Type", "static", {"static", "shared", "header-only"}},
                {"Langage", ws_ ? ws_->project.language : "c++", {"c++", "c"}},
                {"Dépendances", "", {}},
                {"Description", "", {}}},
               [this](const std::vector<std::string>& v) {
                   std::vector<std::string> a = {"add", "module", v[0], v[1], "--type", v[2], "--lang", v[3]};
                   if (!v[4].empty()) a.insert(a.end(), {"--deps", v[4]});
                   if (!v[5].empty()) a.insert(a.end(), {"--description", v[5]});
                   return run_self(a, "module:" + v[0] + "." + v[1]);
               });
    }

    void add_app() {
        std::vector<std::string> dirs = ws_ ? ws_->project.apps_dirs : std::vector<std::string>{"apps"};
        prompt("Nouvelle application",
               {{"Nom", "", {}}, {"Langage", ws_ ? ws_->project.language : "c++", {"c++", "c"}},
                {"Modules utilisés", "", {}}, {"Description", "", {}}, {"Dossier", dirs.front(), dirs}},
               [this](const std::vector<std::string>& v) {
                   std::vector<std::string> a = {"add", "app", v[0], "--lang", v[1], "--dir", v[4]};
                   if (!v[2].empty()) a.insert(a.end(), {"--deps", v[2]});
                   if (!v[3].empty()) a.insert(a.end(), {"--description", v[3]});
                   return run_self(a, "app:" + v[0]);
               });
    }

    void rename(const std::string& kind, const std::string& id) {
        std::string short_name = kind == "module" ? id.substr(id.find('.') + 1) : id;
        prompt("Renommer " + kind + " " + id, {{"Nouveau nom", short_name, {}}},
               [this, kind, id](const std::vector<std::string>& v) {
                   std::string sel = kind == "layer" ? "layer:" + v[0]
                                     : kind == "app" ? "app:" + v[0]
                                                     : "module:" + id.substr(0, id.find('.')) + "." + v[0];
                   return run_self({"rename", kind, id, v[0]}, sel);
               });
    }

    void remove(const std::string& kind, const std::string& id) {
        prompt("Retirer " + kind + " " + id,
               {{"Fichiers", "corbeille", {"corbeille", "suppression définitive"}},
                {"Références des dépendants", "refuser si utilisé", {"refuser si utilisé", "les retirer (--force)"}}},
               [this, kind, id](const std::vector<std::string>& v) {
                   std::vector<std::string> a = {"remove", kind, id, "--yes"};
                   if (v[0] != "corbeille") a.push_back("--purge");
                   if (v[1] != "refuser si utilisé") a.push_back("--force");
                   return run_self(a, kind == "layer" ? "group:layers" : kind == "app" ? "group:apps" : "layer:" + id.substr(0, id.find('.')));
               });
    }

    void remove_table(const std::string& dotted, const std::string& what) {
        prompt("Supprimer " + what, {{"Confirmer", "non", {"non", "oui"}}},
               [this, dotted](const std::vector<std::string>& v) {
                   if (v[0] != "oui") return false;
                   std::string group = dotted.starts_with("build.modes") ? "group:modes"
                                       : dotted.starts_with("toolchains") ? "group:toolchains"
                                       : dotted.starts_with("deploy") ? "group:deploys" : "group:generate";
                   return run_self({"config", "unset", dotted}, group);
               });
    }

    void add_mode() {
        prompt("Nouveau mode de compilation",
               {{"Nom", "", {}},
                {"Hérite de", "release", {"release", "debug", "relwithdebinfo", "minsize", "asan", "tsan", "ubsan", "valgrind"}},
                {"Options (flags)", "", {}}},
               [this](const std::vector<std::string>& v) {
                   std::vector<std::string> a = {"config", "set", "build.modes." + v[0] + ".base=" + v[1]};
                   if (!v[2].empty()) a.push_back("build.modes." + v[0] + ".flags=" + v[2]);
                   return run_self(a, "mode:" + v[0]);
               });
    }

    void add_toolchain() {
        prompt("Nouvelle toolchain",
               {{"Nom", "", {}}, {"Préfixe (croisée)", "", {}}, {"Compilateur C", "", {}}, {"Compilateur C++", "", {}},
                {"Famille", "auto", {"auto", "gcc", "clang"}}},
               [this](const std::vector<std::string>& v) {
                   std::vector<std::string> a = {"toolchain", "add", v[0]};
                   if (!v[1].empty()) a.insert(a.end(), {"--prefix", v[1]});
                   if (!v[2].empty()) a.insert(a.end(), {"--cc", v[2]});
                   if (!v[3].empty()) a.insert(a.end(), {"--cxx", v[3]});
                   if (v[4] != "auto") a.insert(a.end(), {"--family", v[4]});
                   return run_self(a, "toolchain:" + v[0]);
               });
    }

    void add_deploy() {
        prompt("Nouveau déploiement",
               {{"Nom", "", {}}, {"Méthode", "rsync", {"rsync", "scp", "copy", "script"}}, {"Hôte", "", {}},
                {"Utilisateur", "", {}}, {"Chemin", "/opt/{name}", {}}, {"Script (méthode script)", "", {}}},
               [this](const std::vector<std::string>& v) {
                   std::string base = "deploy." + v[0] + ".";
                   std::vector<std::string> a = {"config", "set", base + "method=" + v[1]};
                   if (!v[2].empty()) a.push_back(base + "host=" + v[2]);
                   if (!v[3].empty()) a.push_back(base + "user=" + v[3]);
                   if (!v[4].empty()) a.push_back(base + "path=" + v[4]);
                   if (!v[5].empty()) a.push_back(base + "script=" + v[5]);
                   return run_self(a, "deploy:" + v[0]);
               });
    }

    void add_generate() {
        prompt("Nouvelle étape de génération", {{"Nom", "", {}}, {"Commande", "", {}}, {"Entrées", "", {}}, {"Sorties", "", {}}},
               [this](const std::vector<std::string>& v) {
                   std::string base = "generate." + v[0] + ".";
                   std::vector<std::string> a = {"config", "set", base + "command=" + v[1]};
                   if (!v[2].empty()) a.push_back(base + "inputs=" + v[2]);
                   if (!v[3].empty()) a.push_back(base + "outputs=" + v[3]);
                   return run_self(a, "generate:" + v[0]);
               });
    }

    void show_no_project() {
        ws_.reset();
        nav_->set_items({{"new", "+ Créer un projet", 0, Tone::Accent, ""}});
        title_->set_text("Aucun projet dans " + cwd_.string());
        desc_->set_text("Aucun project.toml dans ce dossier ni ses parents.");
        set_actions({{"Créer un projet", [this] { new_project(); }}, {"Adopter ce dossier (init)", [this] {
                          auto r = proc::capture({self_.string(), "--no-color", "-C", cwd_.string(), "init"});
                          if (r.exit_code != 0) return set_status("✗ " + first_line(r.output), Tone::Error);
                          root_ = cwd_;
                          reload("project");
                      }}});
        clear_form("Créez un projet (layer core + module + application, ou disposition minimale) ou adoptez le dossier courant.");
    }

    void new_project() {
        prompt("Nouveau projet",
               {{"Nom", "", {}}, {"Langage", "c++", {"c++", "c"}}, {"Disposition", "full", {"full", "app", "lib", "empty"}},
                {"Standard C++", "c++23", {"c++17", "c++20", "c++23", "c++26"}}},
               [this](const std::vector<std::string>& v) {
                   auto r = proc::capture({self_.string(), "--no-color", "-C", cwd_.string(), "new", v[0], "--lang", v[1],
                                           "--layout", v[2], "--std", v[3]});
                   if (r.exit_code != 0) {
                       set_status("✗ " + first_line(r.output), Tone::Error);
                       return false;
                   }
                   root_ = cwd_ / v[0];
                   term_ready_ = false;
                   reload("project");
                   set_status("✓ projet " + v[0] + " créé", Tone::Success);
                   return true;
               });
    }

    // ========================================================================
    // Terminal
    // ========================================================================

    void ensure_terminal() {
        if (!term_->running()) {
            term_->start();
            term_ready_ = false;
        }
        if (term_ready_) return;
        term_ready_ = true;
        type_line("cpp-maker() { " + util::shell_quote(self_.string()) + " \"$@\"; }; cd " + util::shell_quote(root_.string()) +
                  " && clear && echo \"cpp-maker — $(pwd)\"");
    }

    void type_line(const std::string& line) {
        for (char32_t ch : tui::TextHelper::decode_utf8(line))
            (void)term_->on_key(tui::KeyEvent{tui::Key::Char, ch, false, false, false});
        (void)term_->on_key(tui::KeyEvent{tui::Key::Enter, 0, false, false, false});
    }

    std::string build_args(const std::string& cmd, bool with_target) {
        std::vector<std::string> a = {cmd};
        static const std::set<std::string> buildish = {"build", "run", "test", "check", "clean", "package", "size", "export compdb"};
        if (buildish.count(cmd)) {
            if (cmd != "check" && !opt_mode_->value().empty() && (!ws_ || opt_mode_->value() != ws_->project.default_mode))
                a.push_back(opt_mode_->value());
            if (with_target && !opt_target_->value().empty() && cmd != "export compdb") a.push_back(opt_target_->value());
            if (!opt_tc_->value().empty() && (!ws_ || opt_tc_->value() != ws_->project.default_toolchain))
                a.push_back("-t " + opt_tc_->value());
            if (cmd != "clean" && cmd != "size") {
                if (!opt_san_->value().empty() && cmd != "package") a.push_back("--sanitize " + opt_san_->value());
                if (!opt_verbose_->value().empty()) a.push_back("-v");
                if (!opt_keep_->value().empty() && cmd != "run") a.push_back("-k");
            }
        }
        return util::join(a, " ");
    }

    /// Lance une commande cpp-maker dans le terminal intégré. `with_options` :
    /// ajoute les options de l'onglet Exécution ; si la commande nomme déjà sa
    /// cible (« build core.log »), le sélecteur « Cible » est ignoré.
    void run_in_terminal(const std::string& cmd, bool with_options) {
        switch_tab(kTabExec);
        std::string line = cmd;
        if (with_options) {
            auto sp = cmd.find(' ');
            line = sp == std::string::npos ? build_args(cmd, true) : build_args(cmd.substr(0, sp), false) + cmd.substr(sp);
        }
        type_line("cpp-maker " + line);
        set_status("▶ cpp-maker " + line, Tone::Info);
    }

    // ========================================================================
    // Git
    // ========================================================================

    static std::string git_state_label(const vcs::StatusEntry& e) {
        if (e.untracked()) return "non suivi";
        auto one = [](char c) -> std::string {
            switch (c) {
            case 'M': return "modifié";
            case 'A': return "ajouté";
            case 'D': return "supprimé";
            case 'R': return "renommé";
            case 'U': return "conflit";
            default: return {};
            }
        };
        std::string a = one(e.index), b = one(e.worktree);
        if (!a.empty() && !b.empty()) return a + " (indexé) + " + b;
        return a.empty() ? b : a + " (indexé)";
    }

    void refresh_git() {
        git_paths_.clear();
        std::vector<NavItem> items;
        git_repo_ = ws_ && util::have("git") && vcs::toplevel(ws_->root).has_value();
        if (!git_repo_) {
            git_branch_->set_text("Pas de dépôt git");
            items.push_back({"git:none", "Aucun dépôt : « Initialiser (git init) »", 0, Tone::Muted, ""});
            git_nav_->set_items(items);
            git_info_->set_text("Initialiser crée le dépôt, un .gitignore et installe le crochet pre-commit de cpp-maker.");
            return;
        }
        try {
            auto st = vcs::status(ws_->root);
            git_branch_->set_text(std::format("Branche {}{}{}", st.branch.empty() ? "(détachée)" : st.branch,
                                              st.upstream.empty() ? "" : " → " + st.upstream,
                                              st.ahead || st.behind ? std::format("  (+{} / -{})", st.ahead, st.behind) : ""));
            std::map<std::string, std::vector<const vcs::StatusEntry*>> groups;
            std::vector<std::string> order;
            for (auto& e : st.entries) {
                auto owner = vcs::owner_of(*ws_, e.path);
                if (!groups.count(owner)) order.push_back(owner);
                groups[owner].push_back(&e);
            }
            int staged_total = 0;
            for (auto& owner : order) {
                auto& g = groups[owner];
                int staged = 0;
                for (auto* e : g) staged += e->staged();
                staged_total += staged;
                std::string title = owner == "project" ? "projet" : owner.starts_with("layer:") ? "layer " + owner.substr(6) : owner;
                std::string gid = "grp:" + owner;
                items.push_back({gid, "▸ " + title, 0, Tone::Accent, std::format("{}/{} indexé(s)", staged, g.size())});
                for (auto* e : g) {
                    std::string fid = "f:" + e->path.string();
                    Tone c = e->staged() ? Tone::Success : e->untracked() ? Tone::Warning : Tone::Error;
                    items.push_back({fid, (e->staged() ? "● " : e->untracked() ? "? " : "○ ") + util::display(e->path, ws_->root), 1, c,
                                     git_state_label(*e)});
                    git_paths_[fid] = {e->path};
                    git_paths_[gid].push_back(e->path);
                }
            }
            if (items.empty()) items.push_back({"git:clean", "✓ rien à valider, copie de travail propre", 0, Tone::Success, ""});
            git_nav_->set_items(items);
            auto state = vcs::hook_state(ws_->root);
            auto rules = vcs::all_rules(*ws_);
            git_info_->set_text(std::format("{} fichier(s), {} indexé(s).\nCrochet pre-commit : {} · {} règle(s).\n"
                                            "● indexé  ○ modifié  ? non suivi",
                                            st.entries.size(), staged_total,
                                            state == vcs::HookState::Ours ? "installé" : state == vcs::HookState::Foreign ? "tiers" : "absent",
                                            rules.size()));
        } catch (const std::exception& e) {
            git_nav_->set_items({{"git:error", "✗ " + first_line(e.what()), 0, Tone::Error, ""}});
        }
    }

    std::vector<fs::path> git_selection() const {
        auto it = git_paths_.find(git_nav_->selected_id());
        return it == git_paths_.end() ? std::vector<fs::path>{} : it->second;
    }

    /// « <cible> » de l'élément sélectionné (pour log/diff), ou vide.
    std::string git_selected_target() const {
        std::string id = git_nav_ ? git_nav_->selected_id() : "";
        if (id.starts_with("grp:") && id != "grp:project") {
            std::string owner = id.substr(4);
            return " " + (owner.starts_with("layer:") ? owner.substr(6) : owner);
        }
        return {};
    }

    void git_stage_selected(bool stage) {
        auto paths = git_selection();
        if (paths.empty() || !ws_) return set_status("Sélectionnez un fichier ou une cible.", Tone::Muted);
        std::string err = stage ? vcs::stage(ws_->root, paths) : vcs::unstage(ws_->root, paths);
        if (!err.empty()) set_status("✗ " + first_line(err), Tone::Error);
        else set_status(std::format("✓ {} fichier(s) {}", paths.size(), stage ? "indexé(s)" : "désindexé(s)"), Tone::Success);
        std::string keep = git_nav_->selected_id();
        refresh_git();
        git_nav_->select_id(keep);
    }

    void git_toggle_selected() {
        auto id = git_nav_->selected_id();
        bool staged = false;
        for (auto& it : git_nav_->items())
            if (it.id == id) staged = it.label.starts_with("● ") || (id.starts_with("grp:") && it.hint.find("0/") != 0);
        git_stage_selected(!staged);
    }

    void git_stage_all() {
        if (!ws_ || !git_repo_) return;
        std::string err = vcs::stage(ws_->root, {ws_->root});
        set_status(err.empty() ? "✓ tout le projet est indexé" : "✗ " + first_line(err), err.empty() ? Tone::Success : Tone::Error);
        refresh_git();
    }

    void git_commit_dialog() {
        if (!git_repo_) return set_status("Pas de dépôt git.", Tone::Warning);
        prompt("Valider (git commit)",
               {{"Message", "", {}}, {"Indexer tout avant", "non", {"non", "oui"}},
                {"Crochet pre-commit", "exécuter", {"exécuter", "ignorer (--no-verify)"}}},
               [this](const std::vector<std::string>& v) {
                   if (v[0].empty()) {
                       set_status("Message de commit vide : rien n'est fait.", Tone::Warning);
                       return false;
                   }
                   std::string cmd = "git commit -m " + util::shell_quote(v[0]);
                   if (v[1] == "oui") cmd += " -a";
                   if (v[2] != "exécuter") cmd += " --no-verify";
                   run_in_terminal(cmd, false);
                   return true;
               });
    }

    // --- Règles pre-commit (configuration)
    void add_rule() {
        if (!ws_) return;
        std::vector<std::string> scopes = {"projet"};
        for (auto& L : ws_->layers) scopes.push_back("layer " + L.name);
        for (auto& t : ws_->targets)
            if (!t.from_git) scopes.push_back((t.is_app() ? "app " : "module ") + t.id);
        prompt("Nouvelle règle pre-commit",
               {{"Nom", "", {}},
                {"Portée", "projet", scopes},
                {"Type", "cpp-maker", {"cpp-maker", "commande shell"}},
                {"Commande", "test {targets}", {}},
                {"Fichiers concernés", "", {}},
                {"En cas d'échec", "bloquer le commit", {"bloquer le commit", "avertir seulement"}},
                {"Description", "", {}}},
               [this](const std::vector<std::string>& v) {
                   std::vector<std::string> a = {"git", "rule", "add", v[0], v[2] == "cpp-maker" ? "--run" : "--command", v[3]};
                   if (v[1] != "projet") {
                       auto sp = v[1].find(' ');
                       std::string kind = v[1].substr(0, sp), val = v[1].substr(sp + 1);
                       a.insert(a.end(), {kind == "layer" ? "--layer" : kind == "app" ? "--app" : "--module", val});
                   }
                   if (!v[4].empty()) a.insert(a.end(), {"--when", v[4]});
                   if (v[5] != "bloquer le commit") a.push_back("--warn");
                   if (!v[6].empty()) a.insert(a.end(), {"--description", v[6]});
                   return run_self(a, "group:rules");
               });
    }

    // ========================================================================
    // Clavier
    // ========================================================================

    void on_tab(size_t index) {
        if (index == kTabExec) ensure_terminal();
        if (index == kTabGit) refresh_git();
    }

    void switch_tab(size_t index) {
        tabs_->set_active_index(index);
        last_tab_ = index;
        on_tab(index);
        if (index == kTabConfig) app_.focus_manager().set_focus(nav_);
        else if (index == kTabToml && editor_->tab_count()) app_.focus_manager().set_focus(editor_->active_editor());
        else if (index == kTabExec) app_.focus_manager().set_focus(term_);
        else if (index == kTabGit) app_.focus_manager().set_focus(git_nav_);
    }

    void show_help() {
        auto body = std::make_shared<tui::Vertical>();
        const std::pair<const char*, const char*> rows[] = {
            {"F2 / F3 / F4 / F8", "onglets Configuration · Fichier TOML · Exécution · Git (ou clic)"},
            {"Souris", "clic : sélectionner / focus · double-clic : ouvrir · molette : défiler"},
            {"F9 / Ctrl+T", "choisir le thème (mémorisé) / thème suivant"},
            {"↑ ↓ (arbre)", "choisir un élément ; Entrée : aller au formulaire"},
            {"Tab / Maj+Tab", "champ suivant / précédent"},
            {"← → (choix)", "changer une valeur à choix (‹ valeur ›)"},
            {"Ctrl+S", "enregistrer : tout le projet est revalidé, sinon rien n'est écrit"},
            {"Ctrl+Z", "abandonner les modifications du formulaire / recharger le fichier"},
            {"Ctrl+R", "recharger le projet depuis le disque"},
            {"F5 / F6 / F7", "build / run / test avec les options de l'onglet F4"},
            {"Ctrl+Q", "quitter"},
        };
        for (auto& [k, v] : rows) {
            auto r = std::make_shared<tui::Horizontal>();
            r->add_child(label(k, Tone::Accent), LayoutParams::fixed(20));
            r->add_child(label(v), LayoutParams::stretch(1));
            body->add_child(r, LayoutParams::fixed(1));
        }
        body->add_child(label(""), LayoutParams::fixed(1));
        body->add_child(label("Champ vide = clé absente (valeur par défaut). Listes : valeurs séparées par des virgules.", Tone::Muted),
                        LayoutParams::fixed(1));
        body->add_child(label("Les actions (ajouter, renommer…) appliquent les mêmes règles que la ligne de commande.", Tone::Muted),
                        LayoutParams::fixed(1));
        auto ok = button("Fermer", [this] { close_dialog(); }, "-primary");
        body->add_child(ok, LayoutParams::fixed(1));
        dialog_ok_ = nullptr;
        dialog_kinds_.clear();
        dialog_->set_title(" Aide ");
        dialog_->set_content(body);
        dialog_cancel_ = nullptr;
        dialog_->set_size(tui::Size{96, 18});
        dialog_->show();
        app_.focus_manager().set_focus(ok);
    }

    bool on_key(const tui::KeyEvent& e) {
        bool in_term = app_.focus_manager().current() == term_;
        bool ctrl = e.key == tui::Key::Char && e.ctrl;
        char32_t c = ctrl ? (e.codepoint | 0x20) : 0;
        if (dialog_->showing()) {
            if (e.key == tui::Key::Escape) {
                close_dialog();
                return true;
            }
            return false;
        }
        if (ctrl && c == U'q') {
            request_quit();
            return true;
        }
        quit_armed_ = false;
        if (ctrl && c == U't' && !in_term) {
            std::string t = app_.next_theme();
            save_theme(t);
            set_status("Thème " + t + " (F9 pour choisir)", Tone::Info);
            return true;
        }
        switch (e.key) {
        case tui::Key::F1: show_help(); return true;
        case tui::Key::F2: switch_tab(kTabConfig); return true;
        case tui::Key::F3: switch_tab(kTabToml); return true;
        case tui::Key::F4: switch_tab(kTabExec); return true;
        case tui::Key::F8: switch_tab(kTabGit); return true;
        case tui::Key::F9: choose_theme(); return true;
        case tui::Key::F5: run_in_terminal("build", true); return true;
        case tui::Key::F6: run_in_terminal("run", true); return true;
        case tui::Key::F7: run_in_terminal("test", true); return true;
        default: break;
        }
        if (in_term) return false;   // le shell reçoit Ctrl+R, Ctrl+Z…
        if (ctrl && c == U's') {
            save();
            return true;
        }
        if (ctrl && c == U'z' && tabs_->active_index() != kTabToml) {
            revert();
            return true;
        }
        if (ctrl && c == U'r') {
            editor_open_ = false;
            form_.reset();
            reload(current_id_);
            set_status("↻ rechargé", Tone::Info);
            return true;
        }
        return false;
    }

    void set_status(const std::string& msg, Tone color) {
        status_->set_text(util::replace_all(msg, root_.string() + "/", ""));
        set_tone(*status_, color);
    }
};

} // namespace

int run_tui(const std::filesystem::path& cwd, const TuiOptions& opts) {
    if (!::isatty(0) || !::isatty(1)) {
        term::error("cpp-maker tui nécessite un terminal interactif");
        return 1;
    }
    tui::App app(std::make_unique<tui::PosixTerminalBackend>());
    auto keep = build_tui(app, cwd, opts);
    return app.run();
}

std::shared_ptr<void> build_tui(tui::App& app, const std::filesystem::path& cwd, const TuiOptions& opts) {
    auto ui = std::make_shared<CppMakerTui>(app, cwd, opts);
    ui->build();
    return ui;
}

} // namespace ui
