/* @file tests/test_tui_mouse.cpp — focus au clic, remontée, capture, survol. */

#include <gtest/gtest.h>
#include <tui/App.hpp>
#include <tui/Tui.hpp>
#include <tui/terminal/HeadlessTerminalBackend.hpp>

using namespace tui;

namespace {

std::unique_ptr<HeadlessTerminalBackend> backend(int w, int h) {
    auto b = std::make_unique<HeadlessTerminalBackend>();
    b->set_size(w, h);
    return b;
}

MouseEvent mouse(int x, int y, MouseEvent::Action a, MouseEvent::Button b = MouseEvent::Button::Left) {
    MouseEvent e;
    e.x = x;
    e.y = y;
    e.action = a;
    e.button = b;
    return e;
}

/// Feuille non focusable qui ne consomme rien.
struct Passive : Widget {
    int seen = 0;
    [[nodiscard]] Size measure(const Constraints& c) const override { return c.clamp({c.max_width, 1}); }
    void paint(Buffer&) const override {}
    bool on_mouse(const MouseEvent&) override {
        ++seen;
        return false;
    }
};

/// Conteneur qui consomme les clics et compte les événements.
struct Catcher : Vertical {
    int presses = 0, releases = 0, drags = 0;
    bool on_mouse(const MouseEvent& e) override {
        if (e.action == MouseEvent::Action::Press) ++presses;
        if (e.action == MouseEvent::Action::Release) ++releases;
        if (e.action == MouseEvent::Action::Drag) ++drags;
        return e.action != MouseEvent::Action::Move;
    }
};

} // namespace

TEST(Mouse, ClicDonneLeFocus) {
    App app(backend(20, 4));
    auto root = std::make_shared<Vertical>();
    auto a = std::make_shared<Input>();
    auto b = std::make_shared<Input>();
    root->add_child(a);
    root->add_child(b);
    app.set_root(root);
    app.refresh();
    app.dispatch(Event{mouse(3, 1, MouseEvent::Action::Press)});
    EXPECT_EQ(app.focus_manager().current(), b);
    EXPECT_TRUE(b->has_focus());
    app.dispatch(Event{mouse(3, 0, MouseEvent::Action::Press)});
    EXPECT_EQ(app.focus_manager().current(), a);
    EXPECT_FALSE(b->has_focus());
}

TEST(Mouse, ClicSurEnfantFocaliseLAncetreFocusable) {
    App app(backend(20, 4));
    auto coll = std::make_shared<Collapsible>("Section", false);
    auto inside = std::make_shared<Passive>();
    coll->set_content(inside);
    app.set_root(coll);
    app.refresh();
    app.dispatch(Event{mouse(2, 1, MouseEvent::Action::Press)});
    EXPECT_EQ(inside->seen, 1);
    EXPECT_EQ(app.focus_manager().current(), coll);
}

TEST(Mouse, RemonteeVersLeParent) {
    App app(backend(20, 4));
    auto root = std::make_shared<Catcher>();
    auto leaf = std::make_shared<Passive>();
    root->add_child(leaf);
    app.set_root(root);
    app.refresh();
    app.dispatch(Event{mouse(1, 0, MouseEvent::Action::Press)});
    EXPECT_EQ(leaf->seen, 1);
    EXPECT_EQ(root->presses, 1);
}

TEST(Mouse, CaptureJusquauRelachement) {
    App app(backend(20, 4));
    auto root = std::make_shared<Vertical>();
    auto top = std::make_shared<Catcher>();
    auto other = std::make_shared<Catcher>();
    top->add_child(std::make_shared<Passive>());
    other->add_child(std::make_shared<Passive>());
    root->add_child(top);
    root->add_child(other);
    app.set_root(root);
    app.refresh();
    app.dispatch(Event{mouse(1, 0, MouseEvent::Action::Press)});
    app.dispatch(Event{mouse(1, 1, MouseEvent::Action::Drag)});
    app.dispatch(Event{mouse(1, 1, MouseEvent::Action::Release)});
    EXPECT_EQ(top->presses, 1);
    EXPECT_EQ(top->drags, 1);
    EXPECT_EQ(top->releases, 1);
    EXPECT_EQ(other->drags + other->releases, 0);
}

TEST(Mouse, BoutonRelacheHorsDuBoutonAnnule) {
    App app(backend(20, 4));
    auto root = std::make_shared<Vertical>();
    auto btn = std::make_shared<Button>("OK");
    int clicks = 0;
    btn->set_on_click([&] { ++clicks; });
    root->add_child(btn);
    root->add_child(std::make_shared<Passive>());
    app.set_root(root);
    app.refresh();
    app.dispatch(Event{mouse(1, 0, MouseEvent::Action::Press)});
    app.dispatch(Event{mouse(1, 0, MouseEvent::Action::Release)});
    EXPECT_EQ(clicks, 1);
    app.dispatch(Event{mouse(1, 0, MouseEvent::Action::Press)});
    app.dispatch(Event{mouse(10, 1, MouseEvent::Action::Release)});
    EXPECT_EQ(clicks, 1);
    EXPECT_FALSE(btn->pressed());
}

TEST(Mouse, SurvolEtPseudoClasseHover) {
    App app(backend(20, 4));
    auto root = std::make_shared<Vertical>();
    auto btn = std::make_shared<Button>("OK");
    root->add_child(btn);
    root->add_child(std::make_shared<Passive>());
    app.set_root(root);
    EXPECT_TRUE(app.set_css("Button:hover { background: #ff0000; }"));
    app.refresh();
    EXPECT_FALSE(btn->style().background.has_value());
    app.dispatch(Event{mouse(1, 0, MouseEvent::Action::Move, MouseEvent::Button::None)});
    EXPECT_TRUE(btn->hovered());
    EXPECT_TRUE(root->hovered());
    EXPECT_EQ(app.hovered(), btn);
    app.refresh();
    EXPECT_EQ(*btn->style().background, (Color{255, 0, 0}));
    app.dispatch(Event{mouse(1, 1, MouseEvent::Action::Move, MouseEvent::Button::None)});
    EXPECT_FALSE(btn->hovered());
}

TEST(Mouse, WidgetDesactiveIgnore) {
    App app(backend(20, 4));
    auto root = std::make_shared<Vertical>();
    auto a = std::make_shared<Input>();
    a->set_disabled(true);
    root->add_child(a);
    app.set_root(root);
    app.refresh();
    app.dispatch(Event{mouse(1, 0, MouseEvent::Action::Press)});
    EXPECT_EQ(app.focus_manager().current(), nullptr);
    app.focus_manager().focus_next(root);
    EXPECT_EQ(app.focus_manager().current(), nullptr);   // non focusable au clavier non plus
}

TEST(Mouse, ClicDansInputPlaceLeCurseur) {
    App app(backend(20, 2));
    auto in = std::make_shared<Input>();
    in->set_text("bonjour");
    app.set_root(in);
    app.refresh();
    app.dispatch(Event{mouse(3, 0, MouseEvent::Action::Press)});
    EXPECT_EQ(in->cursor(), 3u);
    app.dispatch(Event{mouse(15, 0, MouseEvent::Action::Press)});
    EXPECT_EQ(in->cursor(), 7u);
}

TEST(Mouse, ModaleIsoleLeFond) {
    App app(backend(30, 10));
    auto stack = std::make_shared<Stack>();
    auto back = std::make_shared<Input>();
    auto dlg = std::make_shared<Dialog>();
    auto inner = std::make_shared<Input>();
    dlg->set_content(inner);
    dlg->set_size({20, 5});
    dlg->set_focus_manager(&app.focus_manager());
    stack->add_child(back);
    stack->add_child(dlg);
    app.set_root(stack);
    dlg->show();
    app.refresh();
    app.dispatch(Event{mouse(0, 0, MouseEvent::Action::Press)});   // scrim
    EXPECT_NE(app.focus_manager().current(), back);
    app.dispatch(Event{mouse(10, 3, MouseEvent::Action::Press)});  // contenu de la modale
    EXPECT_EQ(app.focus_manager().current(), inner);
}

TEST(VTParserMouse, MouvementSansBouton) {
    VTParser p;
    p.feed("\x1b[<35;5;3M");
    auto evs = p.pump();
    ASSERT_EQ(evs.size(), 1u);
    auto& me = std::get<MouseEvent>(evs[0]);
    EXPECT_EQ(me.action, MouseEvent::Action::Move);
    EXPECT_EQ(me.button, MouseEvent::Button::None);
    EXPECT_EQ(me.x, 4);
    p.feed("\x1b[<32;5;3M");
    auto drag = p.pump();
    ASSERT_EQ(drag.size(), 1u);
    EXPECT_EQ(std::get<MouseEvent>(drag[0]).action, MouseEvent::Action::Drag);
}
