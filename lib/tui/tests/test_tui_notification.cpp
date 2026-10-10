#include <gtest/gtest.h>
#include <tui/widgets/Notification.hpp>

using namespace tui;

TEST(Notification, HiddenBeforeShow) {
    Notification n;
    EXPECT_FALSE(n.visible());
}

TEST(Notification, ShowMakesItVisible) {
    Notification n;
    n.show("Saved!");
    EXPECT_TRUE(n.visible());
}

TEST(Notification, AutoDismissesAfterDurationElapses) {
    Notification n;
    n.show("Saved!", std::chrono::milliseconds(100));
    n.tick(std::chrono::milliseconds(50));
    EXPECT_TRUE(n.visible());
    n.tick(std::chrono::milliseconds(60)); // cumul 110ms > 100ms
    EXPECT_FALSE(n.visible());
}

TEST(Notification, ManualDismissClosesImmediately) {
    Notification n;
    n.show("Saved!", std::chrono::milliseconds(10000));
    n.dismiss();
    EXPECT_FALSE(n.visible());
}

TEST(Notification, TickAfterDismissIsNoOp) {
    Notification n;
    n.show("Saved!", std::chrono::milliseconds(100));
    n.dismiss();
    n.tick(std::chrono::milliseconds(1000)); // ne doit pas re-notifier ni planter
    EXPECT_FALSE(n.visible());
}

TEST(Notification, ReshowResetsElapsedTime) {
    Notification n;
    n.show("First", std::chrono::milliseconds(100));
    n.tick(std::chrono::milliseconds(90));
    n.show("Second", std::chrono::milliseconds(100)); // relance le compte à zéro
    n.tick(std::chrono::milliseconds(50));
    EXPECT_TRUE(n.visible());
}

TEST(Notification, ArrangePositionsWithinScreenBounds) {
    Notification n;
    n.show("A rather long notification message");
    Rect screen{0, 0, 40, 20};
    n.arrange(screen);
    EXPECT_GE(n.bounds().x, screen.x);
    EXPECT_LE(n.bounds().right(), screen.right());
    EXPECT_LE(n.bounds().bottom(), screen.bottom());
}
