#include <gtest/gtest.h>
#include <tui/widgets/ListView.hpp>

using namespace tui;

namespace {
std::vector<ListItem> make_items(int n) {
    std::vector<ListItem> items;
    for (int i = 0; i < n; ++i) items.push_back(ListItem{"item" + std::to_string(i), "", true, ""});
    return items;
}
} // namespace

TEST(ListView, DownArrowMovesSelection) {
    ListView lv;
    lv.set_items(make_items(5));
    lv.on_key(KeyEvent{Key::Down});
    lv.on_key(KeyEvent{Key::Down});
    EXPECT_EQ(lv.selected_index(), 2u);
}

TEST(ListView, UpArrowAtTopIsClamped) {
    ListView lv;
    lv.set_items(make_items(5));
    lv.on_key(KeyEvent{Key::Up});
    EXPECT_EQ(lv.selected_index(), 0u);
}

TEST(ListView, HomeEndJumpToEdges) {
    ListView lv;
    lv.set_items(make_items(10));
    lv.on_key(KeyEvent{Key::End});
    EXPECT_EQ(lv.selected_index(), 9u);
    lv.on_key(KeyEvent{Key::Home});
    EXPECT_EQ(lv.selected_index(), 0u);
}

TEST(ListView, ScrollFollowsSelectionBelowViewport) {
    ListView lv;
    lv.set_items(make_items(20));
    lv.arrange(Rect{0, 0, 20, 5});
    for (int i = 0; i < 12; ++i) lv.on_key(KeyEvent{Key::Down});
    EXPECT_EQ(lv.selected_index(), 12u);
    // La ligne sélectionnée doit être peinte quelque part dans la fenêtre visible.
    Buffer buf(20, 5);
    lv.on_focus();
    lv.paint(buf);
    bool found = false;
    for (int y = 0; y < 5; ++y) {
        if (buf.at(0, y).style == TextStyle::Reverse) found = true;
    }
    EXPECT_TRUE(found);
}

TEST(ListView, MultiSelectTogglesWithSpace) {
    ListView lv;
    lv.set_items(make_items(5));
    lv.set_multi_select(true);
    lv.on_key(KeyEvent{Key::Down});
    lv.on_key(KeyEvent{Key::Char, U' '});
    EXPECT_TRUE(lv.is_checked(1));
    lv.on_key(KeyEvent{Key::Char, U' '});
    EXPECT_FALSE(lv.is_checked(1));
}

TEST(ListView, SingleSelectModeIgnoresSpaceToggle) {
    ListView lv;
    lv.set_items(make_items(5));
    lv.on_key(KeyEvent{Key::Char, U' '});
    EXPECT_FALSE(lv.is_checked(0));
}

TEST(ListView, EmptyListIsNotFocusable) {
    ListView lv;
    EXPECT_FALSE(lv.focusable());
}

TEST(ListView, EnterTriggersOnActivate) {
    ListView lv;
    lv.set_items(make_items(3));
    bool activated = false;
    lv.set_on_activate([&](size_t) { activated = true; });
    lv.on_key(KeyEvent{Key::Enter});
    EXPECT_TRUE(activated);
}

TEST(ListView, SetSelectedIndexFiresOnSelect) {
    ListView lv;
    lv.set_items(make_items(5));
    size_t last = 999;
    lv.set_on_select([&](size_t i) { last = i; });
    lv.set_selected_index(3);
    EXPECT_EQ(last, 3u);
}

TEST(ListView, DetailedStyleRendersTitleAndDetailOnSeparateRows) {
    ListView lv;
    lv.set_style(ListViewStyle::Detailed);
    lv.set_colors(Color{217, 87, 190}, Color{225, 225, 232}, Color{130, 130, 140});
    lv.set_items({ListItem{"ci-fn-be-rules", "Succeeded"}, ListItem{"ci-fn-email-service", "Succeeded"}});
    lv.arrange(Rect{0, 0, 30, 12});
    lv.on_focus();

    Buffer buf(30, 12);
    lv.paint(buf);

    // Item sélectionné (index 0) : barre d'accent sur les 2 lignes du bloc.
    EXPECT_EQ(buf.at(0, 0).codepoint, U'│');
    EXPECT_EQ(buf.at(0, 0).fg, (Color{217, 87, 190}));
    EXPECT_EQ(buf.at(0, 1).codepoint, U'│');
    EXPECT_EQ(buf.at(2, 0).codepoint, U'c'); // début de "ci-fn-be-rules"
    EXPECT_EQ(buf.at(2, 1).codepoint, U'S'); // début de "Succeeded"

    // Deuxième item (non sélectionné) : pas de barre, 3ème ligne du bloc = espacement.
    EXPECT_EQ(buf.at(0, 3).codepoint, U' ');
    EXPECT_EQ(buf.at(2, 3).codepoint, U'c'); // "ci-fn-email-service" démarre à la ligne 3 (bloc de 3 lignes)
}

TEST(ListView, ShowScrollbarReservesLastColumnForTrack) {
    ListView lv;
    lv.set_items(make_items(20));
    lv.set_show_scrollbar(true);
    lv.arrange(Rect{0, 0, 10, 5});
    Buffer buf(10, 5);
    lv.paint(buf);
    // Position de scroll 0 : le curseur occupe le haut de la piste, donc
    // on vérifie la dernière ligne (garantie hors du curseur ici).
    EXPECT_EQ(buf.at(9, 4).codepoint, U'│');
}

TEST(ListView, ClickOnScrollbarColumnJumpsScrollInsteadOfSelecting) {
    ListView lv;
    lv.set_items(make_items(100));
    lv.set_show_scrollbar(true);
    lv.arrange(Rect{0, 0, 10, 10});
    lv.on_mouse(MouseEvent{9, 9, MouseEvent::Button::Left, MouseEvent::Action::Press, false, false, false});
    // La sélection ne doit pas avoir bougé (le clic a été intercepté par la piste).
    EXPECT_EQ(lv.selected_index(), 0u);
}

TEST(ListView, CompactStyleIsStillDefaultAndUsesReverseVideo) {
    ListView lv;
    lv.set_items(make_items(3));
    lv.arrange(Rect{0, 0, 20, 3});
    lv.on_focus();
    Buffer buf(20, 3);
    lv.paint(buf);
    EXPECT_EQ(buf.at(0, 0).style, TextStyle::Reverse);
}
