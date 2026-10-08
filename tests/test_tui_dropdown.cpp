#include <gtest/gtest.h>
#include <tui/widgets/Dropdown.hpp>

using namespace tui;

TEST(Dropdown, StartsClosedWithFirstOptionSelected) {
    Dropdown dd;
    dd.set_options({"Red", "Green", "Blue"});
    EXPECT_FALSE(dd.is_open());
    EXPECT_EQ(dd.selected_index(), 0u);
}

TEST(Dropdown, EnterOpensThenNavigateThenConfirm) {
    Dropdown dd;
    dd.set_options({"Red", "Green", "Blue"});
    dd.arrange(Rect{0, 0, 20, 1});

    dd.on_key(KeyEvent{Key::Enter});
    EXPECT_TRUE(dd.is_open());
    dd.arrange(Rect{0, 0, 20, 4}); // ré-arrange une fois ouvert (le popup a besoin de place)

    dd.on_key(KeyEvent{Key::Down});
    dd.on_key(KeyEvent{Key::Enter}); // confirme
    EXPECT_FALSE(dd.is_open());
    EXPECT_EQ(dd.selected_index(), 1u);
}

TEST(Dropdown, EscapeCancelsWithoutChangingSelection) {
    Dropdown dd;
    dd.set_options({"Red", "Green", "Blue"});
    dd.on_key(KeyEvent{Key::Enter});
    dd.on_key(KeyEvent{Key::Down});
    dd.on_key(KeyEvent{Key::Down});
    dd.on_key(KeyEvent{Key::Escape});
    EXPECT_FALSE(dd.is_open());
    EXPECT_EQ(dd.selected_index(), 0u); // annulé : toujours la sélection d'origine
}

TEST(Dropdown, SetSelectedIndexFiresOnChange) {
    Dropdown dd;
    dd.set_options({"Red", "Green", "Blue"});
    size_t last = 999;
    dd.set_on_change([&](size_t i) { last = i; });
    dd.set_selected_index(2);
    EXPECT_EQ(dd.selected_index(), 2u);
    EXPECT_EQ(last, 2u);
}

TEST(Dropdown, IsFocusableEvenWithNoOptions) {
    Dropdown dd;
    EXPECT_TRUE(dd.focusable());
}

TEST(Dropdown, SpaceKeyAlsoOpensWhenClosed) {
    Dropdown dd;
    dd.set_options({"A", "B"});
    dd.on_key(KeyEvent{Key::Char, U' '});
    EXPECT_TRUE(dd.is_open());
}
