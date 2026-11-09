#include <gtest/gtest.h>
#include <tui/widget/Container.hpp>
#include <tui/widget/FocusManager.hpp>
#include <tui/widget/layout/Vertical.hpp>

using namespace tui;

namespace {
class Leaf : public Widget {
public:
    bool focusable_ = false;
    Size measure(const Constraints& c) const override { return c.clamp({1, 1}); }
    void paint(Buffer&) const override {}
    [[nodiscard]] bool focusable() const override { return focusable_; }
};
} // namespace

TEST(Widget, DefaultsAreSensible) {
    Leaf w;
    EXPECT_TRUE(w.visible());
    EXPECT_EQ(w.parent(), nullptr);
    EXPECT_FALSE(w.focusable());
    EXPECT_TRUE(w.children().empty());
}

TEST(Widget, SetVisibleToggles) {
    Leaf w;
    w.set_visible(false);
    EXPECT_FALSE(w.visible());
}

TEST(Widget, InvalidateBubblesToRootCallback) {
    auto root = std::make_shared<Vertical>();
    auto mid = std::make_shared<Vertical>();
    auto leaf = std::make_shared<Leaf>();
    root->add_child(mid);
    mid->add_child(leaf);

    int calls = 0;
    root->set_repaint_callback([&] { ++calls; });

    leaf->need_repaint();
    EXPECT_EQ(calls, 1);

    mid->need_repaint();
    EXPECT_EQ(calls, 2);
}

TEST(Widget, InvalidateWithoutRootCallbackDoesNotCrash) {
    Leaf w;
    w.need_repaint(); // pas de callback enregistré : ne doit rien faire, ne pas planter
}

TEST(Container, AddChildSetsParent) {
    auto container = std::make_shared<Vertical>();
    auto child = std::make_shared<Leaf>();
    container->add_child(child);
    EXPECT_EQ(child->parent(), container.get());
    ASSERT_EQ(container->children().size(), 1u);
    EXPECT_EQ(container->children()[0], child);
}

TEST(Container, RemoveChildClearsParent) {
    auto container = std::make_shared<Vertical>();
    auto child = std::make_shared<Leaf>();
    container->add_child(child);
    container->remove_child(child);
    EXPECT_EQ(child->parent(), nullptr);
    EXPECT_TRUE(container->children().empty());
}

TEST(Container, RemoveUnknownChildIsNoOp) {
    auto container = std::make_shared<Vertical>();
    auto child = std::make_shared<Leaf>();
    auto stranger = std::make_shared<Leaf>();
    container->add_child(child);
    container->remove_child(stranger); // jamais ajouté
    EXPECT_EQ(container->children().size(), 1u);
}

TEST(Container, ChildrenPreserveInsertionOrder) {
    auto container = std::make_shared<Vertical>();
    auto a = std::make_shared<Leaf>();
    auto b = std::make_shared<Leaf>();
    auto c = std::make_shared<Leaf>();
    container->add_child(a);
    container->add_child(b);
    container->add_child(c);
    auto kids = container->children();
    ASSERT_EQ(kids.size(), 3u);
    EXPECT_EQ(kids[0], a);
    EXPECT_EQ(kids[1], b);
    EXPECT_EQ(kids[2], c);
}

TEST(FocusManager, TraversalSkipsNonFocusableAndWrapsAround) {
    auto root = std::make_shared<Vertical>();
    auto a = std::make_shared<Leaf>(); a->focusable_ = true;
    auto b = std::make_shared<Leaf>(); b->focusable_ = false;
    auto c = std::make_shared<Leaf>(); c->focusable_ = true;
    root->add_child(a);
    root->add_child(b);
    root->add_child(c);

    FocusManager fm;
    fm.focus_next(root);
    EXPECT_EQ(fm.current(), a);
    fm.focus_next(root);
    EXPECT_EQ(fm.current(), c); // b sauté (non focusable)
    fm.focus_next(root);
    EXPECT_EQ(fm.current(), a); // wraparound avant

    fm.focus_prev(root);
    EXPECT_EQ(fm.current(), c); // wraparound arrière
}

TEST(FocusManager, EmptyTreeLeavesFocusUnset) {
    auto root = std::make_shared<Vertical>();
    FocusManager fm;
    fm.focus_next(root);
    EXPECT_EQ(fm.current(), nullptr);
}

TEST(FocusManager, SetFocusCallsBlurThenFocus) {
    auto a = std::make_shared<Leaf>(); a->focusable_ = true;
    auto b = std::make_shared<Leaf>(); b->focusable_ = true;

    FocusManager fm;
    fm.set_focus(a);
    EXPECT_EQ(fm.current(), a);
    fm.set_focus(b);
    EXPECT_EQ(fm.current(), b);
}

TEST(FocusManager, NestedContainersAreTraversedDepthFirst) {
    auto root = std::make_shared<Vertical>();
    auto group = std::make_shared<Vertical>();
    auto a = std::make_shared<Leaf>(); a->focusable_ = true;
    auto b = std::make_shared<Leaf>(); b->focusable_ = true;
    group->add_child(a);
    group->add_child(b);
    root->add_child(group);

    FocusManager fm;
    fm.focus_next(root);
    EXPECT_EQ(fm.current(), a);
    fm.focus_next(root);
    EXPECT_EQ(fm.current(), b);
}

TEST(FocusManager, InvisibleWidgetsAreSkipped) {
    auto root = std::make_shared<Vertical>();
    auto a = std::make_shared<Leaf>(); a->focusable_ = true;
    auto b = std::make_shared<Leaf>(); b->focusable_ = true;
    b->set_visible(false);
    root->add_child(a);
    root->add_child(b);

    FocusManager fm;
    fm.focus_next(root);
    EXPECT_EQ(fm.current(), a);
    fm.focus_next(root);
    EXPECT_EQ(fm.current(), a); // b invisible, reste sur a (wraparound sur lui-même)
}
