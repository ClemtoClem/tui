#include <gtest/gtest.h>
#include <tui/widgets/Input.hpp>
#include <tui/widgets/NumberInput.hpp>

using namespace tui;

TEST(Input, TypingAppendsAtCursor) {
    Input input;
    input.on_key(KeyEvent{Key::Char, U'a'});
    input.on_key(KeyEvent{Key::Char, U'b'});
    input.on_key(KeyEvent{Key::Char, U'c'});
    EXPECT_EQ(input.text(), "abc");
}

TEST(Input, BackspaceRemovesBeforeCursor) {
    Input input;
    input.set_text("abc");
    input.on_key(KeyEvent{Key::End});
    input.on_key(KeyEvent{Key::Backspace});
    EXPECT_EQ(input.text(), "ab");
}

TEST(Input, DeleteRemovesAfterCursor) {
    Input input;
    input.set_text("abc");
    input.on_key(KeyEvent{Key::Home});
    input.on_key(KeyEvent{Key::Delete});
    EXPECT_EQ(input.text(), "bc");
}

TEST(Input, HomeEndMoveCursorToEdges) {
    Input input;
    input.set_text("abc");
    input.on_key(KeyEvent{Key::Home});
    input.on_key(KeyEvent{Key::Char, U'X'});
    EXPECT_EQ(input.text(), "Xabc");
    input.on_key(KeyEvent{Key::End});
    input.on_key(KeyEvent{Key::Char, U'Y'});
    EXPECT_EQ(input.text(), "XabcY");
}

TEST(Input, BackspaceAtStartIsNoOp) {
    Input input;
    input.set_text("abc");
    input.on_key(KeyEvent{Key::Home});
    input.on_key(KeyEvent{Key::Backspace});
    EXPECT_EQ(input.text(), "abc");
}

TEST(Input, ValidatorRejectsDisallowedCharacters) {
    Input input;
    input.set_validator([](char32_t c) { return c >= U'0' && c <= U'9'; });
    input.on_key(KeyEvent{Key::Char, U'a'});
    input.on_key(KeyEvent{Key::Char, U'5'});
    EXPECT_EQ(input.text(), "5");
}

TEST(Input, OnChangeFiresOnEdit) {
    Input input;
    int calls = 0;
    std::string last;
    input.set_on_change([&](const std::string& t) { ++calls; last = t; });
    input.on_key(KeyEvent{Key::Char, U'x'});
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(last, "x");
}

TEST(Input, OnSubmitFiresOnEnter) {
    Input input;
    input.set_text("hello");
    bool submitted = false;
    input.set_on_submit([&](const std::string& t) { submitted = (t == "hello"); });
    input.on_key(KeyEvent{Key::Enter});
    EXPECT_TRUE(submitted);
}

TEST(Input, IsFocusable) {
    Input input;
    EXPECT_TRUE(input.focusable());
}

TEST(NumberInput, ArrowsSpinByStep) {
    NumberInput ni;
    ni.set_value(5);
    ni.set_step(2);
    ni.on_key(KeyEvent{Key::Up});
    EXPECT_DOUBLE_EQ(ni.value(), 7.0);
    ni.on_key(KeyEvent{Key::Down});
    EXPECT_DOUBLE_EQ(ni.value(), 5.0);
}

TEST(NumberInput, ValueIsClampedToRange) {
    NumberInput ni;
    ni.set_range(0, 10);
    ni.set_value(-5);
    EXPECT_DOUBLE_EQ(ni.value(), 0.0);
    ni.set_value(50);
    EXPECT_DOUBLE_EQ(ni.value(), 10.0);
}

TEST(NumberInput, WheelAdjustsValue) {
    NumberInput ni;
    ni.set_step(1);
    ni.on_mouse(MouseEvent{0, 0, MouseEvent::Button::WheelUp, MouseEvent::Action::Press, false, false, false});
    EXPECT_DOUBLE_EQ(ni.value(), 1.0);
    ni.on_mouse(MouseEvent{0, 0, MouseEvent::Button::WheelDown, MouseEvent::Action::Press, false, false, false});
    EXPECT_DOUBLE_EQ(ni.value(), 0.0);
}

TEST(NumberInput, OnChangeFiresWhenValueActuallyChanges) {
    NumberInput ni;
    int calls = 0;
    ni.set_on_change([&](double) { ++calls; });
    ni.set_value(5);
    EXPECT_EQ(calls, 1);
    ni.set_value(5); // valeur identique : pas de notification
    EXPECT_EQ(calls, 1);
}
