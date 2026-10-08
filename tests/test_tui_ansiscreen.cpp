#include <gtest/gtest.h>
#include <tui/terminal/AnsiScreen.hpp>

using namespace tui;

TEST(AnsiScreen, PlainTextAdvancesCursorAndWritesCells) {
    AnsiScreen s(10, 3);
    s.feed("hi");
    EXPECT_EQ(s.grid().at(0, 0).codepoint, U'h');
    EXPECT_EQ(s.grid().at(1, 0).codepoint, U'i');
    EXPECT_EQ(s.cursor_col(), 2);
    EXPECT_EQ(s.cursor_row(), 0);
}

TEST(AnsiScreen, NewlineMovesToNextRowAndCarriageReturnResetsColumn) {
    AnsiScreen s(10, 3);
    s.feed("ab\r\ncd");
    EXPECT_EQ(s.cursor_row(), 1);
    EXPECT_EQ(s.grid().at(0, 1).codepoint, U'c');
    EXPECT_EQ(s.grid().at(1, 1).codepoint, U'd');
}

TEST(AnsiScreen, LineWrapsAtRightEdge) {
    AnsiScreen s(3, 3);
    s.feed("abcd");
    EXPECT_EQ(s.grid().at(2, 0).codepoint, U'c');
    EXPECT_EQ(s.grid().at(0, 1).codepoint, U'd');
    EXPECT_EQ(s.cursor_row(), 1);
    EXPECT_EQ(s.cursor_col(), 1);
}

TEST(AnsiScreen, ScrollsWhenPastBottomRow) {
    AnsiScreen s(5, 2);
    s.feed("one\r\ntwo\r\nthree");
    // "one" doit avoir defile hors ecran, "two" est maintenant en haut.
    EXPECT_EQ(s.grid().at(0, 0).codepoint, U't'); // "two"
    EXPECT_EQ(s.grid().at(0, 1).codepoint, U't'); // "three"
}

TEST(AnsiScreen, CursorPositionCsiMovesCursor) {
    AnsiScreen s(20, 10);
    s.feed("\x1b[3;5Hx");
    EXPECT_EQ(s.cursor_row(), 2); // 1-indexe -> 0-indexe
    EXPECT_EQ(s.grid().at(4, 2).codepoint, U'x');
}

TEST(AnsiScreen, EraseInLineClearsFromCursor) {
    AnsiScreen s(10, 2);
    s.feed("abcdef\r\x1b[3C\x1b[K"); // curseur en colonne 3, efface jusqu'a la fin
    EXPECT_EQ(s.grid().at(0, 0).codepoint, U'a');
    EXPECT_EQ(s.grid().at(2, 0).codepoint, U'c');
    EXPECT_EQ(s.grid().at(3, 0).codepoint, U' ');
}

TEST(AnsiScreen, EraseDisplayModeTwoClearsEverything) {
    AnsiScreen s(5, 2);
    s.feed("abcde\r\ndef\x1b[2J");
    for (int y = 0; y < s.rows(); ++y)
        for (int x = 0; x < s.cols(); ++x) EXPECT_EQ(s.grid().at(x, y).codepoint, U' ');
}

TEST(AnsiScreen, SgrSetsForegroundColor) {
    AnsiScreen s(10, 2);
    s.feed("\x1b[31mred");
    EXPECT_EQ(s.grid().at(0, 0).fg, Color(205, 0, 0));
}

TEST(AnsiScreen, SgrResetClearsStyleAndColor) {
    AnsiScreen s(10, 2);
    s.feed("\x1b[1;31ma\x1b[0mb");
    EXPECT_TRUE(has_style(s.grid().at(0, 0).style, TextStyle::Bold));
    EXPECT_TRUE(s.grid().at(1, 0).fg.is_default);
    EXPECT_FALSE(has_style(s.grid().at(1, 0).style, TextStyle::Bold));
}

TEST(AnsiScreen, TruecolorSgrIsExact) {
    AnsiScreen s(10, 2);
    s.feed("\x1b[38;2;10;20;30mx");
    EXPECT_EQ(s.grid().at(0, 0).fg, Color(10, 20, 30));
}

TEST(AnsiScreen, IncompleteEscapeSequenceAcrossFeedCallsStillParses) {
    AnsiScreen s(10, 2);
    s.feed("\x1b[3");
    s.feed("1mred");
    EXPECT_EQ(s.grid().at(0, 0).fg, Color(205, 0, 0));
}

TEST(AnsiScreen, ResizePreservesGridDimensions) {
    AnsiScreen s(10, 5);
    s.resize(20, 8);
    EXPECT_EQ(s.cols(), 20);
    EXPECT_EQ(s.rows(), 8);
    EXPECT_EQ(s.grid().width(), 20);
    EXPECT_EQ(s.grid().height(), 8);
}
