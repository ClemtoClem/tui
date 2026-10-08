#include <gtest/gtest.h>
#include <tui/widgets/Breadcrumb.hpp>

using namespace tui;

TEST(Breadcrumb, ClickWithinSegmentActivatesIt) {
    Breadcrumb bc;
    bc.set_segments({"Home", "Documents", "file.txt"});
    bc.arrange(Rect{0, 0, 40, 1});

    size_t activated = 999;
    bc.set_on_activate([&](size_t i) { activated = i; });

    // "Home" occupe les colonnes 0-3, puis " › " (3 cols), "Documents" démarre à 7.
    bc.on_mouse(MouseEvent{8, 0, MouseEvent::Button::Left, MouseEvent::Action::Press, false, false, false});
    EXPECT_EQ(activated, 1u);
}

TEST(Breadcrumb, ClickOnFirstSegment) {
    Breadcrumb bc;
    bc.set_segments({"Home", "Documents"});
    bc.arrange(Rect{0, 0, 40, 1});
    size_t activated = 999;
    bc.set_on_activate([&](size_t i) { activated = i; });
    bc.on_mouse(MouseEvent{0, 0, MouseEvent::Button::Left, MouseEvent::Action::Press, false, false, false});
    EXPECT_EQ(activated, 0u);
}

TEST(Breadcrumb, NonLeftClickIsIgnored) {
    Breadcrumb bc;
    bc.set_segments({"Home", "Documents"});
    bc.arrange(Rect{0, 0, 40, 1});
    EXPECT_FALSE(bc.on_mouse(MouseEvent{0, 0, MouseEvent::Button::Right, MouseEvent::Action::Press, false, false, false}));
}

TEST(Breadcrumb, EmptySegmentsDoesNotCrash) {
    Breadcrumb bc;
    bc.set_segments({});
    bc.arrange(Rect{0, 0, 40, 1});
    Buffer buf(40, 1);
    bc.paint(buf);
}
