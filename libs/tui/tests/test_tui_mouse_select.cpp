/* @file tests/test_tui_mouse_select.cpp — sélection à la souris dans les widgets. */

#include <gtest/gtest.h>
#include <tui/App.hpp>
#include <tui/Tui.hpp>
#include <tui/terminal/HeadlessTerminalBackend.hpp>

using namespace tui;

namespace {

MouseEvent press(int x, int y) {
    MouseEvent e;
    e.x = x;
    e.y = y;
    e.button = MouseEvent::Button::Left;
    e.action = MouseEvent::Action::Press;
    return e;
}
MouseEvent release(int x, int y) {
    MouseEvent e = press(x, y);
    e.action = MouseEvent::Action::Release;
    return e;
}

struct Fixture {
    HeadlessTerminalBackend* raw = nullptr;
    std::unique_ptr<App> app;
    explicit Fixture(int w = 40, int h = 10) {
        auto b = std::make_unique<HeadlessTerminalBackend>();
        b->set_size(w, h);
        raw = b.get();
        app = std::make_unique<App>(std::move(b));
    }
    void click(int x, int y) {
        app->dispatch(Event{press(x, y)});
        app->dispatch(Event{release(x, y)});
        app->refresh();
    }
};

} // namespace

TEST(MouseSelect, RunActiveLaSouris) {
    Fixture f;
    f.app->set_root(std::make_shared<Label>("x"));
    f.app->add_timer(std::chrono::milliseconds(0), false, [&] {
        EXPECT_TRUE(f.raw->mouse_enabled());
        f.app->quit();
    });
    EXPECT_EQ(f.app->run(), 0);
}

TEST(MouseSelect, SourisDesactivable) {
    Fixture f;
    f.app->set_root(std::make_shared<Label>("x"));
    f.app->set_mouse_enabled(false);
    f.app->add_timer(std::chrono::milliseconds(0), false, [&] {
        EXPECT_FALSE(f.raw->mouse_enabled());
        f.app->quit();
    });
    (void)f.app->run();
}

TEST(MouseSelect, DoubleClicDetecte) {
    Fixture f;
    auto list = std::make_shared<ListView>();
    list->set_items({{"a", "", true, ""}, {"b", "", true, ""}});
    int activated = -1;
    list->set_on_activate([&](size_t i) { activated = static_cast<int>(i); });
    f.app->set_root(list);
    f.app->refresh();
    f.click(1, 1);
    EXPECT_EQ(list->selected_index(), 1u);
    EXPECT_EQ(activated, -1);          // simple clic : sélection seule
    f.click(1, 1);
    EXPECT_EQ(activated, 1);           // double-clic : activation
    activated = -1;
    f.click(1, 0);                     // autre cellule : compteur remis à 1
    EXPECT_EQ(activated, -1);
}

TEST(MouseSelect, OngletsCliquables) {
    Fixture f;
    auto tabs = std::make_shared<Tabs>();
    tabs->add_tab("Un", std::make_shared<Label>("1"));
    tabs->add_tab("Deux", std::make_shared<Label>("2"));
    size_t changed = 99;
    tabs->set_on_change([&](size_t i) { changed = i; });
    f.app->set_root(tabs);
    f.app->refresh();
    EXPECT_EQ(tabs->tab_at(1, 0), 0);
    EXPECT_EQ(tabs->tab_at(5, 0), 1);   // " Un " occupe 0..3, " Deux " 4..9
    f.click(6, 0);
    EXPECT_EQ(tabs->active_index(), 1u);
    EXPECT_EQ(changed, 1u);
    EXPECT_EQ(f.app->focus_manager().current(), tabs);
}

TEST(MouseSelect, RadioSetEtCheckboxList) {
    Fixture f;
    auto root = std::make_shared<Vertical>();
    auto radio = std::make_shared<RadioSet>();
    radio->set_options({"A", "B", "C"});
    auto checks = std::make_shared<CheckboxList>();
    checks->set_items({{"x", false}, {"y", false}});
    root->add_child(radio, LayoutParams::fixed(3));
    root->add_child(checks, LayoutParams::fixed(2));
    f.app->set_root(root);
    f.app->refresh();
    f.click(2, 2);
    EXPECT_EQ(radio->selected_index(), 2u);
    EXPECT_EQ(f.app->focus_manager().current(), radio);
    f.click(2, 4);
    EXPECT_TRUE(checks->items()[1].checked);
    EXPECT_EQ(checks->selected_index(), 1u);
    f.click(2, 4);
    EXPECT_FALSE(checks->items()[1].checked);
}

TEST(MouseSelect, TableauSelectionEtActivation) {
    Fixture f;
    auto table = std::make_shared<TableView>();
    table->set_columns({{"Nom"}, {"Valeur"}});
    table->set_row_count(20);
    table->set_cell_accessor([](int r, int c) { return std::to_string(r * 10 + c); });
    int activated = -1;
    table->set_on_activate([&](int r) { activated = r; });
    f.app->set_root(table);
    f.app->refresh();
    f.click(2, 3);                      // ligne 0 = en-tête
    EXPECT_EQ(table->selected_row(), 2);
    EXPECT_EQ(activated, -1);
    f.click(2, 3);
    EXPECT_EQ(activated, 2);
    MouseEvent wheel;
    wheel.x = 2;
    wheel.y = 3;
    wheel.button = MouseEvent::Button::WheelDown;
    wheel.action = MouseEvent::Action::Press;
    f.app->dispatch(Event{wheel});
    f.click(2, 1);                      // 1re ligne visible après défilement de 3
    EXPECT_EQ(table->selected_row(), 3);
}

TEST(MouseSelect, ListeDeroulante) {
    Fixture f(30, 10);
    auto root = std::make_shared<Vertical>();
    auto dd = std::make_shared<Dropdown>();
    dd->set_options({"rouge", "vert", "bleu"});
    root->add_child(dd, LayoutParams::auto_size());
    root->add_child(std::make_shared<Label>("dessous"), LayoutParams::stretch(1));
    f.app->set_root(root);
    f.app->refresh();
    f.click(1, 0);
    EXPECT_TRUE(dd->is_open());
    EXPECT_EQ(f.app->widget_at(1, 3), dd);   // le popup capte la souris
    f.click(1, 3);                            // 3e option
    EXPECT_FALSE(dd->is_open());
    EXPECT_EQ(dd->selected_index(), 2u);
}

TEST(MouseSelect, MenusDeroulants) {
    Fixture f(40, 10);
    auto root = std::make_shared<Vertical>();
    auto bar = std::make_shared<MenuBar>();
    std::string done;
    bar->set_menus({{"Fichier", {{"Ouvrir", [&] { done = "ouvrir"; }}, {"", nullptr, true}, {"Quitter", [&] { done = "quitter"; }}}},
                    {"Aide", {{"À propos", [&] { done = "apropos"; }}}}});
    root->add_child(bar, LayoutParams::fixed(1));
    root->add_child(std::make_shared<Label>("contenu"), LayoutParams::stretch(1));
    f.app->set_root(root);
    f.app->refresh();
    EXPECT_EQ(bar->menu_at(2), 0);
    EXPECT_EQ(bar->menu_at(11), 1);
    f.click(2, 0);
    EXPECT_TRUE(bar->has_open_overlay());
    EXPECT_EQ(f.app->widget_at(2, 3), bar);
    f.click(2, 2);                       // séparateur : ignoré, menu toujours ouvert
    EXPECT_TRUE(bar->has_open_overlay());
    f.click(2, 3);
    EXPECT_EQ(done, "quitter");
    EXPECT_FALSE(bar->has_open_overlay());
    f.click(11, 0);                      // « Aide »
    f.click(11, 0);                      // re-clic : referme
    EXPECT_FALSE(bar->has_open_overlay());
}

TEST(MouseSelect, ArbreDoubleClicDeplie) {
    Fixture f;
    auto tree = std::make_shared<TreeView>();
    auto root_node = std::make_shared<TreeNode>();
    root_node->label = "racine";
    auto child = std::make_shared<TreeNode>();
    child->label = "enfant";
    root_node->children.push_back(child);
    tree->set_root(root_node);
    f.app->set_root(tree);
    f.app->refresh();
    f.click(5, 0);
    EXPECT_FALSE(root_node->expanded);
    f.click(5, 0);                       // double-clic sur le libellé
    EXPECT_TRUE(root_node->expanded);
}
