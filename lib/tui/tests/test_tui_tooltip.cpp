#include <gtest/gtest.h>
#include <tui/widgets/Tooltip.hpp>

using namespace tui;

namespace {
constexpr Rect kScreen{0, 0, 40, 20};
} // namespace

TEST(Tooltip, HiddenByDefault) {
    Tooltip tip;
    EXPECT_FALSE(tip.visible());
}

TEST(Tooltip, ShowHideTogglesVisibility) {
    Tooltip tip;
    tip.show();
    EXPECT_TRUE(tip.visible());
    tip.hide();
    EXPECT_FALSE(tip.visible());
}

TEST(Tooltip, ClampedAtTopLeftCorner) {
    Tooltip tip;
    tip.set_text("A reasonably long tooltip text");
    tip.set_anchor({0, 0});
    tip.arrange(kScreen);
    EXPECT_GE(tip.bounds().x, kScreen.x);
    EXPECT_GE(tip.bounds().y, kScreen.y);
    EXPECT_LE(tip.bounds().right(), kScreen.right());
    EXPECT_LE(tip.bounds().bottom(), kScreen.bottom());
}

TEST(Tooltip, ClampedAtBottomRightCorner) {
    Tooltip tip;
    tip.set_text("A reasonably long tooltip text");
    tip.set_anchor({39, 19});
    tip.arrange(kScreen);
    EXPECT_LE(tip.bounds().right(), kScreen.right());
    EXPECT_LE(tip.bounds().bottom(), kScreen.bottom());
}

TEST(Tooltip, ClampedAtTopRightCorner) {
    Tooltip tip;
    tip.set_text("A reasonably long tooltip text");
    tip.set_anchor({39, 0});
    tip.arrange(kScreen);
    EXPECT_LE(tip.bounds().right(), kScreen.right());
    EXPECT_GE(tip.bounds().y, kScreen.y);
}

TEST(Tooltip, ClampedAtBottomLeftCorner) {
    Tooltip tip;
    tip.set_text("A reasonably long tooltip text");
    tip.set_anchor({0, 19});
    tip.arrange(kScreen);
    EXPECT_GE(tip.bounds().x, kScreen.x);
    EXPECT_LE(tip.bounds().bottom(), kScreen.bottom());
}

TEST(Tooltip, ShortTextSizesToContent) {
    Tooltip tip;
    tip.set_text("Hi");
    tip.set_anchor({5, 5});
    tip.arrange(kScreen);
    EXPECT_LE(tip.bounds().width, 10);
}
