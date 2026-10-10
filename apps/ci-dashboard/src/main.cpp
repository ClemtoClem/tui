/**
 * @file main.cpp
 * @brief Exemple "CI dashboard" : deux onglets (pipelines / logs) dans le
 * style planor (github.com/mrusme/planor) - onglets encadrés, panneaux
 * arrondis violets, liste à deux lignes avec barre d'accent magenta.
 *
 * Démontre les widgets étendus pour ce style : Border (arrondi + couleur),
 * Tabs (barre encadrée), ListView (style Detailed), ShortcutBar (vertical).
 */

#include <tui/App.hpp>
#include <tui/terminal/PosixTerminalBackend.hpp>
#include <tui/widget/layout/Border.hpp>
#include <tui/widget/layout/Horizontal.hpp>
#include <tui/widget/layout/Tabs.hpp>
#include <tui/widget/layout/Vertical.hpp>
#include <tui/widgets/Badge.hpp>
#include <tui/widgets/Label.hpp>
#include <tui/widgets/ListView.hpp>
#include <tui/widgets/ShortcutBar.hpp>

#include <memory>
#include <string>
#include <vector>

using namespace tui;

namespace {

// --- Palette planor : violet/magenta sur fond quasi noir --------------
const Color kFg{225, 225, 232};
const Color kBorder{124, 111, 240};
const Color kAccent{217, 87, 190};
const Color kMuted{130, 130, 140};
const Color kBadgeBg{92, 84, 224};
const Color kSuccess{110, 200, 140};
const Color kError{224, 90, 90};

// ListView focusé au clavier + touche de sortie 'q' (même schéma que
// DataTable/SpreadsheetGrid dans les autres exemples : le widget qui a le
// focus est celui qui doit intercepter la sortie, App ne route pas les
// événements clavier ailleurs qu'au widget focusé).
class QuitListView : public ListView {
public:
    void set_on_quit(std::function<void()> fn) { on_quit_ = std::move(fn); }

    bool on_key(const KeyEvent& e) override {
        if (e.key == Key::Char && (e.codepoint == U'q' || e.codepoint == U'Q')) {
            if (on_quit_) on_quit_();
            return true;
        }
        return ListView::on_key(e);
    }

private:
    std::function<void()> on_quit_;
};

std::shared_ptr<Border> make_panel(std::string title = "") {
    auto panel = std::make_shared<Border>();
    panel->set_border_style(BorderStyle::Rounded);
    panel->set_color(kBorder);
    if (!title.empty()) panel->set_title(std::move(title));
    return panel;
}

std::shared_ptr<ShortcutBar> make_nav_hints() {
    auto hints = std::make_shared<ShortcutBar>();
    hints->set_orientation(ShortcutBarOrientation::Vertical);
    hints->set_color(kMuted);
    hints->set_hints({
        {"↑/k", "up"},
        {"↓/j", "down"},
        {"→/l/pgdn", "next page"},
        {"←/h/pgup", "prev page"},
        {"g/home", "go to start"},
        {"G/end", "go to end"},
    });
    return hints;
}

struct Sidebar {
    std::shared_ptr<Border> panel;
    std::shared_ptr<QuitListView> list;
};

// Reconstruit le panneau gauche des captures planor : badge de section,
// compteur d'items discret, liste à deux lignes, aide-mémoire clavier.
Sidebar build_sidebar(const std::string& section, const std::vector<ListItem>& items) {
    auto badge = std::make_shared<Badge>(section);
    badge->set_colors(Color::White(), kBadgeBg);

    auto badge_row = std::make_shared<Horizontal>();
    badge_row->add_child(badge, LayoutParams{LayoutMode::Auto, 0, CrossAlign::Start});

    auto count = std::make_shared<Label>(std::to_string(items.size()) + " items");
    count->set_foreground(kMuted);

    auto list = std::make_shared<QuitListView>();
    list->set_style(ListViewStyle::Detailed);
    list->set_colors(kAccent, kFg, kMuted);
    list->set_items(items);

    auto content = std::make_shared<Vertical>();
    content->add_child(badge_row, LayoutParams::fixed(1));
    content->add_child(count, LayoutParams::fixed(1));
    content->add_child(std::make_shared<Label>(""), LayoutParams::fixed(1));
    content->add_child(list, LayoutParams::stretch(1));
    content->add_child(make_nav_hints(), LayoutParams::fixed(6));

    auto panel = make_panel();
    panel->set_child(content);
    return {panel, list};
}

// Rend un bloc de texte préformaté (une Label par ligne) avec une
// coloration grossière par mot-clé - suffisant pour un panneau de détail
// en lecture seule, pas besoin d'un vrai moteur de mise en forme.
std::shared_ptr<Widget> build_detail_view(const std::vector<std::string>& lines) {
    auto content = std::make_shared<Vertical>();
    for (size_t i = 0; i < lines.size(); ++i) {
        auto label = std::make_shared<Label>(lines[i]);
        Color fg = kFg;
        if (lines[i].find("Succeeded") != std::string::npos) fg = kSuccess;
        else if (lines[i].find("Failed") != std::string::npos) fg = kError;
        else if (lines[i].find("Updated") != std::string::npos || lines[i].find("Status:") != std::string::npos) fg = kMuted;
        label->set_foreground(fg);
        if (i == 0) label->set_style(TextStyle::Bold);
        content->add_child(label, LayoutParams::fixed(1));
    }
    return content;
}

struct Pipeline {
    std::string name;
    std::string status;
    std::vector<std::string> detail;
};

std::vector<Pipeline> sample_pipelines() {
    return {
        {"ci-fn-be-rules", "Succeeded",
         {"ci-fn-be-rules", "", "Updated 2026-08-05 09:41:12 UTC", "",
          "  Source", "  Status: Succeeded", "", "    Function",
          "    Updated: 2026-08-05 09:41:05 UTC", "    Status: Succeeded",
          "    {\"Provider\":\"GitHub\",\"CommitMessage\":\"Fix account lookup\"}", "",
          "    Infrastructure", "    Updated: 2026-08-05 09:41:09 UTC", "    Status: Succeeded", "",
          "  Build", "  Status: Succeeded", "", "    Build",
          "    Updated: 2026-08-05 09:45:47 UTC", "    Status: Succeeded", "",
          "  Store", "  Status: Succeeded"}},
        {"ci-fn-email-service", "Succeeded",
         {"ci-fn-email-service", "", "Updated 2026-08-05 08:12:03 UTC", "",
          "  Source", "  Status: Succeeded", "", "  Build", "  Status: Succeeded", "",
          "  Store", "  Status: Succeeded"}},
        {"ci-fn-financerunits", "Succeeded",
         {"ci-fn-financerunits", "", "Updated 2026-08-04 22:03:51 UTC", "",
          "  Source", "  Status: Succeeded", "", "  Build", "  Status: Succeeded", "",
          "  Store", "  Status: Succeeded"}},
        {"ci-fn-healthcheck", "Failed",
         {"ci-fn-healthcheck", "", "Updated 2026-08-05 07:58:44 UTC", "",
          "  Source", "  Status: Succeeded", "", "  Build", "  Status: Failed",
          "    {\"Provider\":\"GitHub\",\"Error\":\"unit tests failed (3)\"}", "",
          "  Store", "  Status: Skipped"}},
        {"ci-fn-individuals", "Succeeded",
         {"ci-fn-individuals", "", "Updated 2026-08-04 19:20:16 UTC", "",
          "  Source", "  Status: Succeeded", "", "  Build", "  Status: Succeeded", "",
          "  Store", "  Status: Succeeded"}},
        {"ci-fn-mod-compliancecheck", "Succeeded",
         {"ci-fn-mod-compliancecheck", "", "Updated 2026-08-04 15:07:29 UTC", "",
          "  Source", "  Status: Succeeded", "", "  Build", "  Status: Succeeded", "",
          "  Store", "  Status: Succeeded"}},
    };
}

struct LogGroup {
    std::string name;
    std::string size;
};

std::vector<LogGroup> sample_log_groups() {
    return {
        {"/aws/codebuild/ci-fn-build", "0 KB"},
        {"/aws/lambda/api-graphql", "0 KB"},
        {"/aws/lambda/api-auth", "0 KB"},
        {"/aws/lambda/api-billing", "0 KB"},
        {"/aws/lambda/api-notify", "0 KB"},
        {"/aws/lambda/api-search", "205 KB"},
        {"/aws/lambda/api-export", "2 KB"},
        {"/aws/lambda/api-import", "1 KB"},
    };
}

std::shared_ptr<Widget> build_ci_page(App& app) {
    auto pipelines = sample_pipelines();
    std::vector<ListItem> items;
    items.reserve(pipelines.size());
    for (const auto& p : pipelines) items.push_back(ListItem{p.name, p.status});

    Sidebar sidebar = build_sidebar("Pipelines", items);

    auto detail_panel = make_panel();
    detail_panel->set_child(build_detail_view(pipelines.front().detail));

    // set_items() a reconstruit la liste : recapturer detail_panel par
    // valeur dans la lambda est sûr (shared_ptr), pipelines aussi.
    sidebar.list->set_on_select([detail_panel, pipelines](size_t idx) {
        if (idx < pipelines.size()) detail_panel->set_child(build_detail_view(pipelines[idx].detail));
    });
    sidebar.list->set_on_quit([&app] { app.quit(0); });

    auto row = std::make_shared<Horizontal>();
    row->add_child(sidebar.panel, LayoutParams::fixed(34));
    row->add_child(detail_panel, LayoutParams::stretch(1));

    app.focus_manager().set_focus(sidebar.list);
    return row;
}

std::shared_ptr<Widget> build_logging_page(App& app) {
    auto groups = sample_log_groups();
    std::vector<ListItem> items;
    items.reserve(groups.size());
    for (const auto& g : groups) items.push_back(ListItem{g.name, g.size});

    Sidebar sidebar = build_sidebar("Groups", items);
    sidebar.list->set_on_quit([&app] { app.quit(0); });

    auto detail_panel = make_panel(); // vide tant qu'aucun groupe n'est ouvert, comme dans planor

    auto row = std::make_shared<Horizontal>();
    row->add_child(sidebar.panel, LayoutParams::fixed(34));
    row->add_child(detail_panel, LayoutParams::stretch(1));
    return row;
}

} // namespace

int main() {
    App app(std::make_unique<PosixTerminalBackend>());

    auto tabs = std::make_shared<Tabs>();
    tabs->set_bar_style(TabsBarStyle::Boxed);
    tabs->set_colors(kBorder, Color::White(), kMuted);
    tabs->add_tab("Continuous Integration", build_ci_page(app));
    tabs->add_tab("Logging", build_logging_page(app));

    // --- Raccourci global : Ctrl+Q quitte depuis n'importe quel widget ---
    app.set_global_key_handler([&app](const KeyEvent& e) {
        if (e.key == Key::Char && e.ctrl &&
            (e.codepoint == U'q' || e.codepoint == U'Q')) {
            app.quit(0);
            return true;
        }
        return false;
    });

    app.set_root(tabs);
    return app.run();
}
