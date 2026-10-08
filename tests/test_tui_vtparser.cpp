#include <gtest/gtest.h>
#include <tui/terminal/VTParser.hpp>

using namespace tui;

TEST(VTParser, PlainAsciiChar) {
    VTParser p;
    p.feed("a");
    auto evs = p.pump();
    ASSERT_EQ(evs.size(), 1u);
    auto ke = std::get<KeyEvent>(evs[0]);
    EXPECT_EQ(ke.key, Key::Char);
    EXPECT_EQ(ke.codepoint, U'a');
}

TEST(VTParser, ArrowKeys) {
    VTParser p;
    p.feed("\x1b[A\x1b[B\x1b[C\x1b[D");
    auto evs = p.pump();
    ASSERT_EQ(evs.size(), 4u);
    EXPECT_EQ(std::get<KeyEvent>(evs[0]).key, Key::Up);
    EXPECT_EQ(std::get<KeyEvent>(evs[1]).key, Key::Down);
    EXPECT_EQ(std::get<KeyEvent>(evs[2]).key, Key::Right);
    EXPECT_EQ(std::get<KeyEvent>(evs[3]).key, Key::Left);
}

TEST(VTParser, SequenceSplitAcrossFeeds) {
    VTParser p;
    p.feed("\x1b[");
    EXPECT_TRUE(p.pump().empty());
    EXPECT_TRUE(p.has_pending_escape());
    p.feed("A");
    auto evs = p.pump();
    ASSERT_EQ(evs.size(), 1u);
    EXPECT_EQ(std::get<KeyEvent>(evs[0]).key, Key::Up);
}

TEST(VTParser, LoneEscapeFlushedAsEscapeKey) {
    VTParser p;
    p.feed("\x1b");
    EXPECT_TRUE(p.pump().empty());
    ASSERT_TRUE(p.has_pending_escape());
    auto flushed = p.flush_pending_escape();
    ASSERT_TRUE(flushed.has_value());
    EXPECT_EQ(std::get<KeyEvent>(*flushed).key, Key::Escape);
    EXPECT_FALSE(p.has_pending_escape());
}

TEST(VTParser, TildeFormNavigationKeys) {
    VTParser p;
    p.feed("\x1b[3~\x1b[1~\x1b[4~\x1b[5~\x1b[6~");
    auto evs = p.pump();
    ASSERT_EQ(evs.size(), 5u);
    EXPECT_EQ(std::get<KeyEvent>(evs[0]).key, Key::Delete);
    EXPECT_EQ(std::get<KeyEvent>(evs[1]).key, Key::Home);
    EXPECT_EQ(std::get<KeyEvent>(evs[2]).key, Key::End);
    EXPECT_EQ(std::get<KeyEvent>(evs[3]).key, Key::PageUp);
    EXPECT_EQ(std::get<KeyEvent>(evs[4]).key, Key::PageDown);
}

TEST(VTParser, ModifierBearingSequence) {
    VTParser p;
    p.feed("\x1b[1;5C"); // Ctrl+Right
    auto evs = p.pump();
    ASSERT_EQ(evs.size(), 1u);
    auto ke = std::get<KeyEvent>(evs[0]);
    EXPECT_EQ(ke.key, Key::Right);
    EXPECT_TRUE(ke.ctrl);
    EXPECT_FALSE(ke.shift);
    EXPECT_FALSE(ke.alt);
}

TEST(VTParser, CtrlLetter) {
    VTParser p;
    p.feed("\x01"); // Ctrl+A
    auto evs = p.pump();
    ASSERT_EQ(evs.size(), 1u);
    auto ke = std::get<KeyEvent>(evs[0]);
    EXPECT_EQ(ke.key, Key::Char);
    EXPECT_EQ(ke.codepoint, U'a');
    EXPECT_TRUE(ke.ctrl);
}

TEST(VTParser, EnterTabBackspace) {
    VTParser p;
    p.feed("\r\t\x7f");
    auto evs = p.pump();
    ASSERT_EQ(evs.size(), 3u);
    EXPECT_EQ(std::get<KeyEvent>(evs[0]).key, Key::Enter);
    EXPECT_EQ(std::get<KeyEvent>(evs[1]).key, Key::Tab);
    EXPECT_EQ(std::get<KeyEvent>(evs[2]).key, Key::Backspace);
}

TEST(VTParser, SgrMousePress) {
    VTParser p;
    p.feed("\x1b[<0;10;5M");
    auto evs = p.pump();
    ASSERT_EQ(evs.size(), 1u);
    auto me = std::get<MouseEvent>(evs[0]);
    EXPECT_EQ(me.x, 9);
    EXPECT_EQ(me.y, 4);
    EXPECT_EQ(me.button, MouseEvent::Button::Left);
    EXPECT_EQ(me.action, MouseEvent::Action::Press);
}

TEST(VTParser, SgrMouseRelease) {
    VTParser p;
    p.feed("\x1b[<0;10;5m");
    auto evs = p.pump();
    ASSERT_EQ(evs.size(), 1u);
    EXPECT_EQ(std::get<MouseEvent>(evs[0]).action, MouseEvent::Action::Release);
}

TEST(VTParser, MouseWheelUp) {
    VTParser p;
    p.feed("\x1b[<64;1;1M");
    auto evs = p.pump();
    ASSERT_EQ(evs.size(), 1u);
    EXPECT_EQ(std::get<MouseEvent>(evs[0]).button, MouseEvent::Button::WheelUp);
}

TEST(VTParser, BracketedPaste) {
    VTParser p;
    p.feed("\x1b[200~hello world\x1b[201~");
    auto evs = p.pump();
    ASSERT_EQ(evs.size(), 1u);
    EXPECT_EQ(std::get<PasteEvent>(evs[0]).text, "hello world");
}

TEST(VTParser, BracketedPasteSplitAcrossFeeds) {
    VTParser p;
    p.feed("\x1b[200~hello ");
    EXPECT_TRUE(p.pump().empty());
    p.feed("world\x1b[201~");
    auto evs = p.pump();
    ASSERT_EQ(evs.size(), 1u);
    EXPECT_EQ(std::get<PasteEvent>(evs[0]).text, "hello world");
}

TEST(VTParser, FunctionKeysSs3AndTilde) {
    VTParser p;
    p.feed("\x1bOP\x1b[15~");
    auto evs = p.pump();
    ASSERT_EQ(evs.size(), 2u);
    EXPECT_EQ(std::get<KeyEvent>(evs[0]).key, Key::F1);
    EXPECT_EQ(std::get<KeyEvent>(evs[1]).key, Key::F5);
}

TEST(VTParser, AltChar) {
    VTParser p;
    p.feed("\x1bx");
    auto evs = p.pump();
    ASSERT_EQ(evs.size(), 1u);
    auto ke = std::get<KeyEvent>(evs[0]);
    EXPECT_EQ(ke.key, Key::Char);
    EXPECT_EQ(ke.codepoint, U'x');
    EXPECT_TRUE(ke.alt);
}

TEST(VTParser, Utf8MultibyteChar) {
    VTParser p;
    p.feed("\xC3\xA9"); // é
    auto evs = p.pump();
    ASSERT_EQ(evs.size(), 1u);
    EXPECT_EQ(std::get<KeyEvent>(evs[0]).codepoint, char32_t(0xE9));
}

TEST(VTParser, Utf8SplitAcrossFeeds) {
    VTParser p;
    p.feed("\xC3");
    EXPECT_TRUE(p.pump().empty());
    p.feed("\xA9");
    auto evs = p.pump();
    ASSERT_EQ(evs.size(), 1u);
    EXPECT_EQ(std::get<KeyEvent>(evs[0]).codepoint, char32_t(0xE9));
}

TEST(VTParser, EmptyFeedProducesNoEvents) {
    VTParser p;
    p.feed("");
    EXPECT_TRUE(p.pump().empty());
    EXPECT_FALSE(p.has_pending_escape());
}
