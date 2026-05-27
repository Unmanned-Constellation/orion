#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>

#include <gtest/gtest.h>

#include "orion/app/frame_scheduler.hpp"
#include "orion/app/shutdown_latch.hpp"
#include "orion/clock/clock.hpp"

using orion::app::FrameScheduler;
using orion::app::ShutdownLatch;
using orion::clock::ManualClock;

namespace
{

constexpr uint64_t PERIOD_NS = 10'000'000; // 100 Hz → 10 ms period

// Advances the clock by one period and waits until count reaches expected,
// timing out after ~100 ms so a broken implementation fails rather than hangs.
void tick(ManualClock& clock, std::atomic<int>& count, int expected)
{
    clock.advance(PERIOD_NS);
    for (int attempts = 0; attempts < 100; ++attempts)
    {
        if (count.load(std::memory_order_acquire) >= expected)
        {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    ADD_FAILURE() << "tick() timed out waiting for count " << expected;
}

// Waits until count reaches expected without advancing the clock, timing out
// after ~100 ms. Use this for catch-up ticks that fire without a clock advance.
void waitFor(std::atomic<int>& count, int expected)
{
    for (int attempts = 0; attempts < 100; ++attempts)
    {
        if (count.load(std::memory_order_acquire) >= expected)
        {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    ADD_FAILURE() << "waitFor() timed out waiting for count " << expected;
}

// Stops the latch and wakes the clock, then joins the runner thread.
void stopAndJoin(ShutdownLatch& latch, ManualClock& clock, std::thread& runner)
{
    latch.stop();
    clock.wake();
    runner.join();
}

} // namespace

// --- Basic firing -----------------------------------------------------------

TEST(FrameSchedulerTest, SingleCallbackFiresEveryTick)
{
    ShutdownLatch    latch;
    auto             clock = std::make_shared<ManualClock>(0);
    FrameScheduler   sched(100.0, clock, &latch);
    std::atomic<int> count{0};

    sched.every(1, [&] { ++count; });
    auto runner = std::thread([&] { sched.run(); });

    tick(*clock, count, 1);
    tick(*clock, count, 2);
    tick(*clock, count, 3);

    stopAndJoin(latch, *clock, runner);
    EXPECT_EQ(count.load(), 3);
}

TEST(FrameSchedulerTest, CallbackEvery10FiresOncePerTenTicks)
{
    ShutdownLatch    latch;
    auto             clock = std::make_shared<ManualClock>(0);
    FrameScheduler   sched(100.0, clock, &latch);
    std::atomic<int> fast{0}; // every(1) — used as the tick sentinel
    std::atomic<int> slow{0}; // every(10)

    sched.every(1, [&] { ++fast; });
    sched.every(10, [&] { ++slow; });
    auto runner = std::thread([&] { sched.run(); });

    // After 10 ticks the slow callback should have fired exactly once.
    // Use slow as the sentinel on tick 10: once slow >= 1 both callbacks have
    // completed on the scheduler thread, removing the TOCTOU on the two atomics.
    for (int i = 1; i <= 9; ++i)
    {
        tick(*clock, fast, i);
    }
    tick(*clock, slow, 1);
    EXPECT_EQ(slow.load(), 1);

    // After 10 more ticks (20 total) it should have fired twice.
    for (int i = 11; i <= 19; ++i)
    {
        tick(*clock, fast, i);
    }
    tick(*clock, slow, 2);
    EXPECT_EQ(slow.load(), 2);

    stopAndJoin(latch, *clock, runner);
}

TEST(FrameSchedulerTest, MultipleRatesFireCorrectCountsOver100Ticks)
{
    ShutdownLatch    latch;
    auto             clock = std::make_shared<ManualClock>(0);
    FrameScheduler   sched(100.0, clock, &latch);
    std::atomic<int> every1{0};
    std::atomic<int> every10{0};
    std::atomic<int> every100{0};

    sched.every(1, [&] { ++every1; });
    sched.every(10, [&] { ++every10; });
    sched.every(100, [&] { ++every100; });
    auto runner = std::thread([&] { sched.run(); });

    // Use every100 as the sentinel on the final tick: callbacks fire in
    // registration order, so every1 and every10 complete before every100 within
    // each tick. Once every100 >= 1 all three counters are fully written.
    for (int i = 1; i <= 99; ++i)
    {
        tick(*clock, every1, i);
    }
    tick(*clock, every100, 1);

    EXPECT_EQ(every1.load(), 100);
    EXPECT_EQ(every10.load(), 10);
    EXPECT_EQ(every100.load(), 1);

    stopAndJoin(latch, *clock, runner);
}

// --- Ordering ---------------------------------------------------------------

TEST(FrameSchedulerTest, CallbacksFireInRegistrationOrder)
{
    ShutdownLatch    latch;
    auto             clock = std::make_shared<ManualClock>(0);
    FrameScheduler   sched(100.0, clock, &latch);
    std::atomic<int> count{0};
    // Written only on the scheduler thread; read only after join — no data race.
    int last{0};

    sched.every(1, [&] { last = 1; });
    sched.every(1, [&] {
        last = 2;
        ++count;
    });
    auto runner = std::thread([&] { sched.run(); });

    tick(*clock, count, 1);
    tick(*clock, count, 2);
    tick(*clock, count, 3);
    stopAndJoin(latch, *clock, runner);

    // If ordering is respected, callback 2 always runs after callback 1 within
    // each tick, so `last` must be 2 when the loop finishes.
    EXPECT_EQ(last, 2);
}

// --- Shutdown ---------------------------------------------------------------

TEST(FrameSchedulerTest, StopsWhenLatchStopped)
{
    ShutdownLatch    latch;
    auto             clock = std::make_shared<ManualClock>(0);
    FrameScheduler   sched(100.0, clock, &latch);
    std::atomic<int> count{0};

    sched.every(1, [&] { ++count; });
    auto runner = std::thread([&] { sched.run(); });

    tick(*clock, count, 1);
    stopAndJoin(latch, *clock, runner);

    // GE not EQ: the scheduler may have fired additional ticks between the
    // tick() sentinel and stopAndJoin completing.
    EXPECT_GE(count.load(), 1);
}

// --- Overrun ----------------------------------------------------------------

TEST(FrameSchedulerTest, NoOverrunOnNormalTick)
{
    ShutdownLatch    latch;
    auto             clock = std::make_shared<ManualClock>(0);
    FrameScheduler   sched(100.0, clock, &latch);
    std::atomic<int> count{0};

    sched.every(1, [&] { ++count; });
    auto runner = std::thread([&] { sched.run(); });

    tick(*clock, count, 1);
    tick(*clock, count, 2);
    tick(*clock, count, 3);
    stopAndJoin(latch, *clock, runner);

    EXPECT_EQ(sched.overrunCount(), 0U);
}

TEST(FrameSchedulerTest, OverrunDetectedWhenCallbackExceedsPeriod)
{
    ShutdownLatch    latch;
    auto             clock = std::make_shared<ManualClock>(0);
    FrameScheduler   sched(100.0, clock, &latch);
    std::atomic<int> count{0};

    // Callback advances the clock past the next deadline — simulates a slow tick.
    auto slow_callback = [&] {
        ++count;
        clock->advance(PERIOD_NS * 2);
    };

    sched.every(1, slow_callback);
    auto runner = std::thread([&] { sched.run(); });

    tick(*clock, count, 1);

    stopAndJoin(latch, *clock, runner);
    EXPECT_GE(sched.overrunCount(), 1U);
}

TEST(FrameSchedulerTest, OverrunCatchesUpThenResumesSchedule)
{
    ShutdownLatch    latch;
    auto             clock = std::make_shared<ManualClock>(0);
    FrameScheduler   sched(100.0, clock, &latch);
    std::atomic<int> count{0};

    // Tick 1 overruns by 5 periods: clock jumps to 6 * period inside the callback.
    // next_tick_ advances to 2 * period, which is already in the past, so ticks 2–6
    // fire immediately as the scheduler catches up to the clock. Tick 7 requires a
    // normal advance.
    // `first` is written only inside the callback (scheduler thread) and never
    // read by the test thread — no data race; no atomic needed.
    bool first    = true;
    auto callback = [&] {
        ++count;
        if (first)
        {
            first = false;
            clock->advance(PERIOD_NS * 5);
        }
    };

    sched.every(1, callback);
    auto runner = std::thread([&] { sched.run(); });

    clock->advance(PERIOD_NS); // trigger tick 1

    // Ticks 2–6 fire immediately without further clock advance.
    waitFor(count, 6);
    EXPECT_EQ(count.load(), 6);
    EXPECT_EQ(sched.overrunCount(), 1U);

    // next_tick_ has now passed now — normal blocking wait resumes.
    tick(*clock, count, 7);

    stopAndJoin(latch, *clock, runner);
}

// --- Multiple schedulers sharing a clock ------------------------------------

TEST(FrameSchedulerTest, TwoSchedulersShareManualClockAndBothFire)
{
    ShutdownLatch    latch_a;
    ShutdownLatch    latch_b;
    auto             clock = std::make_shared<ManualClock>(0);
    FrameScheduler   sched_a(100.0, clock, &latch_a);
    FrameScheduler   sched_b(100.0, clock, &latch_b);
    std::atomic<int> count_a{0};
    std::atomic<int> count_b{0};

    sched_a.every(1, [&] { ++count_a; });
    sched_b.every(1, [&] { ++count_b; });

    auto runner_a = std::thread([&] { sched_a.run(); });
    auto runner_b = std::thread([&] { sched_b.run(); });

    // A single clock advance wakes both schedulers simultaneously.
    tick(*clock, count_a, 1);
    waitFor(count_b, 1);
    EXPECT_GE(count_a.load(), 1);
    EXPECT_GE(count_b.load(), 1);

    latch_a.stop();
    latch_b.stop();
    clock->wake();
    runner_a.join();
    runner_b.join();
}

// --- RT priority ------------------------------------------------------------

TEST(FrameSchedulerTest, RtPriorityAppliedFalseWhenNotRequested)
{
    ShutdownLatch    latch;
    auto             clock = std::make_shared<ManualClock>(0);
    FrameScheduler   sched(100.0, clock, &latch); // rt_priority defaults to 0
    std::atomic<int> count{0};

    EXPECT_FALSE(sched.rtPriorityApplied()); // false before run()

    sched.every(1, [&] { ++count; });
    auto runner = std::thread([&] { sched.run(); });

    tick(*clock, count, 1);
    stopAndJoin(latch, *clock, runner);

    EXPECT_FALSE(sched.rtPriorityApplied()); // still false — no RT was requested
}

// --- Edge cases -------------------------------------------------------------

TEST(FrameSchedulerTest, EmptySchedulerRunsAndStopsCleanly)
{
    ShutdownLatch  latch;
    auto           clock = std::make_shared<ManualClock>(0);
    FrameScheduler sched(100.0, clock, &latch);

    auto runner = std::thread([&] { sched.run(); });
    stopAndJoin(latch, *clock, runner);
}

// --- First-tick alignment ---------------------------------------------------

TEST(FrameSchedulerTest, FirstTickAlignmentFiresOnTickN)
{
    ShutdownLatch    latch;
    auto             clock = std::make_shared<ManualClock>(0);
    FrameScheduler   sched(100.0, clock, &latch);
    std::atomic<int> fast{0}; // every(1) — tick sentinel
    std::atomic<int> slow{0}; // every(5)

    sched.every(1, [&] { ++fast; });
    sched.every(5, [&] { ++slow; });
    auto runner = std::thread([&] { sched.run(); });

    // Ticks 1–4: every(5) must not have fired yet.
    for (int i = 1; i <= 4; ++i)
    {
        tick(*clock, fast, i);
    }
    EXPECT_EQ(slow.load(), 0);

    // Tick 5: every(5) fires for the first time.
    tick(*clock, slow, 1);
    EXPECT_EQ(slow.load(), 1);

    stopAndJoin(latch, *clock, runner);
}

// --- Programming-error assertions (death tests) ----------------------------

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(FrameSchedulerDeathTest, ZeroDivisorAsserts)
{
    ShutdownLatch  latch;
    auto           clock = std::make_shared<ManualClock>(0);
    FrameScheduler sched(100.0, clock, &latch);

    EXPECT_DEATH(sched.every(0, [] {}), "divisor must be > 0");
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(FrameSchedulerDeathTest, EveryAfterRunAsserts)
{
    ShutdownLatch    latch;
    auto             clock = std::make_shared<ManualClock>(0);
    FrameScheduler   sched(100.0, clock, &latch);
    std::atomic<int> count{0};

    sched.every(1, [&] { ++count; });
    auto runner = std::thread([&] { sched.run(); });

    // Wait until the scheduler has entered its run loop.
    tick(*clock, count, 1);

    // GTest death tests fork. The child inherits the runner thread but terminates
    // via SIGABRT before interacting with it, so the orphaned thread is harmless.
    EXPECT_DEATH(sched.every(1, [] {}), "every\\(\\) called after run\\(\\)");

    stopAndJoin(latch, *clock, runner);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(FrameSchedulerDeathTest, NonDivisorAsserts)
{
    ShutdownLatch  latch;
    auto           clock = std::make_shared<ManualClock>(0);
    FrameScheduler sched(100.0, clock, &latch);

    // 3 does not evenly divide 100.
    EXPECT_DEATH(sched.every(3, [] {}), "divisor must evenly divide");
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(FrameSchedulerDeathTest, NullClockAsserts)
{
    ShutdownLatch latch;
    EXPECT_DEATH(FrameScheduler(100.0, nullptr, &latch), "clock must not be null");
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(FrameSchedulerDeathTest, NullLatchAsserts)
{
    auto clock = std::make_shared<ManualClock>(0);
    EXPECT_DEATH(FrameScheduler(100.0, clock, nullptr), "latch must not be null");
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(FrameSchedulerDeathTest, NonPositiveRateAsserts)
{
    ShutdownLatch latch;
    auto          clock = std::make_shared<ManualClock>(0);
    EXPECT_DEATH(FrameScheduler(0.0, clock, &latch), "rate_hz");
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(FrameSchedulerDeathTest, NegativeRateAsserts)
{
    ShutdownLatch latch;
    auto          clock = std::make_shared<ManualClock>(0);
    EXPECT_DEATH(FrameScheduler(-100.0, clock, &latch), "rate_hz");
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(FrameSchedulerDeathTest, NonIntegerRateAsserts)
{
    ShutdownLatch latch;
    auto          clock = std::make_shared<ManualClock>(0);
    EXPECT_DEATH(FrameScheduler(99.5, clock, &latch), "rate_hz");
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(FrameSchedulerDeathTest, RunCalledTwiceAsserts)
{
    ShutdownLatch    latch;
    auto             clock = std::make_shared<ManualClock>(0);
    FrameScheduler   sched(100.0, clock, &latch);
    std::atomic<int> count{0};

    sched.every(1, [&] { ++count; });
    auto runner = std::thread([&] { sched.run(); });

    tick(*clock, count, 1);

    // GTest death tests fork; the child terminates via SIGABRT immediately.
    EXPECT_DEATH(
        sched.run(),
        "run\\(\\) called more than once"); // NOLINT(readability-function-cognitive-complexity)

    stopAndJoin(latch, *clock, runner);
}
