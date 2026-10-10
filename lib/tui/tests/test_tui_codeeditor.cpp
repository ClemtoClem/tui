#include <gtest/gtest.h>
#include <tui/widgets/CodeEditor.hpp>

using namespace tui;

TEST(CodeEditor, AddTabMakesItActiveAndFocusable) {
    CodeEditor ed;
    EXPECT_FALSE(ed.focusable());
    size_t i = ed.add_tab("a.cpp", "cpp", "int x;\n");
    EXPECT_EQ(i, 0u);
    EXPECT_EQ(ed.active_index(), 0u);
    EXPECT_EQ(ed.tab_count(), 1u);
    EXPECT_TRUE(ed.focusable());
    EXPECT_EQ(ed.active_editor()->text(), "int x;\n");
}

TEST(CodeEditor, EditingActiveTabMarksItDirtyAndCallsCallback) {
    CodeEditor ed;
    ed.add_tab("a.txt", "", "hello");
    size_t modified_index = 999;
    ed.set_on_tab_modified([&](size_t i) { modified_index = i; });

    ed.active_editor()->on_key(KeyEvent{Key::Char, U'!'});
    EXPECT_TRUE(ed.is_dirty(0));
    EXPECT_EQ(modified_index, 0u);
}

TEST(CodeEditor, MarkSavedClearsDirtyFlag) {
    CodeEditor ed;
    ed.add_tab("a.txt", "", "hello");
    ed.active_editor()->on_key(KeyEvent{Key::Char, U'!'});
    ASSERT_TRUE(ed.is_dirty(0));
    ed.mark_saved(0);
    EXPECT_FALSE(ed.is_dirty(0));
}

TEST(CodeEditor, CloseTabShiftsActiveIndexAndCanEmptyOut) {
    CodeEditor ed;
    ed.add_tab("a.txt", "", "a");
    ed.add_tab("b.txt", "", "b");
    ed.set_active_index(1);
    ed.close_tab(1);
    EXPECT_EQ(ed.tab_count(), 1u);
    EXPECT_EQ(ed.active_index(), 0u);
    ed.close_tab(0);
    EXPECT_EQ(ed.tab_count(), 0u);
    EXPECT_FALSE(ed.focusable());
}

TEST(CodeEditor, CtrlPageDownCyclesActiveTabWhenCodeEditorItselfIsFocused) {
    CodeEditor ed;
    ed.add_tab("a.txt", "", "a");
    ed.add_tab("b.txt", "", "b");
    ASSERT_EQ(ed.active_index(), 1u); // add_tab active toujours le dernier ajouté
    ed.set_active_index(0);
    EXPECT_TRUE(ed.on_key(KeyEvent{Key::PageDown, 0, false, true, false}));
    EXPECT_EQ(ed.active_index(), 1u);
}

TEST(CodeEditor, SyntaxHighlightColorsKeywordViaStyleHook) {
    CodeEditor ed;
    ed.syntax_theme().set_color(TokenRole::Keyword, Color{9, 8, 7});
    ed.add_tab("a.cpp", "cpp", "return 0;\n");
    ed.arrange(Rect{0, 0, 40, 10});
    Buffer buf(40, 10);
    ed.paint(buf);
    // "return" commence apres la gouttiere (largeur variable) ; on cherche
    // simplement une cellule peinte avec la couleur configuree pour Keyword.
    bool found = false;
    for (int x = 0; x < 40 && !found; ++x) {
        if (buf.at(x, 1).fg == Color{9, 8, 7}) found = true;
    }
    EXPECT_TRUE(found);
}

TEST(CodeEditor, PaintsWithoutCrashingWhenNoTabsOpen) {
    CodeEditor ed;
    ed.arrange(Rect{0, 0, 30, 10});
    Buffer buf(30, 10);
    ed.paint(buf); // ne doit pas planter (etat "aucun fichier ouvert")
}
