#include <gtest/gtest.h>
#include <tui/widgets/MenuBar.hpp>

using namespace tui;

namespace {
std::vector<MenuBarEntry> sample_menus(bool& new_clicked, bool& copy_clicked) {
    std::vector<MenuBarEntry> menus;
    menus.push_back({"File", {
        MenuItem{"New", [&new_clicked] { new_clicked = true; }, false},
        MenuItem{"Open", nullptr, false},
    }});
    menus.push_back({"Edit", {
        MenuItem{"Copy", [&copy_clicked] { copy_clicked = true; }, false},
    }});
    return menus;
}
} // namespace

TEST(MenuBar, IsFocusableWithMenus) {
    MenuBar bar;
    bool a = false, b = false;
    bar.set_menus(sample_menus(a, b));
    EXPECT_TRUE(bar.focusable());
}

TEST(MenuBar, EnterOpensSubmenuAndActivatesFirstItem) {
    MenuBar bar;
    bool new_clicked = false, copy_clicked = false;
    bar.set_menus(sample_menus(new_clicked, copy_clicked));
    bar.arrange(Rect{0, 0, 40, 1});
    bar.on_focus();

    bar.on_key(KeyEvent{Key::Enter}); // ouvre "File"
    bar.on_key(KeyEvent{Key::Enter}); // active l'item sélectionné ("New")
    EXPECT_TRUE(new_clicked);
    EXPECT_FALSE(copy_clicked);
}

TEST(MenuBar, DownThenEnterActivatesSecondItem) {
    MenuBar bar;
    bool new_clicked = false, copy_clicked = false;
    bar.set_menus(sample_menus(new_clicked, copy_clicked));
    bar.arrange(Rect{0, 0, 40, 1});
    bar.on_focus();

    bar.on_key(KeyEvent{Key::Enter}); // ouvre "File"
    bar.on_key(KeyEvent{Key::Down});  // sélectionne "Open" (pas de callback -> rien ne se passe)
    bar.on_key(KeyEvent{Key::Enter}); // active "Open"
    EXPECT_FALSE(new_clicked);
}

TEST(MenuBar, EscapeClosesSubmenuWithoutActivating) {
    MenuBar bar;
    bool new_clicked = false, copy_clicked = false;
    bar.set_menus(sample_menus(new_clicked, copy_clicked));
    bar.arrange(Rect{0, 0, 40, 1});
    bar.on_focus();

    bar.on_key(KeyEvent{Key::Enter});
    bar.on_key(KeyEvent{Key::Escape});
    EXPECT_FALSE(new_clicked);
    EXPECT_FALSE(copy_clicked);
}

TEST(MenuBar, RightArrowMovesToNextMenuAndReopens) {
    MenuBar bar;
    bool new_clicked = false, copy_clicked = false;
    bar.set_menus(sample_menus(new_clicked, copy_clicked));
    bar.arrange(Rect{0, 0, 40, 1});
    bar.on_focus();

    bar.on_key(KeyEvent{Key::Enter}); // ouvre "File"
    bar.on_key(KeyEvent{Key::Right}); // passe à "Edit", rouvre son menu
    bar.on_key(KeyEvent{Key::Enter}); // active "Copy" (seul item de "Edit")
    EXPECT_TRUE(copy_clicked);
    EXPECT_FALSE(new_clicked);
}

TEST(MenuBar, BlurClosesOpenSubmenu) {
    MenuBar bar;
    bool a = false, b = false;
    bar.set_menus(sample_menus(a, b));
    bar.arrange(Rect{0, 0, 40, 1});
    bar.on_focus();
    bar.on_key(KeyEvent{Key::Enter});
    bar.on_blur();
    // pas d'assertion directe sur l'état interne du dialog, mais ne doit pas planter
    // et une réouverture ultérieure doit repartir de zéro.
    bar.on_focus();
    bar.on_key(KeyEvent{Key::Enter});
    bar.on_key(KeyEvent{Key::Enter});
    EXPECT_TRUE(a);
}
