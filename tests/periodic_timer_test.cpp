#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>

#include <gtest/gtest.h>

#include "orion/app/periodic_timer.hpp"
#include "orion/app/shutdown_latch.hpp"
#include "orion/clock/clock.hpp"

using namespace std::chrono_literals;
using orion::app::PeriodicTimer;
using orion::app::ShutdownLatch;
using orion::clock::ManualClock;

namespace
{

constexpr uint64_t K_PERIOD_NS = 10'000'000; // 100 Hz → 10 ms period

// Advances the clock by one period and waits until count reaches expected.
void tick(ManualClock& clock, std::atomic<int>& count, int expected)
{
    clock.advance(K_PERIOD_NS);
    while (count.load(std::memory_order_acquire) < expected)
    {
        std::this_thread::sleep_for(1ms);
    }
}

// Stops the latch and wakes the timer out of sleepUntil, then joins the thread.
void stopAndJoin(ShutdownLatch& latch, ManualClock& clock, std::thread& runner)
{
    latch.stop();
    clock.wake(); // unblock any sleepUntil so the timer sees the stopped flag
    runner.join();
}

} // namespace

TEST(PeriodicTimerTest, CallbackFiresAtEachTick)
{
    ShutdownLatch    latch;
    auto             clock = std::make_shared<ManualClock>(0);
    PeriodicTimer    timer(100.0, clock, &latch);
    std::atomic<int> count{0};

    std::thread runner([&] { timer.run([&] { ++count; }); });

    tick(*clock, count, 1);
    tick(*clock, count, 2);
    tick(*clock, count, 3);

    stopAndJoin(latch, *clock, runner);
    EXPECT_EQ(count.load(), 3);
}

TEST(PeriodicTimerTest, StopsWhenLatchStopped)
{
    ShutdownLatch    latch;
    auto             clock = std::make_shared<ManualClock>(0);
    PeriodicTimer    timer(100.0, clock, &latch);
    std::atomic<int> count{0};

    std::thread runner([&] { timer.run([&] { ++count; }); });

    tick(*clock, count, 1);
    stopAndJoin(latch, *clock, runner);

    EXPECT_GE(count.load(), 1);
}

TEST(PeriodicTimerTest, NoOverrunOnNormalTick)
{
    ShutdownLatch    latch;
    auto             clock = std::make_shared<ManualClock>(0);
    PeriodicTimer    timer(100.0, clock, &latch);
    std::atomic<int> count{0};

    std::thread runner([&] { timer.run([&] { ++count; }); });

    tick(*clock, count, 1);
    tick(*clock, count, 2);
    tick(*clock, count, 3);
    stopAndJoin(latch, *clock, runner);

    EXPECT_EQ(timer.overrunCount(), 0U);
}

TEST(PeriodicTimerTest, OverrunDetectedWhenCallbackExceedsPeriod)
{
    ShutdownLatch    latch;
    auto             clock = std::make_shared<ManualClock>(0);
    PeriodicTimer    timer(100.0, clock, &latch);
    std::atomic<int> count{0};

    // Callback advances the clock past the next deadline — simulates a slow tick.
    auto slow_callback = [&] {
        ++count;
        clock->advance(K_PERIOD_NS * 2); // burn through 2 extra periods
    };

    std::thread runner([&] { timer.run(slow_callback); });

    // Trigger the first tick
    clock->advance(K_PERIOD_NS);
    while (count.load() < 1)
    {
        std::this_thread::sleep_for(1ms);
    }

    stopAndJoin(latch, *clock, runner);
    EXPECT_GE(timer.overrunCount(), 1U);
}

TEST(PeriodicTimerTest, OverrunDoesNotCauseCatchUpBurst)
{
    ShutdownLatch    latch;
    auto             clock = std::make_shared<ManualClock>(0);
    PeriodicTimer    timer(100.0, clock, &latch);
    std::atomic<int> count{0};

    // First callback overruns by 5 periods.
    bool first    = true;
    auto callback = [&] {
        ++count;
        if (first)
        {
            first = false;
            clock->advance(K_PERIOD_NS * 5);
        }
    };

    std::thread runner([&] { timer.run(callback); });

    // Trigger the first tick (overruns internally)
    clock->advance(K_PERIOD_NS);
    while (count.load() < 1)
    {
        std::this_thread::sleep_for(1ms);
    }

    // Give a moment for the timer to re-sleep — should be waiting at
    // (overrun_time + period), not firing immediately multiple times.
    std::this_thread::sleep_for(5ms);
    EXPECT_EQ(count.load(), 1); // no catch-up burst

    // Confirm the timer is still healthy after the overrun.
    tick(*clock, count, 2);

    stopAndJoin(latch, *clock, runner);
}
