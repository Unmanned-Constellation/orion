#include <atomic>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "orion/clock/clock.hpp"

using namespace std::chrono_literals;
using orion::clock::ManualClock;
using orion::clock::WallClock;

// --- WallClock ---

TEST(WallClockTest, NowNsIsMonotonic)
{
    const WallClock CLOCK;
    const uint64_t  FIRST  = CLOCK.nowNs();
    const uint64_t  SECOND = CLOCK.nowNs();
    EXPECT_GE(SECOND, FIRST);
}

TEST(WallClockTest, NowNsMatchesSystemClock)
{
    const WallClock CLOCK;
    const auto      BEFORE = std::chrono::system_clock::now();
    const uint64_t  NOW_NS = CLOCK.nowNs();
    const auto      AFTER  = std::chrono::system_clock::now();

    const auto BEFORE_NS = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(BEFORE.time_since_epoch()).count());
    const auto AFTER_NS = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(AFTER.time_since_epoch()).count());

    EXPECT_GE(NOW_NS, BEFORE_NS);
    EXPECT_LE(NOW_NS, AFTER_NS);
}

TEST(WallClockTest, SleepUntilPastTargetReturnsImmediately)
{
    WallClock      clock;
    const uint64_t PAST_NS = clock.nowNs() - 1'000'000; // 1 ms in the past
    const auto     START   = std::chrono::steady_clock::now();
    clock.sleepUntil(PAST_NS);
    const auto ELAPSED = std::chrono::steady_clock::now() - START;
    EXPECT_LT(ELAPSED, 5ms);
}

TEST(WallClockTest, SleepUntilFutureTargetWakesAtOrAfterTarget)
{
    WallClock      clock;
    const uint64_t TARGET_NS = clock.nowNs() + 20'000'000; // 20 ms ahead
    clock.sleepUntil(TARGET_NS);
    EXPECT_GE(clock.nowNs(), TARGET_NS);
}

// --- ManualClock ---

TEST(ManualClockTest, InitialNowNsMatchesConstructorArg)
{
    const ManualClock CLOCK(42'000);
    EXPECT_EQ(CLOCK.nowNs(), 42'000U);
}

TEST(ManualClockTest, DefaultInitialTimeIsZero)
{
    const ManualClock CLOCK;
    EXPECT_EQ(CLOCK.nowNs(), 0U);
}

TEST(ManualClockTest, AdvanceIncrementsTime)
{
    ManualClock clock(1'000);
    clock.advance(500);
    EXPECT_EQ(clock.nowNs(), 1'500U);
    clock.advance(500);
    EXPECT_EQ(clock.nowNs(), 2'000U);
}

TEST(ManualClockTest, SetNowOverridesTime)
{
    ManualClock clock(1'000);
    clock.setNow(9'999);
    EXPECT_EQ(clock.nowNs(), 9'999U);
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
    EXPECT_EQ(clock.nowNs(), 0U); // time unchanged
}

TEST(ManualClockTest, SleepUntilAlreadyPassedReturnsImmediately)
{
    ManualClock clock(1'000);
    const auto  START = std::chrono::steady_clock::now();
    clock.sleepUntil(500); // target is in the past
    const auto ELAPSED = std::chrono::steady_clock::now() - START;
    EXPECT_LT(ELAPSED, 5ms);
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
    constexpr int    K_WAITERS = 4;
    std::atomic<int> woken{0};

    std::vector<std::thread> threads;
    threads.reserve(K_WAITERS);
    for (int i = 0; i < K_WAITERS; ++i)
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
    EXPECT_EQ(woken.load(), K_WAITERS);
}
