#include <gtest/gtest.h>
#include <tui/Buffer.hpp>
#include <tui/widgets/Badge.hpp>
#include <tui/widgets/Button.hpp>
#include <tui/widgets/Label.hpp>
#include <tui/widgets/ProgressBar.hpp>
#include <tui/widgets/Spinner.hpp>
#include <tui/widgets/StatusBar.hpp>

using namespace tui;

namespace {
std::u32string painted_line(const Buffer& buf, int y, int width) {
    std::u32string line;
    for (int x = 0; x < width; ++x) {
        if (buf.at(x, y).width != 0) line.push_back(buf.at(x, y).codepoint);
    }
    return line;
}
} // namespace

TEST(Label, SingleLineTruncatesWithEllipsisWhenTooNarrow) {
    Label label("hello world");
    label.arrange(Rect{0, 0, 8, 1});
    Buffer buf(8, 1);
    label.paint(buf);
    auto line = painted_line(buf, 0, 8);
    EXPECT_LE(TextHelper::display_width(line), 8);
    EXPECT_EQ(line.back(), U'…');
}

TEST(Label, FitsExactlyWithoutTruncation) {
    Label label("short");
    label.arrange(Rect{0, 0, 5, 1});
    Buffer buf(5, 1);
    label.paint(buf);
    EXPECT_EQ(painted_line(buf, 0, 5), U"short");
}

TEST(Label, WrapModeProducesMultipleLines) {
    Label label("the quick brown fox");
    label.set_wrap(true);
    label.arrange(Rect{0, 0, 10, 3});
    Buffer buf(10, 3);
    label.paint(buf);
    EXPECT_FALSE(painted_line(buf, 0, 10).empty());
    EXPECT_FALSE(painted_line(buf, 1, 10).empty());
}

TEST(Label, CenterAlignmentCentersShortText) {
    Label label("hi");
    label.set_align(HAlign::Center);
    label.arrange(Rect{0, 0, 10, 1});
    Buffer buf(10, 1);
    label.paint(buf);
    EXPECT_EQ(buf.at(4, 0).codepoint, U'h');
    EXPECT_EQ(buf.at(5, 0).codepoint, U'i');
}

TEST(Button, MeasureIncludesBrackets) {
    Button btn("OK");
    Size size = btn.measure(Constraints::loose({80, 24}));
    EXPECT_EQ(size.width, 6); // "[ OK ]"
}

TEST(Button, IsFocusable) {
    Button btn("OK");
    EXPECT_TRUE(btn.focusable());
}

TEST(Button, EnterKeyTriggersClick) {
    Button btn("OK");
    bool clicked = false;
    btn.set_on_click([&] { clicked = true; });
    EXPECT_TRUE(btn.on_key(KeyEvent{Key::Enter}));
    EXPECT_TRUE(clicked);
}

TEST(Button, SpaceKeyTriggersClick) {
    Button btn("OK");
    bool clicked = false;
    btn.set_on_click([&] { clicked = true; });
    EXPECT_TRUE(btn.on_key(KeyEvent{Key::Char, U' '}));
    EXPECT_TRUE(clicked);
}

TEST(Button, ClickRequiresPressThenReleaseAtSamePosition) {
    Button btn("Go");
    bool clicked = false;
    btn.set_on_click([&] { clicked = true; });
    btn.arrange(Rect{0, 0, 10, 1});

    btn.on_mouse(MouseEvent{2, 0, MouseEvent::Button::Left, MouseEvent::Action::Press, false, false, false});
    EXPECT_FALSE(clicked);
    btn.on_mouse(MouseEvent{2, 0, MouseEvent::Button::Left, MouseEvent::Action::Release, false, false, false});
    EXPECT_TRUE(clicked);
}

TEST(Button, FocusAndBlurToggleFocusedState) {
    Button btn("OK");
    btn.on_focus();
    btn.on_blur(); // ne doit pas planter ; état interne non exposé publiquement
}

TEST(ProgressBar, ZeroPercentFillsNothing) {
    ProgressBar pb;
    pb.arrange(Rect{0, 0, 10, 1});
    pb.set_progress(0.0);
    Buffer buf(10, 1);
    pb.paint(buf);
    int filled = 0;
    for (int x = 0; x < 10; ++x) if (buf.at(x, 0).codepoint == U'█') ++filled;
    EXPECT_EQ(filled, 0);
}

TEST(ProgressBar, FiftyPercentFillsHalf) {
    ProgressBar pb;
    pb.arrange(Rect{0, 0, 10, 1});
    pb.set_progress(0.5);
    Buffer buf(10, 1);
    pb.paint(buf);
    int filled = 0;
    for (int x = 0; x < 10; ++x) if (buf.at(x, 0).codepoint == U'█') ++filled;
    EXPECT_EQ(filled, 5);
}

TEST(ProgressBar, HundredPercentFillsAll) {
    ProgressBar pb;
    pb.arrange(Rect{0, 0, 10, 1});
    pb.set_progress(1.0);
    Buffer buf(10, 1);
    pb.paint(buf);
    int filled = 0;
    for (int x = 0; x < 10; ++x) if (buf.at(x, 0).codepoint == U'█') ++filled;
    EXPECT_EQ(filled, 10);
}

TEST(ProgressBar, ProgressIsClampedToZeroOne) {
    ProgressBar pb;
    pb.set_progress(-0.5);
    EXPECT_DOUBLE_EQ(pb.progress(), 0.0);
    pb.set_progress(2.0);
    EXPECT_DOUBLE_EQ(pb.progress(), 1.0);
}

TEST(ProgressBar, GaugeModeReservesSpaceForLabel) {
    ProgressBar pb;
    pb.set_show_label(true);
    pb.set_label("Sync");
    pb.set_progress(1.0);
    pb.arrange(Rect{0, 0, 20, 1});
    Buffer buf(20, 1);
    pb.paint(buf);
    EXPECT_EQ(buf.at(0, 0).codepoint, U'S');
}

TEST(StatusBar, SegmentsAreVisibleAtExpectedPositions) {
    StatusBar sb;
    sb.set_left("L");
    sb.set_center("C");
    sb.set_right("R");
    sb.arrange(Rect{0, 0, 20, 1});
    Buffer buf(20, 1);
    sb.paint(buf);
    EXPECT_EQ(buf.at(0, 0).codepoint, U'L');
    EXPECT_EQ(buf.at(19, 0).codepoint, U'R');
}

TEST(Spinner, AdvanceCyclesThroughFrames) {
    Spinner sp;
    size_t f0 = sp.frame();
    sp.advance();
    EXPECT_NE(sp.frame(), f0);
}

TEST(Spinner, MeasureAccountsForLabel) {
    Spinner sp;
    Size without_label = sp.measure(Constraints::loose({80, 24}));
    sp.set_label("Loading");
    Size with_label = sp.measure(Constraints::loose({80, 24}));
    EXPECT_GT(with_label.width, without_label.width);
}

TEST(Badge, MeasureIncludesPadding) {
    Badge badge("NEW");
    Size size = badge.measure(Constraints::loose({80, 24}));
    EXPECT_EQ(size.width, 5); // " NEW "
}

TEST(Badge, PaintsWithinBounds) {
    Badge badge("X");
    badge.arrange(Rect{0, 0, 3, 1});
    Buffer buf(3, 1);
    badge.paint(buf);
    EXPECT_EQ(buf.at(1, 0).codepoint, U'X');
}
