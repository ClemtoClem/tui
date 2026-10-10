#include <gtest/gtest.h>
#include <tui/widget/layout/Align.hpp>
#include <tui/widget/layout/Border.hpp>
#include <tui/widget/layout/Grid.hpp>
#include <tui/widget/layout/Horizontal.hpp>
#include <tui/widget/layout/ScrollableContainer.hpp>
#include <tui/widget/layout/SplitPane.hpp>
#include <tui/widget/layout/Stack.hpp>
#include <tui/widget/layout/Tabs.hpp>
#include <tui/widget/layout/Vertical.hpp>

using namespace tui;

namespace {
class Leaf : public Widget {
public:
    Rect last_arranged;
    Size natural_size{1, 1};
    [[nodiscard]] Size measure(const Constraints& c) const override { return c.clamp(natural_size); }
    void arrange(Rect r) override { bounds_ = r; last_arranged = r; }
    void paint(Buffer&) const override {}
};
} // namespace

// --- Vertical / Horizontal (LinearLayout) ---

TEST(Vertical, FixedThenStretchFillsRemainder) {
    auto v = std::make_shared<Vertical>();
    auto a = std::make_shared<Leaf>();
    auto b = std::make_shared<Leaf>();
    auto c = std::make_shared<Leaf>();
    v->add_child(a, LayoutParams::fixed(3));
    v->add_child(b, LayoutParams::fixed(2));
    v->add_child(c, LayoutParams::stretch(1));

    v->arrange(Rect{0, 0, 10, 20});
    EXPECT_EQ(a->last_arranged, (Rect{0, 0, 10, 3}));
    EXPECT_EQ(b->last_arranged, (Rect{0, 3, 10, 2}));
    EXPECT_EQ(c->last_arranged, (Rect{0, 5, 10, 15}));
}

TEST(Horizontal, WeightedStretchSplitsProportionally) {
    auto h = std::make_shared<Horizontal>();
    auto a = std::make_shared<Leaf>();
    auto b = std::make_shared<Leaf>();
    h->add_child(a, LayoutParams::stretch(1));
    h->add_child(b, LayoutParams::stretch(2));
    h->arrange(Rect{0, 0, 30, 5});

    EXPECT_EQ(a->last_arranged.width, 10);
    EXPECT_EQ(b->last_arranged.width, 20);
    EXPECT_EQ(a->last_arranged.x, 0);
    EXPECT_EQ(b->last_arranged.x, 10);
}

TEST(Vertical, OverflowingFixedSizeClampsStretchToZero) {
    auto v = std::make_shared<Vertical>();
    auto a = std::make_shared<Leaf>();
    auto b = std::make_shared<Leaf>();
    v->add_child(a, LayoutParams::fixed(100));
    v->add_child(b, LayoutParams::stretch(1));
    v->arrange(Rect{0, 0, 10, 10});

    EXPECT_EQ(a->last_arranged.height, 100);
    EXPECT_EQ(b->last_arranged.height, 0);
}

TEST(Horizontal, CrossAlignCenterCentersOnCrossAxis) {
    auto h = std::make_shared<Horizontal>();
    auto a = std::make_shared<Leaf>();
    a->natural_size = {4, 2};
    LayoutParams p = LayoutParams::fixed(4);
    p.cross_align = CrossAlign::Center;
    h->add_child(a, p);
    h->arrange(Rect{0, 0, 20, 10});

    EXPECT_EQ(a->last_arranged.height, 2);
    EXPECT_EQ(a->last_arranged.y, 4);
}

TEST(Vertical, AutoModeUsesChildMeasure) {
    auto v = std::make_shared<Vertical>();
    auto a = std::make_shared<Leaf>();
    a->natural_size = {5, 7};
    v->add_child(a, LayoutParams::auto_size());
    v->arrange(Rect{0, 0, 20, 20});
    EXPECT_EQ(a->last_arranged.height, 7);
}

// --- Stack ---

TEST(Stack, AllChildrenGetTheSameRect) {
    auto s = std::make_shared<Stack>();
    auto a = std::make_shared<Leaf>();
    auto b = std::make_shared<Leaf>();
    s->add_child(a);
    s->add_child(b);
    s->arrange(Rect{1, 2, 10, 10});
    EXPECT_EQ(a->last_arranged, (Rect{1, 2, 10, 10}));
    EXPECT_EQ(b->last_arranged, (Rect{1, 2, 10, 10}));
}

// --- Border ---

TEST(Border, InsetsChildByOneCellOnEachSide) {
    auto b = std::make_shared<Border>();
    auto child = std::make_shared<Leaf>();
    b->set_child(child);
    b->arrange(Rect{0, 0, 10, 5});
    EXPECT_EQ(child->last_arranged, (Rect{1, 1, 8, 3}));
}

TEST(Border, RoundedStyleDrawsRoundedCorners) {
    auto b = std::make_shared<Border>();
    b->set_border_style(BorderStyle::Rounded);
    b->arrange(Rect{0, 0, 10, 5});
    Buffer buf(10, 5);
    b->paint(buf);
    EXPECT_EQ(buf.at(0, 0).codepoint, U'╭');
    EXPECT_EQ(buf.at(9, 0).codepoint, U'╮');
    EXPECT_EQ(buf.at(0, 4).codepoint, U'╰');
    EXPECT_EQ(buf.at(9, 4).codepoint, U'╯');
}

TEST(Border, SquareStyleIsDefaultAndUnchanged) {
    auto b = std::make_shared<Border>();
    b->arrange(Rect{0, 0, 10, 5});
    Buffer buf(10, 5);
    b->paint(buf);
    EXPECT_EQ(buf.at(0, 0).codepoint, U'┌');
}

TEST(Border, FocusedColorAppliesOnlyWhenFocused) {
    auto b = std::make_shared<Border>();
    b->set_color(Color{1, 2, 3});
    b->set_focused_color(Color{9, 8, 7});
    b->arrange(Rect{0, 0, 10, 5});
    Buffer buf(10, 5);
    b->paint(buf);
    EXPECT_EQ(buf.at(0, 0).fg, (Color{1, 2, 3}));

    b->set_focused(true);
    b->paint(buf);
    EXPECT_EQ(buf.at(0, 0).fg, (Color{9, 8, 7}));
}

// --- Align ---

TEST(Align, CenterCenterPositionsMidway) {
    auto a = std::make_shared<Align>(HAlign::Center, VAlign::Center);
    auto child = std::make_shared<Leaf>();
    child->natural_size = {4, 2};
    a->set_child(child);
    a->arrange(Rect{0, 0, 10, 10});
    EXPECT_EQ(child->last_arranged.x, 3);
    EXPECT_EQ(child->last_arranged.y, 4);
}

TEST(Align, StartEndPositionsAtEdges) {
    auto a = std::make_shared<Align>(HAlign::Start, VAlign::End);
    auto child = std::make_shared<Leaf>();
    child->natural_size = {3, 3};
    a->set_child(child);
    a->arrange(Rect{0, 0, 10, 10});
    EXPECT_EQ(child->last_arranged.x, 0);
    EXPECT_EQ(child->last_arranged.y, 7);
}

TEST(Align, StretchFillsAvailableSpace) {
    auto a = std::make_shared<Align>(HAlign::Stretch, VAlign::Stretch);
    auto child = std::make_shared<Leaf>();
    child->natural_size = {2, 2};
    a->set_child(child);
    a->arrange(Rect{0, 0, 10, 10});
    EXPECT_EQ(child->last_arranged, (Rect{0, 0, 10, 10}));
}

// --- Grid ---

TEST(Grid, MixedFixedAndFractionTracks) {
    auto g = std::make_shared<Grid>();
    g->set_columns({GridTrack::fixed(3), GridTrack::fr(1), GridTrack::fr(2)});
    g->set_rows({GridTrack::fr(1)});
    auto a = std::make_shared<Leaf>();
    auto b = std::make_shared<Leaf>();
    auto c = std::make_shared<Leaf>();
    g->add_child(a, GridPlacement{0, 0});
    g->add_child(b, GridPlacement{0, 1});
    g->add_child(c, GridPlacement{0, 2});
    g->arrange(Rect{0, 0, 30, 5});

    EXPECT_EQ(a->last_arranged, (Rect{0, 0, 3, 5}));
    EXPECT_EQ(b->last_arranged, (Rect{3, 0, 9, 5}));
    EXPECT_EQ(c->last_arranged, (Rect{12, 0, 18, 5}));
}

TEST(Grid, ColumnSpanCoversMultipleTracks) {
    auto g = std::make_shared<Grid>();
    g->set_columns({GridTrack::fixed(5), GridTrack::fixed(5), GridTrack::fixed(5)});
    g->set_rows({GridTrack::fixed(3), GridTrack::fixed(3)});
    auto a = std::make_shared<Leaf>();
    g->add_child(a, GridPlacement{0, 0, 1, 2});
    g->arrange(Rect{0, 0, 15, 6});
    EXPECT_EQ(a->last_arranged, (Rect{0, 0, 10, 3}));
}

TEST(Grid, EmptyTracksProduceNoLayout) {
    auto g = std::make_shared<Grid>();
    auto a = std::make_shared<Leaf>();
    g->add_child(a, GridPlacement{0, 0});
    g->arrange(Rect{0, 0, 10, 10}); // pas de set_rows/set_columns
    EXPECT_EQ(a->last_arranged, Rect{}); // jamais arrangé, ne plante pas
}

// --- ScrollableContainer ---

TEST(ScrollableContainer, ScrollClampsToContentBounds) {
    auto s = std::make_shared<ScrollableContainer>();
    auto child = std::make_shared<Leaf>();
    child->natural_size = {100, 100};
    s->set_child(child);
    s->arrange(Rect{0, 0, 10, 10});

    EXPECT_EQ(s->max_scroll_x(), 90);
    EXPECT_EQ(s->max_scroll_y(), 90);

    s->scroll_to(1000, 1000);
    EXPECT_EQ(s->scroll_x(), 90);
    EXPECT_EQ(s->scroll_y(), 90);

    s->scroll_to(-50, -50);
    EXPECT_EQ(s->scroll_x(), 0);
    EXPECT_EQ(s->scroll_y(), 0);
}

TEST(ScrollableContainer, ArrangeOffsetsChildByScrollPosition) {
    auto s = std::make_shared<ScrollableContainer>();
    auto child = std::make_shared<Leaf>();
    child->natural_size = {100, 100};
    s->set_child(child);
    s->arrange(Rect{0, 0, 10, 10});

    s->scroll_to(20, 30);
    s->arrange(Rect{0, 0, 10, 10});
    EXPECT_EQ(child->last_arranged.x, -20);
    EXPECT_EQ(child->last_arranged.y, -30);
}

TEST(ScrollableContainer, ContentSmallerThanViewportHasNoScrollRange) {
    auto s = std::make_shared<ScrollableContainer>();
    auto child = std::make_shared<Leaf>();
    child->natural_size = {5, 5};
    s->set_child(child);
    s->arrange(Rect{0, 0, 10, 10});
    EXPECT_EQ(s->max_scroll_x(), 0);
    EXPECT_EQ(s->max_scroll_y(), 0);
}

TEST(ScrollableVertical, IgnoresHorizontalKeys) {
    auto s = std::make_shared<ScrollableVertical>();
    auto child = std::make_shared<Leaf>();
    child->natural_size = {100, 100};
    s->set_child(child);
    s->arrange(Rect{0, 0, 10, 10});

    EXPECT_FALSE(s->on_key(KeyEvent{Key::Left}));
    EXPECT_TRUE(s->on_key(KeyEvent{Key::Down}));
}

// --- SplitPane ---

TEST(SplitPane, RatioSplitsSpaceMinusDivider) {
    auto sp = std::make_shared<SplitPane>(SplitPane::Axis::LeftRight);
    auto a = std::make_shared<Leaf>();
    auto b = std::make_shared<Leaf>();
    sp->set_first(a);
    sp->set_second(b);
    sp->set_ratio(0.5);
    sp->arrange(Rect{0, 0, 21, 10});

    EXPECT_EQ(a->last_arranged.width, 10);
    EXPECT_EQ(b->last_arranged.width, 10);
    EXPECT_EQ(b->last_arranged.x, 11);
}

TEST(SplitPane, KeyboardAdjustsRatioByOneCell) {
    auto sp = std::make_shared<SplitPane>(SplitPane::Axis::LeftRight);
    auto a = std::make_shared<Leaf>();
    auto b = std::make_shared<Leaf>();
    sp->set_first(a);
    sp->set_second(b);
    sp->set_ratio(0.5);
    sp->arrange(Rect{0, 0, 20, 10});
    int before = a->last_arranged.width;

    sp->on_key(KeyEvent{Key::Right});
    sp->arrange(Rect{0, 0, 20, 10});
    EXPECT_EQ(a->last_arranged.width, before + 1);
}

TEST(SplitPane, MouseDragSetsRatioFromPosition) {
    auto sp = std::make_shared<SplitPane>(SplitPane::Axis::LeftRight);
    auto a = std::make_shared<Leaf>();
    auto b = std::make_shared<Leaf>();
    sp->set_first(a);
    sp->set_second(b);
    sp->arrange(Rect{0, 0, 20, 10});

    sp->on_mouse(MouseEvent{15, 0, MouseEvent::Button::Left, MouseEvent::Action::Press, false, false, false});
    EXPECT_NEAR(sp->ratio(), 0.75, 0.01);
}

// --- Tabs ---

TEST(Tabs, OnlyActivePageIsArranged) {
    auto tabs = std::make_shared<Tabs>();
    auto page0 = std::make_shared<Leaf>();
    auto page1 = std::make_shared<Leaf>();
    tabs->add_tab("One", page0);
    tabs->add_tab("Two", page1);
    tabs->arrange(Rect{0, 0, 20, 10});

    EXPECT_EQ(page0->last_arranged.height, 9);
    EXPECT_EQ(page1->last_arranged, Rect{});
}

TEST(Tabs, SwitchingActiveIndexArrangesNewPage) {
    auto tabs = std::make_shared<Tabs>();
    auto page0 = std::make_shared<Leaf>();
    auto page1 = std::make_shared<Leaf>();
    tabs->add_tab("One", page0);
    tabs->add_tab("Two", page1);
    tabs->arrange(Rect{0, 0, 20, 10});

    tabs->set_active_index(1);
    tabs->arrange(Rect{0, 0, 20, 10});
    EXPECT_EQ(page1->last_arranged.height, 9);
}

TEST(Tabs, CtrlRightAdvancesWithWraparound) {
    auto tabs = std::make_shared<Tabs>();
    tabs->add_tab("One", std::make_shared<Leaf>());
    tabs->add_tab("Two", std::make_shared<Leaf>());

    EXPECT_TRUE(tabs->on_key(KeyEvent{Key::Right, 0, false, true, false}));
    EXPECT_EQ(tabs->active_index(), 1u);
    EXPECT_TRUE(tabs->on_key(KeyEvent{Key::Right, 0, false, true, false}));
    EXPECT_EQ(tabs->active_index(), 0u); // wraparound
}

TEST(Tabs, BoxedStyleReservesThreeRowsForTheBar) {
    auto tabs = std::make_shared<Tabs>();
    auto page0 = std::make_shared<Leaf>();
    tabs->add_tab("One", page0);
    tabs->set_bar_style(TabsBarStyle::Boxed);
    tabs->arrange(Rect{0, 0, 20, 10});
    EXPECT_EQ(page0->last_arranged, (Rect{0, 3, 20, 7}));
}

TEST(Tabs, BoxedStyleDrawsRoundedTabBoxes) {
    auto tabs = std::make_shared<Tabs>();
    tabs->add_tab("One", std::make_shared<Leaf>());
    tabs->add_tab("Two", std::make_shared<Leaf>());
    tabs->set_bar_style(TabsBarStyle::Boxed);
    tabs->arrange(Rect{0, 0, 20, 10});

    Buffer buf(20, 10);
    tabs->paint(buf);
    EXPECT_EQ(buf.at(0, 0).codepoint, U'╭'); // coin haut-gauche du premier onglet
    EXPECT_EQ(buf.at(2, 1).codepoint, U'O'); // libellé "One" sur la ligne du milieu (après le "│ " de bord)
    EXPECT_EQ(buf.at(0, 2).codepoint, U'╰'); // ligne du bas / règle pleine largeur
}
