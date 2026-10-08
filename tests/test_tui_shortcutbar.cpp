#include <gtest/gtest.h>
#include <tui/widgets/ShortcutBar.hpp>

using namespace tui;

TEST(ShortcutBar, PaintsFirstHintKeyAtOrigin) {
    ShortcutBar sb;
    sb.set_hints({{"F1", "Help"}, {"Ctrl+Q", "Quit"}});
    sb.arrange(Rect{0, 0, 40, 1});
    Buffer buf(40, 1);
    sb.paint(buf);
    EXPECT_EQ(buf.at(0, 0).codepoint, U'F');
    EXPECT_EQ(buf.at(1, 0).codepoint, U'1');
}

TEST(ShortcutBar, KeyPortionIsReversedStyle) {
    ShortcutBar sb;
    sb.set_hints({{"F1", "Help"}});
    sb.arrange(Rect{0, 0, 40, 1});
    Buffer buf(40, 1);
    sb.paint(buf);
    EXPECT_EQ(buf.at(0, 0).style, TextStyle::Reverse);
    // La partie label (après "F1 ") ne doit pas être en style inversé.
    EXPECT_EQ(buf.at(3, 0).style, TextStyle::None);
}

TEST(ShortcutBar, EmptyHintsDoesNotCrash) {
    ShortcutBar sb;
    sb.arrange(Rect{0, 0, 40, 1});
    Buffer buf(40, 1);
    sb.paint(buf);
}

TEST(ShortcutBar, TruncatesAtBoundsWidth) {
    ShortcutBar sb;
    sb.set_hints({{"F1", "A very very very long label that overflows"}});
    sb.arrange(Rect{0, 0, 5, 1});
    Buffer buf(5, 1);
    sb.paint(buf); // ne doit pas écrire hors des 5 colonnes ni planter
}

TEST(ShortcutBar, VerticalOrientationListsEachHintOnItsOwnRow) {
    ShortcutBar sb;
    sb.set_orientation(ShortcutBarOrientation::Vertical);
    sb.set_hints({{"up/k", "up"}, {"down/j", "down"}});
    sb.arrange(Rect{0, 0, 20, 5});
    Buffer buf(20, 5);
    sb.paint(buf);

    EXPECT_EQ(buf.at(0, 0).codepoint, U'u'); // "up/k" ligne 0
    EXPECT_EQ(buf.at(0, 1).codepoint, U'd'); // "down/j" ligne 1
    // Pas de vidéo inverse en orientation verticale.
    EXPECT_EQ(buf.at(0, 0).style, TextStyle::None);
}

TEST(ShortcutBar, VerticalMeasureHeightMatchesHintCount) {
    ShortcutBar sb;
    sb.set_orientation(ShortcutBarOrientation::Vertical);
    sb.set_hints({{"a", "A"}, {"b", "B"}, {"c", "C"}});
    Size s = sb.measure(Constraints::loose({40, 40}));
    EXPECT_EQ(s.height, 3);
}
