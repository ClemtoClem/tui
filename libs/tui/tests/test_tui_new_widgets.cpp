/* @file tests/test_tui_new_widgets.cpp — Header, Footer, Rule, Sparkline, Digits,
 * LoadingIndicator, Log, Collapsible, ContentSwitcher, Placeholder, Link. */

#include <gtest/gtest.h>
#include <tui/Tui.hpp>

using namespace tui;

namespace {
std::string row(const Buffer& b, int y, int x0 = 0, int w = -1) {
    std::u32string s;
    int end = w < 0 ? b.width() : x0 + w;
    for (int x = x0; x < end; ++x)
        if (b.at(x, y).width != 0) s += b.at(x, y).codepoint;
    return TextHelper::encode_utf8(s);
}
MouseEvent press(int x, int y) {
    MouseEvent e;
    e.x = x;
    e.y = y;
    e.button = MouseEvent::Button::Left;
    e.action = MouseEvent::Action::Press;
    return e;
}
} // namespace

TEST(NewWidgets, HeaderCentreTitreEtSousTitre) {
    Header h("Titre", "sous");
    Buffer b(40, 1);
    h.arrange({0, 0, 40, 1});
    h.paint(b);
    auto r = row(b, 0);
    EXPECT_NE(r.find("Titre — sous"), std::string::npos);
    EXPECT_NE(r.find("⭘"), std::string::npos);
    bool clicked = false;
    h.set_on_icon_click([&] { clicked = true; });
    EXPECT_TRUE(h.on_mouse(press(1, 0)));
    EXPECT_TRUE(clicked);
}

TEST(NewWidgets, FooterRaccourcisCliquables) {
    Footer f;
    int quit = 0, help = 0;
    f.add_binding({"^q", "Quitter", [&] { ++quit; }});
    f.add_binding({"F1", "Aide", [&] { ++help; }});
    Buffer b(40, 1);
    f.arrange({0, 0, 40, 1});
    f.paint(b);
    EXPECT_EQ(row(b, 0).substr(0, 20), " ^q Quitter  F1 Aide");
    EXPECT_EQ(f.index_at(1), 0);
    EXPECT_EQ(f.index_at(13), 1);
    EXPECT_TRUE(f.on_mouse(press(15, 0)));
    EXPECT_EQ(help, 1);
    EXPECT_EQ(quit, 0);
    MouseEvent mv = press(2, 0);
    mv.action = MouseEvent::Action::Move;
    mv.button = MouseEvent::Button::None;
    EXPECT_TRUE(f.on_mouse(mv));   // survol : repaint
}

TEST(NewWidgets, RuleHorizontaleEtVerticale) {
    Rule h;
    Buffer b(5, 3);
    EXPECT_EQ(h.measure(Constraints::loose({5, 3})), (Size{5, 1}));
    h.arrange({0, 0, 5, 1});
    h.paint(b);
    EXPECT_EQ(row(b, 0), "─────");
    Rule v(Rule::Orientation::Vertical, Rule::Line::Double);
    EXPECT_EQ(v.measure(Constraints::loose({5, 3})), (Size{1, 3}));
    v.arrange({4, 0, 1, 3});
    v.paint(b);
    EXPECT_EQ(b.at(4, 2).codepoint, U'║');
}

TEST(NewWidgets, SparklineReechantillonne) {
    Sparkline s({0, 1, 2, 3, 4, 5, 6, 7});
    auto bk = s.buckets(4);
    ASSERT_EQ(bk.size(), 4u);
    EXPECT_DOUBLE_EQ(bk[0], 1);
    EXPECT_DOUBLE_EQ(bk[3], 7);
    s.set_summary(Sparkline::Summary::Mean);
    EXPECT_DOUBLE_EQ(s.buckets(4)[0], 0.5);
    Buffer b(8, 1);
    s.set_summary(Sparkline::Summary::Max);
    s.arrange({0, 0, 8, 1});
    s.paint(b);
    EXPECT_EQ(b.at(7, 0).codepoint, U'█');
    EXPECT_EQ(b.at(0, 0).codepoint, U'▁');
    s.push(1, 3);
    EXPECT_EQ(s.data().size(), 3u);
}

TEST(NewWidgets, DigitsSurTroisLignes) {
    Digits d("12:5");
    auto r = d.rows();
    EXPECT_EQ(r[0], "╶┐ ╶─╮ ╭─╴");
    EXPECT_EQ(r[1], " │ ┌─┘:╰─╮");
    EXPECT_EQ(d.measure(Constraints::loose({80, 10})), (Size{10, 3}));
    Buffer b(12, 3);
    d.arrange({0, 0, 12, 3});
    d.paint(b);
    EXPECT_EQ(row(b, 2, 0, 3), "╶┴╴");
}

TEST(NewWidgets, LoadingIndicatorAnime) {
    LoadingIndicator li;
    double before = li.intensity(0);
    for (int i = 0; i < 4; ++i) li.advance();
    EXPECT_NE(li.intensity(0), before);
    EXPECT_DOUBLE_EQ(li.intensity(1), 1.0);
    Buffer b(11, 1);
    li.arrange({0, 0, 11, 1});
    li.paint(b);
    EXPECT_EQ(b.at(1, 0).codepoint, U'●');
}

TEST(NewWidgets, LogDefileEtLimite) {
    Log log;
    log.arrange({0, 0, 20, 3});
    for (int i = 0; i < 10; ++i) log.write_line("ligne " + std::to_string(i));
    EXPECT_EQ(log.lines().size(), 10u);
    EXPECT_EQ(log.scroll(), 7);   // suit la fin
    log.write("a");
    log.write("b\nc");
    EXPECT_EQ(log.lines()[10], "ab");
    EXPECT_EQ(log.lines().back(), "c");
    KeyEvent home;
    home.key = Key::Home;
    EXPECT_TRUE(log.on_key(home));
    EXPECT_EQ(log.scroll(), 0);
    log.write_line("x");
    EXPECT_EQ(log.scroll(), 0);   // ne suit plus quand on a remonté
    log.set_max_lines(5);
    EXPECT_EQ(log.lines().size(), 5u);
    Buffer b(20, 3);
    log.paint(b);
    EXPECT_EQ(row(b, 0, 0, 7), "ligne 8");   // 5 dernières : 8, 9, ab, c, x
}

TEST(NewWidgets, CollapsibleBascule) {
    auto c = std::make_shared<Collapsible>("Options");
    auto inner = std::make_shared<Label>("contenu");
    c->set_content(inner);
    EXPECT_TRUE(c->collapsed());
    EXPECT_TRUE(c->children().empty());
    EXPECT_EQ(c->measure(Constraints::loose({20, 10})).height, 1);
    bool state = true;
    c->set_on_toggle([&](bool col) { state = col; });
    KeyEvent enter;
    enter.key = Key::Enter;
    EXPECT_TRUE(c->on_key(enter));
    EXPECT_FALSE(state);
    EXPECT_EQ(c->children().size(), 1u);
    EXPECT_EQ(c->measure(Constraints::loose({20, 10})).height, 2);
    Buffer b(20, 3);
    c->arrange({0, 0, 20, 3});
    c->paint(b);
    EXPECT_EQ(row(b, 0, 0, 9), "▼ Options");
    EXPECT_EQ(row(b, 1, 0, 7), "contenu");
    EXPECT_TRUE(c->on_mouse(press(0, 0)));
    EXPECT_TRUE(c->collapsed());
}

TEST(NewWidgets, ContentSwitcherUnePageALaFois) {
    ContentSwitcher sw;
    auto a = std::make_shared<Label>("A");
    auto bb = std::make_shared<Label>("B");
    sw.add_page(a, "a");
    sw.add_page(bb, "b");
    EXPECT_EQ(sw.current(), "a");
    EXPECT_EQ(sw.children().size(), 1u);
    EXPECT_TRUE(sw.set_current("b"));
    EXPECT_FALSE(sw.set_current("zz"));
    EXPECT_EQ(sw.current_widget(), bb);
    Buffer buf(5, 1);
    sw.arrange({0, 0, 5, 1});
    sw.paint(buf);
    EXPECT_EQ(buf.at(0, 0).codepoint, U'B');
}

TEST(NewWidgets, PlaceholderEtVariantes) {
    Placeholder p("Zone");
    Buffer b(10, 3);
    p.arrange({0, 0, 10, 3});
    p.paint(b);
    EXPECT_NE(row(b, 1).find("Zone"), std::string::npos);
    EXPECT_FALSE(b.at(0, 0).bg.is_default);
    EXPECT_TRUE(p.on_mouse(press(1, 1)));
    EXPECT_EQ(p.variant(), Placeholder::Variant::Size);
    Buffer b2(10, 3);
    p.paint(b2);
    EXPECT_NE(row(b2, 1).find("10 x 3"), std::string::npos);
}

TEST(NewWidgets, LinkActivable) {
    Link l("Documentation", "https://exemple.org");
    std::string got;
    l.set_on_click([&](const std::string& u) { got = u; });
    EXPECT_EQ(l.measure(Constraints::loose({80, 1})), (Size{13, 1}));
    EXPECT_TRUE(l.focusable());
    EXPECT_TRUE(l.on_mouse(press(0, 0)));
    EXPECT_EQ(got, "https://exemple.org");
    Buffer b(20, 1);
    l.arrange({0, 0, 20, 1});
    l.paint(b);
    EXPECT_TRUE(has_style(b.at(0, 0).style, TextStyle::Underline));
}

TEST(NewWidgets, NomsDeTypeCss) {
    EXPECT_EQ(Header().type_name(), "Header");
    EXPECT_EQ(Log().type_name(), "Log");
    EXPECT_EQ(Button().type_name(), "Button");
}
