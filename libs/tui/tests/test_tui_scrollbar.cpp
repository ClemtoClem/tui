#include <gtest/gtest.h>
#include <tui/widget/Scrollbar.hpp>

using namespace tui;

TEST(DrawVerticalScrollbar, DrawsTrackWithoutThumbWhenContentFitsViewport) {
    Buffer buf(3, 10);
    draw_vertical_scrollbar(buf, Rect{0, 0, 1, 10}, /*content*/ 5, /*viewport*/ 10, /*pos*/ 0, Color::Red());
    for (int y = 0; y < 10; ++y) EXPECT_EQ(buf.at(0, y).codepoint, U'│');
}

TEST(DrawVerticalScrollbar, DrawsThumbProportionalToViewport) {
    Buffer buf(3, 10);
    draw_vertical_scrollbar(buf, Rect{0, 0, 1, 10}, /*content*/ 100, /*viewport*/ 10, /*pos*/ 0, Color::Red());
    int filled = 0;
    for (int y = 0; y < 10; ++y) {
        if (buf.at(0, y).codepoint == U'█') ++filled;
    }
    EXPECT_GT(filled, 0);
    EXPECT_LT(filled, 10);
}

TEST(DrawVerticalScrollbar, ThumbMovesToBottomWhenPositionAtMax) {
    Buffer buf(3, 10);
    draw_vertical_scrollbar(buf, Rect{0, 0, 1, 10}, 100, 10, 90, Color::Red());
    EXPECT_EQ(buf.at(0, 9).codepoint, U'█');
    EXPECT_EQ(buf.at(0, 0).codepoint, U'│');
}

TEST(ScrollbarHitToPosition, TopOfTrackMapsToZero) {
    Rect track{0, 0, 1, 10};
    EXPECT_EQ(scrollbar_hit_to_position(track, 100, 10, 0), 0);
}

TEST(ScrollbarHitToPosition, BottomOfTrackMapsToMaxScroll) {
    Rect track{0, 0, 1, 10};
    EXPECT_EQ(scrollbar_hit_to_position(track, 100, 10, 9), 90);
}

TEST(ScrollbarHitToPosition, NoScrollRangeAlwaysReturnsZero) {
    Rect track{0, 0, 1, 10};
    EXPECT_EQ(scrollbar_hit_to_position(track, 5, 10, 5), 0);
}

TEST(Scrollbar, MouseDragUpdatesPositionAndInvokesCallback) {
    Scrollbar sb;
    sb.set_content_size(100);
    sb.set_viewport_size(10);
    sb.arrange(Rect{0, 0, 1, 10});

    int last = -1;
    sb.set_on_scroll([&](int p) { last = p; });
    sb.on_mouse(MouseEvent{0, 9, MouseEvent::Button::Left, MouseEvent::Action::Press, false, false, false});
    EXPECT_EQ(sb.position(), 90);
    EXPECT_EQ(last, 90);
}

TEST(Scrollbar, WheelScrollsByThreeAndClamps) {
    Scrollbar sb;
    sb.set_content_size(10);
    sb.set_viewport_size(5);
    sb.arrange(Rect{0, 0, 1, 5});
    sb.on_mouse(MouseEvent{0, 0, MouseEvent::Button::WheelDown, MouseEvent::Action::Press, false, false, false});
    EXPECT_EQ(sb.position(), 3);
    sb.on_mouse(MouseEvent{0, 0, MouseEvent::Button::WheelDown, MouseEvent::Action::Press, false, false, false});
    EXPECT_EQ(sb.position(), 5); // borne au max (content-viewport)
}
