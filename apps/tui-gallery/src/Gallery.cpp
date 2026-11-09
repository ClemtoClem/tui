#include "Gallery.hpp"

#include <cmath>
#include <ctime>

using namespace tui;


namespace {

constexpr const char* kAppCss = R"(
#sidebar { border-title-color: $accent; }
#themes { width: 24; }
.row { padding: 0 1; }
.title { color: $text-accent; text-style: bold; }
#clock { color: $primary; text-align: center; }
#status { color: $text-muted; padding: 0 1; }
Placeholder { height: 5; }
)";

std::shared_ptr<Label> label(const std::string& text, const std::string& classes = {}) {
    auto l = std::make_shared<Label>(text);
    if (!classes.empty()) l->add_class(classes);
    return l;
}

std::shared_ptr<Rule> rule() { return std::make_shared<Rule>(); }

} // namespace

Gallery::Gallery(App& app) : app_(app) {
    app_.use_default_stylesheet();
    app_.set_css(kAppCss);

    status = label("Cliquez sur un widget ou utilisez Tab.");
    status->set_id("status");

    // --- En-tête ----------------------------------------------------------
    header = std::make_shared<Header>("Galerie libtui", "thème " + app_.theme_name());
    header->set_show_clock(true);
    header->set_on_icon_click([this] { status->set_text("Icône de l'en-tête cliquée."); });

    // --- Barre latérale : thèmes -------------------------------------------
    themes_list = std::make_shared<ListView>();
    themes_list->set_id("themes");
    std::vector<ListItem> items;
    auto names = themes::names();
    for (auto& n : names) items.push_back({n, "", true, ""});
    themes_list->set_items(items);
    themes_list->set_on_select([this, names](size_t i) {
        if (i < names.size()) app_.set_theme(names[i]);
    });
    sidebar = std::make_shared<Border>();
    sidebar->set_id("sidebar");
    sidebar->set_title("Thèmes");
    sidebar->set_border_style(BorderStyle::Rounded);
    sidebar->set_child(themes_list);

    // --- Onglet « Contrôles » ------------------------------------------------
    controls = std::make_shared<Vertical>();
    controls->add_child(label("Boutons", "title"), LayoutParams::fixed(1));
    buttons = std::make_shared<Horizontal>();
    buttons->add_class("row");
    for (auto [text, cls] : std::initializer_list<std::pair<const char*, const char*>>{
             {"Défaut", ""}, {"Principal", "-primary"}, {"Succès", "-success"},
             {"Attention", "-warning"}, {"Erreur", "-error"}}) {
        auto b = std::make_shared<Button>(text);
        if (*cls) b->add_class(cls);
        std::string t = text;
        b->set_on_click([this, t] { status->set_text("Bouton « " + t + " » cliqué."); });
        buttons->add_child(b, LayoutParams::auto_size());
        buttons->add_child(label(" "), LayoutParams::fixed(1));
    }
    auto disabled = std::make_shared<Button>("Désactivé");
    disabled->set_disabled(true);
    buttons->add_child(disabled, LayoutParams::auto_size());
    controls->add_child(buttons, LayoutParams::fixed(1));
    controls->add_child(rule(), LayoutParams::fixed(1));

    controls->add_child(label("Saisie", "title"), LayoutParams::fixed(1));
    input = std::make_shared<Input>();
    input->set_placeholder("Tapez quelque chose puis Entrée…");
    input->set_on_submit([this](const std::string& s) { status->set_text("Saisi : " + s); });
    controls->add_child(input, LayoutParams::fixed(1));
    controls->add_child(rule(), LayoutParams::fixed(1));

    controls->add_child(label("Options", "title"), LayoutParams::fixed(1));
    check = std::make_shared<Checkbox>("Activer les animations");
    check->set_checked(true);
    controls->add_child(check, LayoutParams::fixed(1));
    progress = std::make_shared<ProgressBar>();
    progress->set_show_label(true);
    controls->add_child(progress, LayoutParams::fixed(1));
    link = std::make_shared<Link>("Documentation de Textual", "https://textual.textualize.io");
    link->set_on_click([this](const std::string& url) { status->set_text("Lien : " + url); });
    controls->add_child(link, LayoutParams::fixed(1));
    controls->add_child(std::make_shared<Label>(), LayoutParams::stretch(1));

    // --- Onglet « Données » --------------------------------------------------
    data = std::make_shared<Vertical>();
    clock = std::make_shared<Digits>("00:00:00");
    clock->set_id("clock");
    data->add_child(clock, LayoutParams::fixed(3));
    data->add_child(label("Activité", "title"), LayoutParams::fixed(1));
    spark = std::make_shared<Sparkline>();
    data->add_child(spark, LayoutParams::fixed(3));
    loading = std::make_shared<LoadingIndicator>();
    data->add_child(loading, LayoutParams::fixed(1));
    data->add_child(label("Journal", "title"), LayoutParams::fixed(1));
    log = std::make_shared<Log>();
    log->set_max_lines(500);
    data->add_child(log, LayoutParams::stretch(1));

    // --- Onglet « Mise en page » ---------------------------------------------
    layout = std::make_shared<Vertical>();
    auto c1 = std::make_shared<Collapsible>("Zones de maquette (cliquez dessus)", false);
    auto ph = std::make_shared<Horizontal>();
    ph->add_child(std::make_shared<Placeholder>("Gauche"), LayoutParams::stretch(1));
    ph->add_child(std::make_shared<Placeholder>("Centre"), LayoutParams::stretch(2));
    ph->add_child(std::make_shared<Placeholder>("Droite"), LayoutParams::stretch(1));
    c1->set_content(ph);
    layout->add_child(c1, LayoutParams::auto_size());
    auto c2 = std::make_shared<Collapsible>("Séparateurs");
    auto rules = std::make_shared<Vertical>();
    for (auto l : {Rule::Line::Solid, Rule::Line::Heavy, Rule::Line::Double, Rule::Line::Dashed})
        rules->add_child(std::make_shared<Rule>(Rule::Orientation::Horizontal, l), LayoutParams::fixed(1));
    c2->set_content(rules);
    layout->add_child(c2, LayoutParams::auto_size());
    auto c3 = std::make_shared<Collapsible>("À propos des styles");
    c3->set_content(label("Les couleurs viennent du thème ($primary, $surface…) et de la feuille "
                          "par défaut ; passez un fichier .tcss en argument pour l'éditer à chaud."));
    layout->add_child(c3, LayoutParams::auto_size());
    layout->add_child(std::make_shared<Label>(), LayoutParams::stretch(1));

    tabs = std::make_shared<Tabs>();
    tabs->add_tab("Contrôles", controls);
    tabs->add_tab("Données", data);
    tabs->add_tab("Mise en page", layout);

    body = std::make_shared<Horizontal>();
    body->add_child(sidebar, LayoutParams::fixed(26));
    body->add_child(tabs, LayoutParams::stretch(1));

    // --- Pied --------------------------------------------------------------
        footer = std::make_shared<Footer>();
    footer->add_binding({"^q", "Quitter", [this] { app_.quit(0); }});
    footer->add_binding({"^t", "Thème suivant", [this] { app_.next_theme(); }});
    footer->add_binding({"^s", "Style par défaut", [this] {
        default_css_ = !default_css_;
        app_.use_default_stylesheet(default_css_);
    }});
    footer->add_binding({"Tab", "Focus suivant", [this] { app_.focus_manager().focus_next(app_.focus_manager().current()); }});

    root = std::make_shared<Vertical>();
    root->add_child(header, LayoutParams::fixed(1));
    root->add_child(body, LayoutParams::stretch(1));
    root->add_child(status, LayoutParams::fixed(1));
    root->add_child(footer, LayoutParams::fixed(1));
    app_.set_root(root);

    app_.set_on_theme_change([this](const std::string& name) {
        header->set_subtitle("thème " + name);
        log->write_line("Thème : " + name);
    });
    app_.set_global_key_handler([this](const KeyEvent& e) {
        if (!e.ctrl || e.key != Key::Char) return false;
        for (auto& b : footer->bindings())
            if (b.key == std::string("^") + static_cast<char>(e.codepoint)) {
                b.action();
                return true;
            }
        return false;
    });

    app_.focus_manager().focus_next(root);
}

void Gallery::tick() {
        if (!check->checked()) return;
        ++tick_;
        loading->advance();
        spark->push(5 + 4 * std::sin(tick_ / 6.0) + (tick_ * 7 % 5) / 2.0, 200);
        progress->set_progress((tick_ % 100) / 100.0);
        if (tick_ % 10 == 0) {
            std::time_t t = std::time(nullptr);
            char buf[16];
            std::strftime(buf, sizeof buf, "%H:%M:%S", std::localtime(&t));
            clock->set_value(buf);
            header->need_repaint();
        }
        if (tick_ % 25 == 0) log->write_line("[" + std::to_string(tick_ / 10) + " s] tic");
    }
