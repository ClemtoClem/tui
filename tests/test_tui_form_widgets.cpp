#include <gtest/gtest.h>
#include <tui/widgets/Checkbox.hpp>
#include <tui/widgets/CheckboxList.hpp>
#include <tui/widgets/Dropdown.hpp>
#include <tui/widgets/RadioSet.hpp>
#include <tui/widgets/ToggleSwitch.hpp>

using namespace tui;

TEST(Checkbox, StartsUnchecked) {
    Checkbox cb("Enable");
    EXPECT_FALSE(cb.checked());
}

TEST(Checkbox, SpaceKeyToggles) {
    Checkbox cb("Enable");
    EXPECT_TRUE(cb.on_key(KeyEvent{Key::Char, U' '}));
    EXPECT_TRUE(cb.checked());
    EXPECT_TRUE(cb.on_key(KeyEvent{Key::Char, U' '}));
    EXPECT_FALSE(cb.checked());
}

TEST(Checkbox, MouseClickToggles) {
    Checkbox cb("Enable");
    cb.on_mouse(MouseEvent{0, 0, MouseEvent::Button::Left, MouseEvent::Action::Press, false, false, false});
    EXPECT_TRUE(cb.checked());
}

TEST(Checkbox, OnChangeFiresWithNewValue) {
    Checkbox cb("Enable");
    bool last = false;
    int calls = 0;
    cb.set_on_change([&](bool v) { last = v; ++calls; });
    cb.set_checked(true);
    EXPECT_EQ(calls, 1);
    EXPECT_TRUE(last);
    cb.set_checked(true); // pas de changement réel : pas de notification
    EXPECT_EQ(calls, 1);
}

TEST(CheckboxList, DownArrowMovesSelection) {
    CheckboxList list;
    list.set_items({{"A", false}, {"B", false}, {"C", false}});
    list.on_key(KeyEvent{Key::Down});
    list.on_key(KeyEvent{Key::Enter});
    EXPECT_TRUE(list.items()[1].checked);
    EXPECT_FALSE(list.items()[0].checked);
}

TEST(CheckboxList, UpArrowAtTopIsNoOp) {
    CheckboxList list;
    list.set_items({{"A", false}, {"B", false}});
    list.on_key(KeyEvent{Key::Up});
    list.on_key(KeyEvent{Key::Enter});
    EXPECT_TRUE(list.items()[0].checked);
}

TEST(CheckboxList, EmptyListIsNotFocusable) {
    CheckboxList list;
    EXPECT_FALSE(list.focusable());
}

TEST(CheckboxList, OnChangeReportsIndexAndState) {
    CheckboxList list;
    list.set_items({{"A", false}});
    size_t last_index = 999;
    bool last_state = false;
    list.set_on_change([&](size_t i, bool c) { last_index = i; last_state = c; });
    list.on_key(KeyEvent{Key::Enter});
    EXPECT_EQ(last_index, 0u);
    EXPECT_TRUE(last_state);
}

TEST(RadioSet, NavigationThenSelectSetsSelectedIndex) {
    RadioSet rs;
    rs.set_options({"One", "Two", "Three"});
    rs.on_key(KeyEvent{Key::Down});
    rs.on_key(KeyEvent{Key::Down});
    rs.on_key(KeyEvent{Key::Enter});
    EXPECT_EQ(rs.selected_index(), 2u);
}

TEST(RadioSet, CursorMovementAloneDoesNotChangeSelection) {
    RadioSet rs;
    rs.set_options({"One", "Two", "Three"});
    rs.on_key(KeyEvent{Key::Down}); // ne fait que déplacer le curseur, pas la sélection
    EXPECT_EQ(rs.selected_index(), 0u);
}

TEST(RadioSet, EmptyOptionsIsNotFocusable) {
    RadioSet rs;
    EXPECT_FALSE(rs.focusable());
}

TEST(ToggleSwitch, StartsOff) {
    ToggleSwitch ts;
    EXPECT_FALSE(ts.checked());
}

TEST(ToggleSwitch, EnterTogglesState) {
    ToggleSwitch ts;
    ts.on_key(KeyEvent{Key::Enter});
    EXPECT_TRUE(ts.checked());
}

TEST(ToggleSwitch, LeftRightArrowsToggle) {
    ToggleSwitch ts;
    ts.on_key(KeyEvent{Key::Right});
    EXPECT_TRUE(ts.checked());
    ts.on_key(KeyEvent{Key::Left});
    EXPECT_FALSE(ts.checked());
}

TEST(Dropdown, StartsClosed) {
    Dropdown dd;
    dd.set_options({"Red", "Green", "Blue"});
    EXPECT_FALSE(dd.is_open());
}

TEST(Dropdown, EnterOpensAndTogglesClosed) {
    Dropdown dd;
    dd.set_options({"Red", "Green", "Blue"});
    dd.on_key(KeyEvent{Key::Enter});
    EXPECT_TRUE(dd.is_open());
    dd.on_key(KeyEvent{Key::Enter});
    EXPECT_FALSE(dd.is_open());
}

TEST(Dropdown, EscapeClosesWhenOpen) {
    Dropdown dd;
    dd.set_options({"Red", "Green", "Blue"});
    dd.on_key(KeyEvent{Key::Enter});
    ASSERT_TRUE(dd.is_open());
    dd.on_key(KeyEvent{Key::Escape});
    EXPECT_FALSE(dd.is_open());
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
