#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "orion/clock/clock.hpp"

using orion::clock::CoordinatedClock;
using orion::clock::ManualClock;
using orion::clock::SimClock;
using orion::clock::WallClock;

// --- WallClock ---

TEST(WallClockTest, NowNsIsMonotonic)
{
    const WallClock clock;
    const uint64_t  first  = clock.nowNs();
    const uint64_t  second = clock.nowNs();
    EXPECT_GE(second, first);
}

TEST(WallClockTest, NowNsMatchesSystemClock)
{
    const WallClock clock;
    const auto      before = std::chrono::system_clock::now();
    const uint64_t  now_ns = clock.nowNs();
    const auto      after  = std::chrono::system_clock::now();

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
    EXPECT_LT(elapsed, std::chrono::milliseconds{5});
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
    const ManualClock clock(42'000);
    EXPECT_EQ(clock.nowNs(), 42'000U);
}

TEST(ManualClockTest, DefaultInitialTimeIsZero)
{
    const ManualClock clock;
    EXPECT_EQ(clock.nowNs(), 0U);
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

    std::this_thread::sleep_for(std::chrono::milliseconds{10});
    EXPECT_FALSE(woken);

    clock.wake();
    waiter.join();
    EXPECT_TRUE(woken);
    EXPECT_EQ(clock.nowNs(), 0U); // time unchanged
}

TEST(ManualClockTest, SleepUntilAlreadyPassedReturnsImmediately)
{
    ManualClock clock(1'000);
    const auto  start = std::chrono::steady_clock::now();
    clock.sleepUntil(500); // target is in the past
    const auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_LT(elapsed, std::chrono::milliseconds{5});
}

TEST(ManualClockTest, SleepUntilBlocksUntilAdvanced)
{
    ManualClock clock(0);

    bool        woken = false;
    std::thread waiter([&] {
        clock.sleepUntil(1'000);
        woken = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds{10});
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

    std::this_thread::sleep_for(std::chrono::milliseconds{10});
    EXPECT_FALSE(woken);

    clock.setNow(5'000);
    waiter.join();
    EXPECT_TRUE(woken);
}

// --- CoordinatedClock ---

TEST(CoordinatedClockTest, DefaultConstructs)
{
    EXPECT_NO_THROW(CoordinatedClock clock); // NOLINT(misc-const-correctness)
}

TEST(CoordinatedClockTest, UpdateThrowsLogicError)
{
    CoordinatedClock clock;
    EXPECT_THROW(clock.update(0), std::logic_error);
}

TEST(CoordinatedClockTest, NowNsThrowsLogicError) // NOLINT(readability-function-size)
{
    const CoordinatedClock clock;
    EXPECT_THROW({ (void)clock.nowNs(); }, std::logic_error);
}

TEST(CoordinatedClockTest, SleepUntilThrowsLogicError)
{
    CoordinatedClock clock;
    EXPECT_THROW(clock.sleepUntil(0), std::logic_error);
}

// --- SimClock ---

TEST(SimClockTest, ZeroScaleThrows) // NOLINT(readability-function-size)
{
    EXPECT_THROW({ const SimClock clock(0.0); }, std::invalid_argument);
}

TEST(SimClockTest, NegativeScaleThrows) // NOLINT(readability-function-size)
{
    EXPECT_THROW({ const SimClock clock(-1.0); }, std::invalid_argument);
}

TEST(SimClockTest, NaNScaleThrows) // NOLINT(readability-function-size)
{
    const double nan_val = std::numeric_limits<double>::quiet_NaN();
    EXPECT_THROW({ const SimClock clock(nan_val); }, std::invalid_argument);
}

TEST(SimClockTest, InfScaleThrows) // NOLINT(readability-function-size)
{
    const double inf_val = std::numeric_limits<double>::infinity();
    EXPECT_THROW({ const SimClock clock(inf_val); }, std::invalid_argument);
}

TEST(SimClockTest, SingleArgConstructorNowNsNearWallTime)
{
    const auto before_ns =
        static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                  std::chrono::system_clock::now().time_since_epoch())
                                  .count());
    const SimClock clock(1.0);
    const auto     after_ns =
        static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                  std::chrono::system_clock::now().time_since_epoch())
                                  .count());

    EXPECT_GE(clock.nowNs(), before_ns);
    EXPECT_LE(clock.nowNs(), after_ns + 5'000'000); // +5 ms for scheduler jitter
}

TEST(SimClockTest, AnchoredNowNsMatchesSimStartAtConstruction)
{
    constexpr uint64_t sim_start = 1'000'000'000ULL; // 1 s
    const SimClock     clock(1.0, sim_start);
    EXPECT_NEAR(static_cast<double>(clock.nowNs()), static_cast<double>(sim_start), 5e6);
}

TEST(SimClockTest, NowNsAdvancesAtScaledRate)
{
    const SimClock clock(2.0, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
    constexpr uint64_t expected_ns = 40'000'000ULL; // 40 ms at 2×
    constexpr uint64_t tolerance   = 10'000'000ULL; // ±10 ms
    EXPECT_NEAR(static_cast<double>(clock.nowNs()),
                static_cast<double>(expected_ns),
                static_cast<double>(tolerance));
}

TEST(SimClockTest, NowNsIsMonotonic)
{
    const SimClock clock(1.0, 0);
    const uint64_t first  = clock.nowNs();
    const uint64_t second = clock.nowNs();
    EXPECT_GE(second, first);
}

TEST(SimClockTest, SleepUntilPastTargetReturnsImmediately)
{
    SimClock   clock(1.0, 0);
    const auto start = std::chrono::steady_clock::now();
    clock.sleepUntil(0); // already past
    const auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_LT(elapsed, std::chrono::milliseconds{5});
}

TEST(SimClockTest, SleepUntilNeverReturnsBeforeTarget)
{
    SimClock           clock(10.0, 0);
    constexpr uint64_t target_ns = 50'000'000ULL; // 50 ms sim = 5 ms wall at 10×
    clock.sleepUntil(target_ns);
    EXPECT_GE(clock.nowNs(), target_ns);
}

TEST(SimClockTest, SleepUntilWallTimeScalesWithFactor)
{
    SimClock           clock(10.0, 0);
    constexpr uint64_t target_ns = 100'000'000ULL; // 100 ms sim = ~10 ms wall at 10×
    const auto         start     = std::chrono::steady_clock::now();
    clock.sleepUntil(target_ns);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_GE(elapsed, std::chrono::milliseconds{5});   // took real wall time
    EXPECT_LT(elapsed, std::chrono::milliseconds{100}); // but much less than sim time
}

// --- ManualClock ---

TEST(ManualClockTest, SleepUntilAfterWakeReturnsImmediately)
{
    ManualClock clock(0);
    clock.wake();

    const auto start = std::chrono::steady_clock::now();
    clock.sleepUntil(999'999'999); // far future — must not block
    const auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_LT(elapsed, std::chrono::milliseconds{5});
}

TEST(ManualClockTest, MultipleWaitersAllWakeOnAdvance)
{
    ManualClock      clock(0);
    constexpr int    num_waiters = 4;
    std::atomic<int> woken{0};

    std::vector<std::thread> threads;
    threads.reserve(num_waiters);
    for (int i = 0; i < num_waiters; ++i)
    {
        threads.emplace_back([&] {
            clock.sleepUntil(100);
            ++woken;
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds{10});
    EXPECT_EQ(woken.load(), 0);

    clock.advance(100);
    for (auto& thr : threads)
    {
        thr.join();
    }
    EXPECT_EQ(woken.load(), num_waiters);
}
