#include <gtest/gtest.h>
#include <tui/core/Timer.hpp>

using namespace tui;
using Clock = TimerManager::Clock;

namespace {
Clock::time_point at_ms(long ms) {
    return Clock::time_point(std::chrono::milliseconds(ms));
}
} // namespace

TEST(TimerManager, NoTimersMeansNoNextDeadline) {
    TimerManager tm;
    EXPECT_FALSE(tm.time_until_next(at_ms(0)).has_value());
}

TEST(TimerManager, TimeUntilNextComputesRemainingDelay) {
    TimerManager tm;
    tm.add(at_ms(0), std::chrono::milliseconds(100), false, [] {});
    auto remaining = tm.time_until_next(at_ms(30));
    ASSERT_TRUE(remaining.has_value());
    EXPECT_EQ(std::chrono::duration_cast<std::chrono::milliseconds>(*remaining).count(), 70);
}

TEST(TimerManager, OneShotFiresOnceThenIsGone) {
    TimerManager tm;
    int fired = 0;
    tm.add(at_ms(0), std::chrono::milliseconds(50), false, [&] { ++fired; });

    tm.run_due(at_ms(40)); // pas encore dû
    EXPECT_EQ(fired, 0);

    tm.run_due(at_ms(50)); // dû
    EXPECT_EQ(fired, 1);

    tm.run_due(at_ms(1000)); // ne doit plus jamais se redéclencher
    EXPECT_EQ(fired, 1);
    EXPECT_FALSE(tm.time_until_next(at_ms(1000)).has_value());
}

TEST(TimerManager, RepeatingTimerFiresMultipleTimes) {
    TimerManager tm;
    int fired = 0;
    tm.add(at_ms(0), std::chrono::milliseconds(10), true, [&] { ++fired; });

    tm.run_due(at_ms(10));
    EXPECT_EQ(fired, 1);
    tm.run_due(at_ms(20));
    EXPECT_EQ(fired, 2);
    tm.run_due(at_ms(30));
    EXPECT_EQ(fired, 3);
}

TEST(TimerManager, CancelPreventsFutureFires) {
    TimerManager tm;
    int fired = 0;
    TimerId id = tm.add(at_ms(0), std::chrono::milliseconds(10), true, [&] { ++fired; });

    tm.run_due(at_ms(10));
    EXPECT_EQ(fired, 1);

    tm.cancel(id);
    tm.run_due(at_ms(20));
    EXPECT_EQ(fired, 1); // pas de second déclenchement après annulation
}

TEST(TimerManager, CancelUnknownIdIsNoOp) {
    TimerManager tm;
    tm.cancel(TimerId(9999));
    EXPECT_FALSE(tm.time_until_next(at_ms(0)).has_value());
}

TEST(TimerManager, SelfCancelFromWithinCallbackStopsRepeat) {
    TimerManager tm;
    int fired = 0;
    TimerId id{};
    id = tm.add(at_ms(0), std::chrono::milliseconds(10), true, [&] {
        ++fired;
        if (fired == 2) tm.cancel(id);
    });

    tm.run_due(at_ms(10));
    tm.run_due(at_ms(20)); // s'annule ici
    tm.run_due(at_ms(30));
    EXPECT_EQ(fired, 2);
}

TEST(TimerManager, MultipleTimersOrderedByEarliestDeadline) {
    TimerManager tm;
    std::vector<int> order;
    tm.add(at_ms(0), std::chrono::milliseconds(30), false, [&] { order.push_back(1); });
    tm.add(at_ms(0), std::chrono::milliseconds(10), false, [&] { order.push_back(2); });

    auto next = tm.time_until_next(at_ms(0));
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(std::chrono::duration_cast<std::chrono::milliseconds>(*next).count(), 10);

    tm.run_due(at_ms(10));
    EXPECT_EQ(order, (std::vector<int>{2}));
    tm.run_due(at_ms(30));
    EXPECT_EQ(order, (std::vector<int>{2, 1}));
}

TEST(TimerManager, ActiveCountReflectsCancellation) {
    TimerManager tm;
    TimerId a = tm.add(at_ms(0), std::chrono::milliseconds(10), true, [] {});
    tm.add(at_ms(0), std::chrono::milliseconds(20), true, [] {});
    EXPECT_EQ(tm.active_count(), 2u);
    tm.cancel(a);
    EXPECT_EQ(tm.active_count(), 1u);
}

TEST(TimerManager, LargeOvershootDoesNotBurstFire) {
    // Si beaucoup de temps s'est écoulé sans appel à run_due(), un timer
    // répétitif ne doit pas "rattraper" en tirant en rafale : une seule
    // exécution, puis reprogrammation à partir de `now`.
    TimerManager tm;
    int fired = 0;
    tm.add(at_ms(0), std::chrono::milliseconds(10), true, [&] { ++fired; });

    tm.run_due(at_ms(1000)); // très en retard
    EXPECT_EQ(fired, 1);
}
