#include <gtest/gtest.h>
#include <tui/widgets/TextArea.hpp>

using namespace tui;

namespace {
void type_string(TextArea& ta, const std::string& s) {
    auto decoded = TextHelper::decode_utf8(s);
    for (char32_t ch : decoded) {
        if (ch == U'\n') ta.on_key(KeyEvent{Key::Enter});
        else ta.on_key(KeyEvent{Key::Char, ch});
    }
}

KeyEvent shift_key(Key k) { return KeyEvent{k, 0, true, false, false}; }
} // namespace

TEST(TextArea, TypingInsertsText) {
    TextArea ta;
    type_string(ta, "hello");
    EXPECT_EQ(ta.text(), "hello");
}

TEST(TextArea, EnterSplitsLine) {
    TextArea ta;
    type_string(ta, "hello");
    ta.on_key(KeyEvent{Key::Enter});
    type_string(ta, "world");
    EXPECT_EQ(ta.text(), "hello\nworld");
    EXPECT_EQ(ta.line_count(), 2u);
}

TEST(TextArea, BackspaceAtColumnZeroJoinsLines) {
    TextArea ta;
    ta.set_text("hello\nworld");
    ta.on_key(KeyEvent{Key::Down});
    ta.on_key(KeyEvent{Key::Home});
    ta.on_key(KeyEvent{Key::Backspace});
    EXPECT_EQ(ta.text(), "helloworld");
    EXPECT_EQ(ta.line_count(), 1u);
}

TEST(TextArea, DeleteAtEndOfLineJoinsNextLine) {
    TextArea ta;
    ta.set_text("hello\nworld");
    ta.on_key(KeyEvent{Key::Home});
    ta.on_key(KeyEvent{Key::End});
    ta.on_key(KeyEvent{Key::Delete});
    EXPECT_EQ(ta.text(), "helloworld");
    EXPECT_EQ(ta.line_count(), 1u);
}

TEST(TextArea, UndoOfCoalescedTypingRemovesWholeWord) {
    TextArea ta;
    type_string(ta, "hello");
    ta.undo();
    EXPECT_EQ(ta.text(), ""); // une seule frappe fusionnée -> un seul undo l'annule tout
}

TEST(TextArea, RedoRestoresAfterUndo) {
    TextArea ta;
    type_string(ta, "hello");
    ta.undo();
    ta.redo();
    EXPECT_EQ(ta.text(), "hello");
}

TEST(TextArea, UndoStopsAtWhitespaceBoundary) {
    TextArea ta;
    type_string(ta, "hello world");
    ta.undo();
    EXPECT_EQ(ta.text(), "hello "); // seul "world" est annulé, pas "hello "
    ta.undo();
    EXPECT_EQ(ta.text(), "");
}

TEST(TextArea, NewUndoStepAfterExplicitNonTypingAction) {
    TextArea ta;
    type_string(ta, "abc");
    ta.on_key(KeyEvent{Key::Left}); // action non-frappe : ferme le groupe de coalescing
    type_string(ta, "d");
    ta.undo();
    EXPECT_EQ(ta.text(), "abc"); // "d" s'annule seul, pas "abcd" en bloc
}

TEST(TextArea, SelectionExtendsWithShiftArrow) {
    TextArea ta;
    ta.set_text("hello world");
    ta.on_key(KeyEvent{Key::Home});
    for (int i = 0; i < 5; ++i) ta.on_key(shift_key(Key::Right));
    EXPECT_TRUE(ta.has_selection());
    EXPECT_EQ(ta.selected_text(), "hello");
}

TEST(TextArea, DeletingSelectionRemovesIt) {
    TextArea ta;
    ta.set_text("hello world");
    ta.on_key(KeyEvent{Key::Home});
    for (int i = 0; i < 5; ++i) ta.on_key(shift_key(Key::Right));
    ta.on_key(KeyEvent{Key::Backspace});
    EXPECT_EQ(ta.text(), " world");
}

TEST(TextArea, MultiLineSelectionDeletion) {
    TextArea ta;
    ta.set_text("line1\nline2\nline3");
    ta.on_key(KeyEvent{Key::Home});
    ta.on_key(shift_key(Key::Down));
    ta.on_key(shift_key(Key::Down));
    ASSERT_TRUE(ta.has_selection());
    ta.on_key(KeyEvent{Key::Backspace});
    EXPECT_EQ(ta.line_count(), 1u);
    EXPECT_EQ(ta.text(), "line3");
}

TEST(TextArea, UndoRestoresExactTextAfterMultiLineDelete) {
    TextArea ta;
    ta.set_text("abc\ndef");
    ta.on_key(KeyEvent{Key::Home});
    ta.on_key(shift_key(Key::Down));
    ta.on_key(shift_key(Key::End));
    ASSERT_EQ(ta.selected_text(), "abc\ndef");
    ta.on_key(KeyEvent{Key::Delete});
    EXPECT_EQ(ta.text(), "");
    ta.undo();
    EXPECT_EQ(ta.text(), "abc\ndef");
}

TEST(TextArea, CutRemovesAndReturnsSelection) {
    TextArea ta;
    ta.set_text("hello world");
    ta.on_key(KeyEvent{Key::Home});
    for (int i = 0; i < 5; ++i) ta.on_key(shift_key(Key::Right));
    std::string cut = ta.cut_selection();
    EXPECT_EQ(cut, "hello");
    EXPECT_EQ(ta.text(), " world");
}

TEST(TextArea, CopyDoesNotModifyText) {
    TextArea ta;
    ta.set_text("hello world");
    ta.on_key(KeyEvent{Key::Home});
    for (int i = 0; i < 5; ++i) ta.on_key(shift_key(Key::Right));
    std::string copied = ta.selected_text();
    EXPECT_EQ(copied, "hello");
    EXPECT_EQ(ta.text(), "hello world"); // inchangé
}

TEST(TextArea, MovingCursorWithoutShiftCollapsesSelection) {
    TextArea ta;
    ta.set_text("hello world");
    ta.on_key(KeyEvent{Key::Home});
    for (int i = 0; i < 5; ++i) ta.on_key(shift_key(Key::Right));
    ASSERT_TRUE(ta.has_selection());
    ta.on_key(KeyEvent{Key::Right}); // sans shift : effondre la sélection
    EXPECT_FALSE(ta.has_selection());
}

TEST(TextArea, SetTextResetsHistoryAndSelection) {
    TextArea ta;
    type_string(ta, "hello");
    ta.set_text("fresh start");
    EXPECT_FALSE(ta.can_undo());
    EXPECT_EQ(ta.text(), "fresh start");
}

TEST(TextArea, CanUndoRedoReflectStackState) {
    TextArea ta;
    EXPECT_FALSE(ta.can_undo());
    type_string(ta, "x");
    EXPECT_TRUE(ta.can_undo());
    EXPECT_FALSE(ta.can_redo());
    ta.undo();
    EXPECT_FALSE(ta.can_undo());
    EXPECT_TRUE(ta.can_redo());
}

// --- Multicurseur --------------------------------------------------------

TEST(TextArea, TypingWithSecondaryCursorInsertsAtBothPositions) {
    TextArea ta;
    ta.set_text("foo\nbar");
    ta.on_key(KeyEvent{Key::End}); // curseur principal -> fin de "foo"
    ta.add_cursor_at({1, 3});      // curseur secondaire -> fin de "bar"
    ta.on_key(KeyEvent{Key::Char, U'!'});
    EXPECT_EQ(ta.text(), "foo!\nbar!");
}

TEST(TextArea, EnterWithSecondaryCursorSplitsBothLinesAndKeepsThemInSync) {
    TextArea ta;
    ta.set_text("foo\nbar");
    ta.on_key(KeyEvent{Key::End});
    ta.add_cursor_at({1, 3});
    ta.on_key(KeyEvent{Key::Enter});
    EXPECT_EQ(ta.text(), "foo\n\nbar\n");
}

TEST(TextArea, BackspaceWithSecondaryCursorDeletesAtBothPositions) {
    TextArea ta;
    ta.set_text("foo\nbar");
    ta.on_key(KeyEvent{Key::End});
    ta.add_cursor_at({1, 3});
    ta.on_key(KeyEvent{Key::Backspace});
    EXPECT_EQ(ta.text(), "fo\nba");
}

TEST(TextArea, EscapeClearsSecondaryCursors) {
    TextArea ta;
    ta.set_text("foo\nbar");
    ta.add_cursor_at({1, 0});
    ASSERT_TRUE(ta.has_multiple_cursors());
    ta.on_key(KeyEvent{Key::Escape});
    EXPECT_FALSE(ta.has_multiple_cursors());
}

TEST(TextArea, ArrowKeyClearsSecondaryCursors) {
    TextArea ta;
    ta.set_text("foo\nbar");
    ta.add_cursor_at({1, 0});
    ta.on_key(KeyEvent{Key::Right});
    EXPECT_FALSE(ta.has_multiple_cursors());
}

TEST(TextArea, DuplicateCursorPositionIsIgnored) {
    TextArea ta;
    ta.set_text("foo"); // curseur principal en (0,0)
    ta.add_cursor_at({0, 2});
    ta.add_cursor_at({0, 2}); // doublon d'un secondaire existant : ignoré
    EXPECT_EQ(ta.secondary_cursors().size(), 1u);
}

TEST(TextArea, CursorAtPrimaryPositionIsIgnored) {
    TextArea ta;
    ta.set_text("foo"); // curseur principal en (0,0)
    ta.add_cursor_at({0, 0});
    EXPECT_FALSE(ta.has_multiple_cursors());
}

TEST(TextArea, CtrlAltDownAddsCursorOnLineBelowAtSameColumn) {
    TextArea ta;
    ta.set_text("aa\nbb\ncc");
    ta.on_key(KeyEvent{Key::Right}); // curseur en (0,1)
    ta.on_key(KeyEvent{Key::Down, 0, false, true, true}); // Ctrl+Alt+Bas
    ASSERT_TRUE(ta.has_multiple_cursors());
    ASSERT_EQ(ta.secondary_cursors().size(), 1u);
    EXPECT_EQ(ta.secondary_cursors()[0].line, 1);
    EXPECT_EQ(ta.secondary_cursors()[0].col, 1);
}

TEST(TextArea, AltClickAddsSecondaryCursorInsteadOfMovingPrimary) {
    TextArea ta;
    ta.set_text("hello");
    ta.arrange(Rect{0, 0, 20, 3});
    TextArea::Pos before = ta.cursor();
    ta.on_mouse(MouseEvent{3, 0, MouseEvent::Button::Left, MouseEvent::Action::Press, false, false, true});
    EXPECT_EQ(ta.cursor().line, before.line);
    EXPECT_EQ(ta.cursor().col, before.col);
    EXPECT_TRUE(ta.has_multiple_cursors());
}

// --- Lecture seule / hook d'interception -----------------------------------

TEST(TextArea, ReadOnlyBlocksMutationButAllowsNavigation) {
    TextArea ta;
    ta.set_text("hello");
    ta.set_read_only(true);
    ta.on_key(KeyEvent{Key::Char, U'x'});
    EXPECT_EQ(ta.text(), "hello");
    ta.on_key(KeyEvent{Key::End});
    EXPECT_EQ(ta.cursor().col, 5);
}

TEST(TextArea, KeyInterceptHookRunsBeforeInternalHandling) {
    TextArea ta;
    bool intercepted = false;
    ta.set_key_intercept([&](const KeyEvent& e) {
        if (e.key == Key::Char && e.ctrl && e.codepoint == U's') { intercepted = true; return true; }
        return false;
    });
    ta.on_key(KeyEvent{Key::Char, U's', false, true, false});
    EXPECT_TRUE(intercepted);
    EXPECT_EQ(ta.text(), "");
}

// --- Scrollbar --------------------------------------------------------------

TEST(TextArea, ShowScrollbarReservesLastColumnForTrack) {
    TextArea ta;
    ta.set_text("a\nb\nc\nd\ne\nf\ng\nh");
    ta.set_show_scrollbar(true);
    ta.arrange(Rect{0, 0, 10, 3});
    Buffer buf(10, 3);
    ta.paint(buf);
    // Position de scroll 0 : le curseur de la piste occupe le haut, donc
    // on vérifie la dernière ligne (garantie hors du curseur ici).
    EXPECT_EQ(buf.at(9, 2).codepoint, U'│');
}
