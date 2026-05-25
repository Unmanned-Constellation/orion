#include <chrono>
#include <cstdint>
#include <thread>

#include <gtest/gtest.h>

#include "orion/clock/clock.hpp"

using namespace std::chrono_literals;
using orion::clock::ManualClock;
using orion::clock::WallClock;

// --- WallClock ---

TEST(WallClockTest, NowNsIsMonotonic)
{
    WallClock      clock;
    const uint64_t first  = clock.nowNs();
    const uint64_t second = clock.nowNs();
    EXPECT_GE(second, first);
}

TEST(WallClockTest, NowNsMatchesSystemClock)
{
    WallClock      clock;
    const auto     before = std::chrono::system_clock::now();
    const uint64_t now_ns = clock.nowNs();
    const auto     after  = std::chrono::system_clock::now();

    const auto before_ns = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(before.time_since_epoch()).count());
    const auto after_ns = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(after.time_since_epoch()).count());

    EXPECT_GE(now_ns, before_ns);
    EXPECT_LE(now_ns, after_ns);
}

TEST(WallClockTest, SleepUntilPastTargetReturnsImmediately)
{
    WallClock      clock;
    const uint64_t past_ns = clock.nowNs() - 1'000'000; // 1 ms in the past
    const auto     start   = std::chrono::steady_clock::now();
    clock.sleepUntil(past_ns);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_LT(elapsed, 5ms);
}

TEST(WallClockTest, SleepUntilFutureTargetWakesAtOrAfterTarget)
{
    WallClock      clock;
    const uint64_t target_ns = clock.nowNs() + 20'000'000; // 20 ms ahead
    clock.sleepUntil(target_ns);
    EXPECT_GE(clock.nowNs(), target_ns);
}

// --- ManualClock ---

TEST(ManualClockTest, InitialNowNsMatchesConstructorArg)
{
    ManualClock clock(42'000);
    EXPECT_EQ(clock.nowNs(), 42'000u);
}

TEST(ManualClockTest, DefaultInitialTimeIsZero)
{
    ManualClock clock;
    EXPECT_EQ(clock.nowNs(), 0u);
}

TEST(ManualClockTest, AdvanceIncrementsTime)
{
    ManualClock clock(1'000);
    clock.advance(500);
    EXPECT_EQ(clock.nowNs(), 1'500u);
    clock.advance(500);
    EXPECT_EQ(clock.nowNs(), 2'000u);
}

TEST(ManualClockTest, SetNowOverridesTime)
{
    ManualClock clock(1'000);
    clock.setNow(9'999);
    EXPECT_EQ(clock.nowNs(), 9'999u);
}

TEST(ManualClockTest, SetNowBackwardsThrows)
{
    ManualClock clock(1'000);
    EXPECT_THROW(clock.setNow(500), std::invalid_argument);
}

TEST(ManualClockTest, WakeUnblocksSleeperWithoutAdvancingTime)
{
    ManualClock clock(0);

    bool        woken = false;
    std::thread waiter([&] {
        clock.sleepUntil(999'999'999); // far future
        woken = true;
    });

    std::this_thread::sleep_for(10ms);
    EXPECT_FALSE(woken);

    clock.wake();
    waiter.join();
    EXPECT_TRUE(woken);
    EXPECT_EQ(clock.nowNs(), 0u); // time unchanged
}

TEST(ManualClockTest, SleepUntilAlreadyPassedReturnsImmediately)
{
    ManualClock clock(1'000);
    const auto  start = std::chrono::steady_clock::now();
    clock.sleepUntil(500); // target is in the past
    const auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_LT(elapsed, 5ms);
}

TEST(ManualClockTest, SleepUntilBlocksUntilAdvanced)
{
    ManualClock clock(0);

    bool        woken = false;
    std::thread waiter([&] {
        clock.sleepUntil(1'000);
        woken = true;
    });

    std::this_thread::sleep_for(10ms);
    EXPECT_FALSE(woken);

    clock.advance(1'000);
    waiter.join();
    EXPECT_TRUE(woken);
}

TEST(ManualClockTest, SleepUntilBlocksUntilSetNow)
{
    ManualClock clock(0);

    bool        woken = false;
    std::thread waiter([&] {
        clock.sleepUntil(5'000);
        woken = true;
    });

    std::this_thread::sleep_for(10ms);
    EXPECT_FALSE(woken);

    clock.setNow(5'000);
    waiter.join();
    EXPECT_TRUE(woken);
}

TEST(ManualClockTest, MultipleWaitersAllWakeOnAdvance)
{
    ManualClock      clock(0);
    constexpr int    kWaiters = 4;
    std::atomic<int> woken{0};

    std::vector<std::thread> threads;
    threads.reserve(kWaiters);
    for (int i = 0; i < kWaiters; ++i)
    {
        threads.emplace_back([&] {
            clock.sleepUntil(100);
            ++woken;
        });
    }

    std::this_thread::sleep_for(10ms);
    EXPECT_EQ(woken.load(), 0);

    clock.advance(100);
    for (auto& thr : threads)
    {
        thr.join();
    }
    EXPECT_EQ(woken.load(), kWaiters);
}
