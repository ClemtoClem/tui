/* @file tests/test_tui_style.cpp — couleurs, TCSS, cascade, thèmes, rendu stylé. */

#include <gtest/gtest.h>
#include <tui/App.hpp>
#include <tui/Tui.hpp>
#include <tui/terminal/HeadlessTerminalBackend.hpp>

#include <filesystem>
#include <fstream>

using namespace tui;

namespace {

struct Leaf : Widget {
    Size natural{4, 1};
    [[nodiscard]] Size measure(const Constraints& c) const override { return c.clamp(natural); }
    void paint(Buffer& b) const override {
        for (int x = bounds_.x; x < bounds_.right(); ++x) b.set(x, bounds_.y, Cell{U'x', 1, Color::Default(), Color::Default(), TextStyle::None});
    }
};

std::unique_ptr<HeadlessTerminalBackend> backend(int w, int h) {
    auto b = std::make_unique<HeadlessTerminalBackend>();
    b->set_size(w, h);
    return b;
}

} // namespace

// --- Couleurs ------------------------------------------------------------

TEST(ColorUtil, AnalyseEtFormats) {
    EXPECT_EQ(*color::parse("#fff"), (Color{255, 255, 255}));
    EXPECT_EQ(*color::parse("#0178D4"), (Color{1, 120, 212}));
    EXPECT_EQ(*color::parse("rgb(10, 20, 30)"), (Color{10, 20, 30}));
    EXPECT_EQ(*color::parse("Red"), (Color{220, 50, 47}));
    EXPECT_TRUE(color::parse("transparent")->is_default);
    EXPECT_FALSE(color::parse("#12").has_value());
    EXPECT_FALSE(color::parse("pas-une-couleur").has_value());
    EXPECT_EQ(color::to_hex({1, 120, 212}), "#0178D4");
}

TEST(ColorUtil, MelangeEtLuminosite) {
    EXPECT_EQ(color::blend({0, 0, 0}, {200, 100, 50}, 0.5), (Color{100, 50, 25}));
    Color c{100, 100, 100};
    EXPECT_GT(color::luminance(color::lighten(c, 0.2)), color::luminance(c));
    EXPECT_LT(color::luminance(color::lighten(c, -0.2)), color::luminance(c));
    EXPECT_GT(color::luminance(color::contrast_text({0, 0, 0})), 0.5);
    EXPECT_LT(color::luminance(color::contrast_text({255, 255, 255})), 0.5);
}

// --- Sélecteurs -----------------------------------------------------------

TEST(Selector, AnalyseEtSpecificite) {
    auto s = css::Stylesheet::parse_selector("Vertical > Button#ok.-primary:hover");
    ASSERT_TRUE(s.has_value());
    EXPECT_EQ(s->steps.size(), 2u);
    EXPECT_EQ(s->steps[1].combinator, '>');
    EXPECT_EQ(s->ids, 1);
    EXPECT_EQ(s->classes, 2);
    EXPECT_EQ(s->types, 2);
    EXPECT_EQ(s->to_string(), "Vertical > Button#ok.-primary:hover");
    std::string err;
    EXPECT_FALSE(css::Stylesheet::parse_selector("Button:bidule", &err).has_value());
    EXPECT_NE(err.find("bidule"), std::string::npos);
    EXPECT_FALSE(css::Stylesheet::parse_selector("> Button").has_value());
}

TEST(Selector, CorrespondanceTypesIdsClassesCombinateurs) {
    auto root = std::make_shared<Vertical>();
    auto inner = std::make_shared<Horizontal>();
    auto btn = std::make_shared<Button>("OK");
    btn->set_id("ok");
    btn->add_class("-primary big");
    inner->add_child(btn);
    root->add_child(inner);
    css::MatchContext ctx;
    auto m = [&](const char* sel, const Widget& w) { return css::Stylesheet::matches(*css::Stylesheet::parse_selector(sel), w, ctx); };
    EXPECT_TRUE(m("Button", *btn));
    EXPECT_TRUE(m("Widget", *btn));
    EXPECT_TRUE(m("#ok", *btn));
    EXPECT_TRUE(m(".big.-primary", *btn));
    EXPECT_TRUE(m("Vertical Button", *btn));
    EXPECT_TRUE(m("Horizontal > Button", *btn));
    EXPECT_FALSE(m("Vertical > Button", *btn));
    EXPECT_TRUE(m("Screen", *root));
    EXPECT_TRUE(m("LinearLayout", *inner));   // type de base enregistré
    EXPECT_FALSE(m("Button:focus", *btn));
    btn->set_has_focus(true);
    ctx.focused = btn.get();
    EXPECT_TRUE(m("Button:focus", *btn));
    EXPECT_TRUE(m("Horizontal:focus-within", *inner));
    btn->remove_class("big");
    EXPECT_FALSE(m(".big", *btn));
    root->set_disabled(true);
    EXPECT_TRUE(m("Button:disabled", *btn));   // hérité de l'ancêtre
}

// --- Feuilles de style ---------------------------------------------------

TEST(Stylesheet, CascadeSpecificiteEtOrdre) {
    css::Stylesheet sh;
    EXPECT_TRUE(sh.add_source(R"(
        Button { color: red; background: #000; }
        .x { color: blue; }
        Button { background: #111; }
        #b { padding: 1 2; }
    )"));
    Button b;
    b.set_id("b");
    b.add_class("x");
    auto st = sh.compute(b, {});
    EXPECT_EQ(*st.color, (Color{38, 139, 210}));   // .x bat Button
    EXPECT_EQ(*st.background, (Color{17, 17, 17}));   // dernière règle à égalité
    EXPECT_EQ(*st.padding, (Spacing{1, 2, 1, 2}));
    b.set_inline_style("color: #00ff00");
    EXPECT_EQ(*sh.compute(b, {}).color, (Color{0, 255, 0}));
}

TEST(Stylesheet, TierParDefautPlusFaible) {
    css::Stylesheet sh;
    (void)sh.add_source("#b { color: red; }", "<default>", 0);
    (void)sh.add_source("Button { color: blue; }", "<app>", 1);
    Button b;
    b.set_id("b");
    EXPECT_EQ(*sh.compute(b, {}).color, (Color{38, 139, 210}));
}

TEST(Stylesheet, VariablesEtTheme) {
    css::Stylesheet sh;
    sh.set_theme_variables({{"primary", "#102030"}, {"background", "#000000"}});
    EXPECT_TRUE(sh.add_source("$accent2: $primary; Label { color: $accent2; background: #ffffff 50%; }"));
    Label l("t");
    auto st = sh.compute(l, {});
    EXPECT_EQ(*st.color, (Color{16, 32, 48}));
    EXPECT_EQ(*st.background, (Color{128, 128, 128}));
    sh.set_theme_variables({{"primary", "#ffffff"}, {"background", "#000000"}});
    EXPECT_EQ(*sh.compute(l, {}).color, (Color{255, 255, 255}));
}

TEST(Stylesheet, ImbricationEtCommentaires) {
    css::Stylesheet sh;
    EXPECT_TRUE(sh.add_source(R"(
        /* bloc */
        Button {
            color: red; // fin de ligne
            &:hover { color: blue; }
            &.-big { padding: 2; }
        }
    )"));
    EXPECT_EQ(sh.rules().size(), 3u);
    Button b;
    b.set_hovered(true);
    EXPECT_EQ(*sh.compute(b, {}).color, (Color{38, 139, 210}));
    b.add_class("-big");
    EXPECT_EQ(sh.compute(b, {}).padding->top, 2);
}

TEST(Stylesheet, ErreursAvecPosition) {
    css::Stylesheet sh;
    EXPECT_FALSE(sh.add_source("Button {\n  color: pasunecouleur;\n  padding: a;\n}\nLabel:inconnu { color: red; }\n", "t.tcss"));
    ASSERT_GE(sh.errors().size(), 3u);
    bool line2 = false, line5 = false;
    for (auto& e : sh.errors()) {
        if (e.line == 2) line2 = true;
        if (e.line == 5) line5 = true;
        EXPECT_EQ(e.origin, "t.tcss");
    }
    EXPECT_TRUE(line2);
    EXPECT_TRUE(line5);
    EXPECT_EQ(sh.rules().size(), 1u);   // la règle valide est conservée
}

TEST(Stylesheet, ProprietesDiverses) {
    css::Stylesheet sh;
    EXPECT_FALSE(sh.add_source(
        "Label { text-style: bold underline; border: round $x; width: 50%; height: 3; display: none; "
        "text-align: center; min-width: 4; }",
        "<t>"));
    // $x inconnue : erreur de valeur, le reste s'applique.
    EXPECT_FALSE(sh.errors().empty());
    Label l("t");
    auto st = sh.compute(l, {});
    EXPECT_EQ(*st.text_style, TextStyle::Bold | TextStyle::Underline);
    EXPECT_EQ(st.width->unit, Dimension::Unit::Percent);
    EXPECT_EQ(*st.height->resolve(10), 3);
    EXPECT_FALSE(*st.display);
    EXPECT_EQ(st.extra_value("text-align"), "center");
    EXPECT_EQ(*st.min_width, 4);
    sh.set_theme_variables({{"x", "#ff0000"}});
    EXPECT_TRUE(sh.errors().empty());
    EXPECT_EQ(*sh.compute(l, {}).border, BorderStyle::Rounded);
}

TEST(Stylesheet, RetraitParOrigine) {
    css::Stylesheet sh;
    (void)sh.add_source("$v: #fff; Label { color: $v; }", "a");
    (void)sh.add_source("Button { color: red; }", "b");
    sh.remove_origin("a");
    EXPECT_EQ(sh.rules().size(), 1u);
    EXPECT_FALSE(sh.variables().contains("v"));
}

// --- Thèmes ---------------------------------------------------------------

TEST(Themes, IntegresEtVariables) {
    auto names = themes::names();
    EXPECT_GE(names.size(), 15u);
    ASSERT_TRUE(themes::find("textual-dark") != nullptr);
    ASSERT_TRUE(themes::find("nord") != nullptr);
    EXPECT_TRUE(themes::find("inexistant") == nullptr);
    auto v = themes::generate_variables(*themes::find("textual-dark"));
    EXPECT_EQ(v["primary"], "#0178D4");
    EXPECT_EQ(v["background"], "#121212");
    for (const char* k : {"primary-lighten-3", "primary-darken-3", "text-muted", "panel", "surface", "boost",
                          "border", "footer-key-foreground", "block-cursor-background", "accent-muted"})
        EXPECT_TRUE(v.contains(k)) << k;
    auto lum = [&](const char* k) { return color::luminance(*color::parse(v[k])); };
    EXPECT_GT(lum("primary-lighten-1"), lum("primary"));
    EXPECT_LT(lum("primary-darken-1"), lum("primary"));
    EXPECT_FALSE(themes::find("textual-light")->dark);
}

TEST(Themes, EnregistrementEtPaletteParRole) {
    themes::ThemeSpec t;
    t.name = "maison";
    t.primary = {200, 0, 0};
    t.background = Color{10, 10, 10};
    t.variables["ma-variable"] = "#123456";
    themes::register_theme(t);
    ASSERT_TRUE(themes::find("maison") != nullptr);
    auto v = themes::generate_variables(*themes::find("maison"));
    EXPECT_EQ(v["ma-variable"], "#123456");
    EXPECT_EQ(v["secondary"], "#C80000");   // repli sur primary
    Theme role = themes::to_role_theme(*themes::find("maison"));
    EXPECT_EQ(role.resolve(ColorRole::Background), (Color{10, 10, 10}));
}

// --- Intégration Widget / App ------------------------------------------------

TEST(StyledWidget, SansFeuilleRenduInchange) {
    Leaf l;
    EXPECT_FALSE(l.styled());
    EXPECT_EQ(l.measure_outer(Constraints::loose({20, 5})), (Size{4, 1}));
    l.layout({2, 3, 4, 1});
    EXPECT_EQ(l.bounds(), (Rect{2, 3, 4, 1}));
    EXPECT_EQ(l.outer_bounds(), l.bounds());
    Buffer b(10, 5);
    l.render(b);
    EXPECT_EQ(b.at(2, 3).codepoint, U'x');
    EXPECT_TRUE(b.at(2, 3).bg.is_default);
}

TEST(StyledWidget, BoiteBordurePaddingDimensions) {
    Leaf l;
    css::Stylesheet sh;
    (void)sh.add_source("Leaf { border: solid #ff0000; padding: 0 1; background: #000080; color: #ffffff; width: 10; }");
    sh.apply(l, {});
    ASSERT_TRUE(l.styled());
    EXPECT_EQ(l.measure_outer(Constraints::loose({40, 10})), (Size{10, 3}));
    l.layout({0, 0, 10, 3});
    EXPECT_EQ(l.bounds(), (Rect{2, 1, 6, 1}));
    EXPECT_EQ(l.outer_bounds(), (Rect{0, 0, 10, 3}));
    Buffer b(12, 4);
    l.render(b);
    EXPECT_EQ(b.at(0, 0).codepoint, U'┌');
    EXPECT_EQ(b.at(0, 0).fg, (Color{255, 0, 0}));
    EXPECT_EQ(b.at(1, 1).bg, (Color{0, 0, 128}));    // padding au fond du style
    EXPECT_EQ(b.at(2, 1).codepoint, U'x');
    EXPECT_EQ(b.at(2, 1).fg, (Color{255, 255, 255}));   // couleur héritée
    EXPECT_TRUE(b.at(11, 3).bg.is_default);            // hors de la boîte
}

TEST(StyledWidget, TitreDeBordureEtDisplayNone) {
    auto root = std::make_shared<Vertical>();
    auto a = std::make_shared<Leaf>();
    auto bwid = std::make_shared<Leaf>();
    a->set_border_title("Titre");
    a->add_class("cadre");
    bwid->add_class("cache");
    root->add_child(a);
    root->add_child(bwid);
    css::Stylesheet sh;
    (void)sh.add_source(".cadre { border: round; width: 12; } .cache { display: none; }");
    sh.apply(*root, {});
    EXPECT_FALSE(bwid->visible());
    Buffer buf(20, 6);
    (void)root->measure_outer(Constraints::tight({20, 6}));
    root->layout({0, 0, 20, 6});
    root->render(buf);
    EXPECT_EQ(buf.at(0, 0).codepoint, U'╭');
    EXPECT_EQ(buf.at(3, 0).codepoint, U'T');
}

TEST(AppStyle, ThemeCssEtFeuilleParDefaut) {
    auto be = backend(30, 5);
    auto* raw = be.get();
    App app(std::move(be));
    auto root = std::make_shared<Vertical>();
    auto btn = std::make_shared<Button>("Go");
    btn->add_class("-primary");
    root->add_child(btn);
    app.set_root(root);
    EXPECT_EQ(app.theme_name(), "textual-dark");
    app.refresh();
    EXPECT_TRUE(raw->painted().at(0, 0).bg.is_default);   // sans feuille : rien ne change

    app.use_default_stylesheet();
    app.refresh();
    Color bg = *color::parse("#121212");
    EXPECT_EQ(raw->painted().at(29, 4).bg, bg);             // Screen { background: $background }
    EXPECT_EQ(btn->style().background, *color::parse("#0178D4"));

    EXPECT_TRUE(app.set_theme("nord"));
    app.refresh();
    EXPECT_EQ(raw->painted().at(29, 4).bg, *color::parse("#2E3440"));
    EXPECT_FALSE(app.set_theme("inexistant"));
    EXPECT_FALSE(app.set_css("Button { color: zzz; }"));
    EXPECT_FALSE(app.css_errors().empty());
    EXPECT_TRUE(app.set_css("Button { background: #010203; }"));   // remplace le CSS précédent
    app.refresh();
    EXPECT_EQ(btn->style().background, (Color{1, 2, 3}));
    std::string next = app.next_theme();
    EXPECT_NE(next, "nord");
}

TEST(AppStyle, FichierCss) {
    auto path = std::filesystem::temp_directory_path() / "tui_style_test.tcss";
    {
        std::ofstream(path) << "Label { color: #abcdef; }\n";
    }
    App app(backend(10, 2));
    auto l = std::make_shared<Label>("x");
    app.set_root(l);
    EXPECT_TRUE(app.load_css_file(path));
    app.refresh();
    EXPECT_EQ(*l->style().color, (Color{0xab, 0xcd, 0xef}));
    std::filesystem::remove(path);
    EXPECT_FALSE(app.load_css_file(path));
}
