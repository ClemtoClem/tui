#include <gtest/gtest.h>
#include <tui/core/Theme.hpp>

using namespace tui;

TEST(Theme, DarkResolvesAllRoles) {
    const Theme& t = Theme::dark();
    EXPECT_FALSE(t.resolve(ColorRole::Background).is_default);
    EXPECT_FALSE(t.resolve(ColorRole::Foreground).is_default);
    EXPECT_FALSE(t.resolve(ColorRole::Border).is_default);
    EXPECT_FALSE(t.resolve(ColorRole::BorderFocused).is_default);
    EXPECT_FALSE(t.resolve(ColorRole::Selection).is_default);
    EXPECT_FALSE(t.resolve(ColorRole::Accent).is_default);
    EXPECT_FALSE(t.resolve(ColorRole::Error).is_default);
    EXPECT_FALSE(t.resolve(ColorRole::Warning).is_default);
    EXPECT_FALSE(t.resolve(ColorRole::Success).is_default);
    EXPECT_FALSE(t.resolve(ColorRole::Info).is_default);
    EXPECT_FALSE(t.resolve(ColorRole::Disabled).is_default);
    EXPECT_FALSE(t.resolve(ColorRole::Muted).is_default);
}

TEST(Theme, PlanorResolvesAllRolesAndDiffersFromDark) {
    const Theme& planor = Theme::planor();
    const Theme& dark = Theme::dark();
    EXPECT_FALSE(planor.resolve(ColorRole::Background).is_default);
    EXPECT_FALSE(planor.resolve(ColorRole::Border).is_default);
    EXPECT_FALSE(planor.resolve(ColorRole::Selection).is_default);
    EXPECT_FALSE(planor.resolve(ColorRole::Muted).is_default);
    // Palette violet/magenta distincte du thème sombre neutre existant.
    EXPECT_NE(planor.resolve(ColorRole::Border), dark.resolve(ColorRole::Border));
    EXPECT_NE(planor.resolve(ColorRole::Selection), dark.resolve(ColorRole::Selection));
}

TEST(Theme, LightResolvesAllRoles) {
    const Theme& t = Theme::light();
    EXPECT_FALSE(t.resolve(ColorRole::Background).is_default);
    EXPECT_FALSE(t.resolve(ColorRole::Foreground).is_default);
}

TEST(Theme, DarkAndLightDiffer) {
    const Theme& dark = Theme::dark();
    const Theme& light = Theme::light();
    EXPECT_NE(dark.resolve(ColorRole::Background), light.resolve(ColorRole::Background));
    EXPECT_NE(dark.resolve(ColorRole::Foreground), light.resolve(ColorRole::Foreground));
}

TEST(Theme, SetOverridesRole) {
    Theme t;
    Color custom{1, 2, 3};
    t.set(ColorRole::Accent, custom);
    EXPECT_EQ(t.resolve(ColorRole::Accent), custom);
}

TEST(Theme, DefaultConstructedIsAllDefaultColor) {
    Theme t;
    EXPECT_TRUE(t.resolve(ColorRole::Background).is_default);
    EXPECT_TRUE(t.resolve(ColorRole::Foreground).is_default);
}

TEST(Theme, FocusStyleDefaultsToBoldAndIsSettable) {
    Theme t;
    EXPECT_TRUE(has_style(t.focus_style(), TextStyle::Bold));
    t.set_focus_style(TextStyle::Underline);
    EXPECT_TRUE(has_style(t.focus_style(), TextStyle::Underline));
    EXPECT_FALSE(has_style(t.focus_style(), TextStyle::Bold));
}
