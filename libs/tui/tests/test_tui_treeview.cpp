#include <gtest/gtest.h>
#include <tui/widgets/TreeView.hpp>

using namespace tui;

namespace {
std::shared_ptr<TreeNode> make_node(std::string label) {
    auto n = std::make_shared<TreeNode>();
    n->label = std::move(label);
    return n;
}
} // namespace

TEST(TreeView, StartsAtRoot) {
    auto root = make_node("root");
    TreeView tv;
    tv.set_root(root);
    EXPECT_EQ(tv.selected_index(), 0u);
    EXPECT_EQ(tv.selected_node()->label, "root");
}

TEST(TreeView, CollapsedChildrenAreNotNavigable) {
    auto root = make_node("root");
    root->expanded = true;
    root->children = {make_node("child1"), make_node("child2")};
    // enfants non-expandés par défaut, mais eux-mêmes n'ont pas de sous-enfants
    TreeView tv;
    tv.set_root(root);
    tv.on_key(KeyEvent{Key::Down});
    EXPECT_EQ(tv.selected_node()->label, "child1");
    tv.on_key(KeyEvent{Key::Down});
    EXPECT_EQ(tv.selected_node()->label, "child2");
}

TEST(TreeView, RootNotExpandedHidesChildren) {
    auto root = make_node("root");
    root->expanded = false;
    root->children = {make_node("child1")};
    TreeView tv;
    tv.set_root(root);
    tv.on_key(KeyEvent{Key::Down}); // rien à faire : pas d'enfants visibles
    EXPECT_EQ(tv.selected_node()->label, "root");
}

TEST(TreeView, RightArrowExpandsNode) {
    auto root = make_node("root");
    root->expanded = true;
    auto child = make_node("child");
    child->children = {make_node("grandchild")};
    root->children = {child};
    TreeView tv;
    tv.set_root(root);
    tv.on_key(KeyEvent{Key::Down}); // sélectionne "child"
    tv.on_key(KeyEvent{Key::Right}); // l'expand
    EXPECT_TRUE(child->expanded);
    tv.on_key(KeyEvent{Key::Down});
    EXPECT_EQ(tv.selected_node()->label, "grandchild");
}

TEST(TreeView, LeftArrowCollapsesExpandedNode) {
    auto root = make_node("root");
    root->expanded = true;
    auto child = make_node("child");
    child->expanded = true;
    child->children = {make_node("grandchild")};
    root->children = {child};
    TreeView tv;
    tv.set_root(root);
    tv.on_key(KeyEvent{Key::Down}); // "child"
    tv.on_key(KeyEvent{Key::Left}); // collapse
    EXPECT_FALSE(child->expanded);
    tv.on_key(KeyEvent{Key::Down}); // plus rien après "child" (grandchild caché) : reste clampé dessus
    EXPECT_EQ(tv.selected_node()->label, "child");
}

TEST(TreeView, LazyExpandCallbackFiresOnceOnFirstExpand) {
    auto root = make_node("root");
    root->expanded = true;
    auto child = make_node("child");
    int calls = 0;
    child->lazy_expand = [&](TreeNode& n) {
        ++calls;
        n.children.push_back(make_node("lazy-child"));
    };
    root->children = {child};

    TreeView tv;
    tv.set_root(root);
    tv.on_key(KeyEvent{Key::Down});
    tv.on_key(KeyEvent{Key::Right}); // premier expand : déclenche lazy_expand
    EXPECT_EQ(calls, 1);
    tv.on_key(KeyEvent{Key::Left});  // collapse
    tv.on_key(KeyEvent{Key::Right}); // ré-expand : ne redéclenche PAS lazy_expand
    EXPECT_EQ(calls, 1);
}

TEST(TreeView, NavigationSkipsCollapsedSubtree) {
    auto root = make_node("root");
    root->expanded = true;
    auto child1 = make_node("child1");
    child1->expanded = false; // collapsé
    child1->children = {make_node("hidden-grandchild")};
    auto child2 = make_node("child2");
    root->children = {child1, child2};

    TreeView tv;
    tv.set_root(root);
    tv.on_key(KeyEvent{Key::Down}); // child1
    tv.on_key(KeyEvent{Key::Down}); // child2 directement, grandchild caché sauté
    EXPECT_EQ(tv.selected_node()->label, "child2");
}

TEST(TreeView, OnActivateFiresOnEnter) {
    auto root = make_node("root");
    TreeView tv;
    tv.set_root(root);
    bool activated = false;
    tv.set_on_activate([&](TreeNode& n) { activated = (n.label == "root"); });
    tv.on_key(KeyEvent{Key::Enter});
    EXPECT_TRUE(activated);
}

TEST(TreeView, EmptyTreeIsNotFocusable) {
    TreeView tv;
    EXPECT_FALSE(tv.focusable());
}

TEST(TreeView, IdFieldIsIndependentOfLabelAndUnusedByTreeViewItself) {
    auto n = make_node("display name");
    n->id = "/full/path/on/disk";
    EXPECT_EQ(n->label, "display name");
    EXPECT_EQ(n->id, "/full/path/on/disk");
}

TEST(TreeView, MouseClickOnLeafSelectsAndFiresOnActivate) {
    auto root = make_node("root");
    root->expanded = true;
    root->children = {make_node("child1"), make_node("child2")};
    TreeView tv;
    tv.set_root(root);
    tv.arrange(Rect{0, 0, 20, 5});
    std::string activated_label;
    tv.set_on_activate([&](TreeNode& n) { activated_label = n.label; });
    tv.on_mouse(MouseEvent{2, 1, MouseEvent::Button::Left, MouseEvent::Action::Press, false, false, false});
    EXPECT_EQ(tv.selected_node()->label, "child1");
    EXPECT_EQ(activated_label, "child1");
}

TEST(TreeView, MouseClickOnExpandMarkerTogglesWithoutActivating) {
    auto root = make_node("root");
    root->expanded = true;
    auto child = make_node("child");
    child->children = {make_node("grandchild")};
    root->children = {child};
    TreeView tv;
    tv.set_root(root);
    tv.arrange(Rect{0, 0, 20, 5});
    bool activated = false;
    tv.set_on_activate([&](TreeNode&) { activated = true; });
    // Ligne 1 = "child" (profondeur 1 -> marqueur en x=2..3).
    tv.on_mouse(MouseEvent{2, 1, MouseEvent::Button::Left, MouseEvent::Action::Press, false, false, false});
    EXPECT_TRUE(child->expanded);
    EXPECT_FALSE(activated);
}

TEST(TreeView, ShowScrollbarReservesLastColumnForTrack) {
    auto root = make_node("root");
    root->expanded = true;
    for (int i = 0; i < 10; ++i) root->children.push_back(make_node("c" + std::to_string(i)));
    TreeView tv;
    tv.set_root(root);
    tv.set_show_scrollbar(true);
    tv.arrange(Rect{0, 0, 10, 3});
    Buffer buf(10, 3);
    tv.paint(buf);
    // Le curseur (thumb) occupe le haut de la piste (position 0) ; la
    // ligne suivante reste la piste seule ('│').
    EXPECT_EQ(buf.at(9, 1).codepoint, U'│');
}
