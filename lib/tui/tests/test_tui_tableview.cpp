#include <gtest/gtest.h>
#include <tui/widgets/TableView.hpp>

using namespace tui;

namespace {
TableView make_table(int rows) {
    TableView table;
    table.set_columns({
        TableColumn{"Name", LayoutParams::fixed(10)},
        TableColumn{"Value", LayoutParams::stretch(1)},
    });
    table.set_row_count(rows);
    table.set_cell_accessor([](int row, int col) {
        return col == 0 ? ("row" + std::to_string(row)) : std::to_string(row * 10);
    });
    return table;
}
} // namespace

TEST(TableView, FixedAndStretchColumnWidths) {
    TableView table = make_table(5);
    table.arrange(Rect{0, 0, 30, 10});
    Buffer buf(30, 10);
    table.paint(buf);
    // Colonne "Name" fixe à 10 -> "Value" commence à x=10.
    EXPECT_EQ(buf.at(0, 0).codepoint, U'N'); // "Name"
    EXPECT_EQ(buf.at(10, 0).codepoint, U'V'); // "Value"
}

TEST(TableView, DownArrowMovesSelectionAndScrolls) {
    TableView table = make_table(50);
    table.arrange(Rect{0, 0, 30, 10}); // 9 lignes de données visibles (1 pour l'en-tête)
    for (int i = 0; i < 30; ++i) table.on_key(KeyEvent{Key::Down});
    EXPECT_EQ(table.selected_row(), 30);
}

TEST(TableView, HeaderStaysPinnedWhileBodyScrolls) {
    TableView table = make_table(50);
    table.arrange(Rect{0, 0, 30, 10});
    for (int i = 0; i < 40; ++i) table.on_key(KeyEvent{Key::Down});
    Buffer buf(30, 10);
    table.paint(buf);
    EXPECT_EQ(buf.at(0, 0).codepoint, U'N'); // l'en-tête reste sur la ligne 0
}

TEST(TableView, HomeEndJumpToFirstLastRow) {
    TableView table = make_table(20);
    table.arrange(Rect{0, 0, 30, 5});
    table.on_key(KeyEvent{Key::End});
    EXPECT_EQ(table.selected_row(), 19);
    table.on_key(KeyEvent{Key::Home});
    EXPECT_EQ(table.selected_row(), 0);
}

TEST(TableView, EmptyTableIsNotFocusable) {
    TableView table = make_table(0);
    EXPECT_FALSE(table.focusable());
}

TEST(TableView, EnterFiresOnActivate) {
    TableView table = make_table(5);
    int activated_row = -1;
    table.set_on_activate([&](int row) { activated_row = row; });
    table.on_key(KeyEvent{Key::Down});
    table.on_key(KeyEvent{Key::Enter});
    EXPECT_EQ(activated_row, 1);
}

TEST(TableView, CellAccessorSuppliesRowData) {
    TableView table = make_table(3);
    table.arrange(Rect{0, 0, 30, 10});
    Buffer buf(30, 10);
    table.paint(buf);
    EXPECT_EQ(buf.at(0, 1).codepoint, U'r'); // "row0" sur la première ligne de données
}
