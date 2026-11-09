#include <gtest/gtest.h>
#include <tui/core/Geometry.hpp>

using namespace tui;

TEST(Point, Arithmetic) {
    Point a{2, 3};
    Point b{10, 20};
    EXPECT_EQ(a + b, (Point{12, 23}));
    EXPECT_EQ(b - a, (Point{8, 17}));
}

TEST(Size, Empty) {
    EXPECT_TRUE(Size(0, 5).empty());
    EXPECT_TRUE(Size(5, 0).empty());
    EXPECT_TRUE(Size(-1, 5).empty());
    EXPECT_FALSE(Size(1, 1).empty());
}

TEST(Rect, Contains) {
    Rect r{0, 0, 10, 5};
    EXPECT_TRUE(r.contains({0, 0}));
    EXPECT_TRUE(r.contains({9, 4}));
    EXPECT_FALSE(r.contains({10, 4})); // borne exclusive
    EXPECT_FALSE(r.contains({9, 5}));
    EXPECT_FALSE(r.contains({-1, 0}));
}

TEST(Rect, OriginAndSize) {
    Rect r{3, 4, 10, 20};
    EXPECT_EQ(r.origin(), (Point{3, 4}));
    EXPECT_EQ(r.size(), (Size{10, 20}));
    EXPECT_EQ(r.right(), 13);
    EXPECT_EQ(r.bottom(), 24);
}

TEST(Rect, IntersectOverlapping) {
    Rect a{0, 0, 10, 10};
    Rect b{5, 5, 10, 10};
    Rect i = a.intersect(b);
    EXPECT_EQ(i, (Rect{5, 5, 5, 5}));
}

TEST(Rect, IntersectDisjointIsEmpty) {
    Rect a{0, 0, 5, 5};
    Rect b{100, 100, 5, 5};
    Rect i = a.intersect(b);
    EXPECT_TRUE(i.empty());
}

TEST(Rect, Inset) {
    Rect r{0, 0, 10, 10};
    Rect inner = r.inset(1);
    EXPECT_EQ(inner, (Rect{1, 1, 8, 8}));

    Rect over_inset = r.inset(100);
    EXPECT_TRUE(over_inset.empty());
}

TEST(Constraints, Clamp) {
    Constraints c = Constraints::loose({80, 24});
    EXPECT_EQ(c.clamp({100, 100}), (Size{80, 24}));
    EXPECT_EQ(c.clamp({-5, -5}), (Size{0, 0}));

    Constraints tight = Constraints::tight({20, 10});
    EXPECT_EQ(tight.clamp({1, 1}), (Size{20, 10}));
    EXPECT_EQ(tight.clamp({999, 999}), (Size{20, 10}));
}
