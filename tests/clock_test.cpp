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
    EXPECT_LT(ELAPSED, std::chrono::milliseconds{5});
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
    const auto  START = std::chrono::steady_clock::now();
    clock.sleepUntil(500); // target is in the past
    const auto ELAPSED = std::chrono::steady_clock::now() - START;
    EXPECT_LT(ELAPSED, std::chrono::milliseconds{5});
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

TEST(CoordinatedClockTest, DefaultConstructs) { EXPECT_NO_THROW(CoordinatedClock clock); }

TEST(CoordinatedClockTest, UpdateThrowsLogicError)
{
    CoordinatedClock clock;
    EXPECT_THROW(clock.update(0), std::logic_error);
}

TEST(CoordinatedClockTest, NowNsThrowsLogicError)
{
    CoordinatedClock clock;
    EXPECT_THROW({ (void)clock.nowNs(); }, std::logic_error);
}

TEST(CoordinatedClockTest, SleepUntilThrowsLogicError)
{
    CoordinatedClock clock;
    EXPECT_THROW(clock.sleepUntil(0), std::logic_error);
}

// --- SimClock ---

TEST(SimClockTest, ZeroScaleThrows)
{
    EXPECT_THROW({ SimClock clock(0.0); }, std::invalid_argument);
}

TEST(SimClockTest, NegativeScaleThrows)
{
    EXPECT_THROW({ SimClock clock(-1.0); }, std::invalid_argument);
}

TEST(SimClockTest, NaNScaleThrows)
{
    const double NAN_VAL = std::numeric_limits<double>::quiet_NaN();
    EXPECT_THROW({ SimClock clock(NAN_VAL); }, std::invalid_argument);
}

TEST(SimClockTest, InfScaleThrows)
{
    const double INF_VAL = std::numeric_limits<double>::infinity();
    EXPECT_THROW({ SimClock clock(INF_VAL); }, std::invalid_argument);
}

TEST(SimClockTest, SingleArgConstructorNowNsNearWallTime)
{
    const auto BEFORE_NS =
        static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                  std::chrono::system_clock::now().time_since_epoch())
                                  .count());
    const SimClock CLOCK(1.0);
    const auto     AFTER_NS =
        static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                  std::chrono::system_clock::now().time_since_epoch())
                                  .count());

    EXPECT_GE(CLOCK.nowNs(), BEFORE_NS);
    EXPECT_LE(CLOCK.nowNs(), AFTER_NS + 5'000'000); // +5 ms for scheduler jitter
}

TEST(SimClockTest, AnchoredNowNsMatchesSimStartAtConstruction)
{
    constexpr uint64_t K_SIM_START = 1'000'000'000ULL; // 1 s
    const SimClock     CLOCK(1.0, K_SIM_START);
    EXPECT_NEAR(static_cast<double>(CLOCK.nowNs()), static_cast<double>(K_SIM_START), 5e6);
}

TEST(SimClockTest, NowNsAdvancesAtScaledRate)
{
    const SimClock CLOCK(2.0, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
    constexpr uint64_t K_EXPECTED_NS = 40'000'000ULL; // 40 ms at 2×
    constexpr uint64_t K_TOLERANCE   = 10'000'000ULL; // ±10 ms
    EXPECT_NEAR(static_cast<double>(CLOCK.nowNs()),
                static_cast<double>(K_EXPECTED_NS),
                static_cast<double>(K_TOLERANCE));
}

TEST(SimClockTest, NowNsIsMonotonic)
{
    const SimClock CLOCK(1.0, 0);
    const uint64_t FIRST  = CLOCK.nowNs();
    const uint64_t SECOND = CLOCK.nowNs();
    EXPECT_GE(SECOND, FIRST);
}

TEST(SimClockTest, SleepUntilPastTargetReturnsImmediately)
{
    SimClock   clock(1.0, 0);
    const auto START = std::chrono::steady_clock::now();
    clock.sleepUntil(0); // already past
    const auto ELAPSED = std::chrono::steady_clock::now() - START;
    EXPECT_LT(ELAPSED, std::chrono::milliseconds{5});
}

TEST(SimClockTest, SleepUntilNeverReturnsBeforeTarget)
{
    SimClock           clock(10.0, 0);
    constexpr uint64_t K_TARGET_NS = 50'000'000ULL; // 50 ms sim = 5 ms wall at 10×
    clock.sleepUntil(K_TARGET_NS);
    EXPECT_GE(clock.nowNs(), K_TARGET_NS);
}

TEST(SimClockTest, SleepUntilWallTimeScalesWithFactor)
{
    SimClock           clock(10.0, 0);
    constexpr uint64_t K_TARGET_NS = 100'000'000ULL; // 100 ms sim = ~10 ms wall at 10×
    const auto         START       = std::chrono::steady_clock::now();
    clock.sleepUntil(K_TARGET_NS);
    const auto ELAPSED = std::chrono::steady_clock::now() - START;
    EXPECT_GE(ELAPSED, std::chrono::milliseconds{5});   // took real wall time
    EXPECT_LT(ELAPSED, std::chrono::milliseconds{100}); // but much less than sim time
}

// --- ManualClock ---

TEST(ManualClockTest, SleepUntilAfterWakeReturnsImmediately)
{
    ManualClock clock(0);
    clock.wake();

    const auto START = std::chrono::steady_clock::now();
    clock.sleepUntil(999'999'999); // far future — must not block
    const auto ELAPSED = std::chrono::steady_clock::now() - START;
    EXPECT_LT(ELAPSED, std::chrono::milliseconds{5});
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

    std::this_thread::sleep_for(std::chrono::milliseconds{10});
    EXPECT_EQ(woken.load(), 0);

    clock.advance(100);
    for (auto& thr : threads)
    {
        thr.join();
    }
    EXPECT_EQ(woken.load(), K_WAITERS);
}
