#include <gtest/gtest.h>
#include <tui/Buffer.hpp>
#include <tui/terminal/HeadlessTerminalBackend.hpp>

using namespace tui;

namespace {
Cell make_cell(char32_t ch) {
    return Cell{ch, 1, Color::White(), Color::Black(), TextStyle::None};
}
} // namespace

TEST(Buffer, SetAndAtRoundTrip) {
    Buffer b(5, 5);
    b.set(2, 3, make_cell(U'x'));
    EXPECT_EQ(b.at(2, 3).codepoint, U'x');
}

TEST(Buffer, SetOutOfBoundsIsNoOp) {
    Buffer b(5, 5);
    b.set(-1, 0, make_cell(U'x'));
    b.set(0, -1, make_cell(U'x'));
    b.set(5, 0, make_cell(U'x'));
    b.set(0, 5, make_cell(U'x'));
    // Ne doit pas planter ; rien d'autre à vérifier (comportement no-op).
}

TEST(Buffer, ClearFillsAllCells) {
    Buffer b(3, 3);
    b.set(1, 1, make_cell(U'x'));
    b.clear(make_cell(U'.'));
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x)
            EXPECT_EQ(b.at(x, y).codepoint, U'.');
}

TEST(Buffer, ResizeResetsContent) {
    Buffer b(2, 2);
    b.set(0, 0, make_cell(U'x'));
    b.resize(4, 4);
    EXPECT_EQ(b.width(), 4);
    EXPECT_EQ(b.height(), 4);
    EXPECT_EQ(b.at(0, 0).codepoint, U' '); // reblanchi
}

TEST(DoubleBuffer, FirstFrameForcesFullRepaint) {
    HeadlessTerminalBackend backend(10, 5);
    DoubleBuffer db;
    db.resize(10, 5);
    db.back().set(2, 2, make_cell(U'X'));
    size_t drawn = db.diff_and_flush(backend);
    EXPECT_EQ(drawn, 10u * 5u);
    EXPECT_EQ(backend.painted().at(2, 2).codepoint, U'X');
    EXPECT_EQ(backend.flush_calls(), 1u);
}

TEST(DoubleBuffer, UnchangedFrameRedrawsNothing) {
    HeadlessTerminalBackend backend(10, 5);
    DoubleBuffer db;
    db.resize(10, 5);
    db.back().set(2, 2, make_cell(U'X'));
    db.diff_and_flush(backend);

    db.back().set(2, 2, make_cell(U'X')); // même contenu
    backend.reset_call_counters();
    size_t drawn = db.diff_and_flush(backend);

    EXPECT_EQ(drawn, 0u);
    EXPECT_EQ(backend.draw_cell_calls(), 0u);
    EXPECT_EQ(backend.flush_calls(), 0u); // aucune cellule sale => pas de flush
}

TEST(DoubleBuffer, OnlyChangedCellRedraws) {
    HeadlessTerminalBackend backend(10, 5);
    DoubleBuffer db;
    db.resize(10, 5);
    db.diff_and_flush(backend); // établit le premier front_

    db.back().set(5, 3, make_cell(U'Y'));
    backend.reset_call_counters();
    size_t drawn = db.diff_and_flush(backend);

    EXPECT_EQ(drawn, 1u);
    EXPECT_EQ(backend.draw_cell_calls(), 1u);
    EXPECT_EQ(backend.painted().at(5, 3).codepoint, U'Y');
}

TEST(DoubleBuffer, ResizeForcesFullRepaintEvenAtSameSize) {
    HeadlessTerminalBackend backend(10, 5);
    DoubleBuffer db;
    db.resize(10, 5);
    db.diff_and_flush(backend);

    db.resize(10, 5); // même dimensions, mais resize() force quand même
    backend.reset_call_counters();
    size_t drawn = db.diff_and_flush(backend);
    EXPECT_EQ(drawn, 10u * 5u);
}
