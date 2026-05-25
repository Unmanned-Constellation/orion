#include <atomic>
#include <chrono>
#include <csignal>
#include <thread>
#include <vector>

#include <unistd.h>

#include <gtest/gtest.h>

#include "orion/app/shutdown_latch.hpp"

TEST(ShutdownLatchTest, InitiallyNotStopped)
{
    const orion::app::ShutdownLatch LATCH;
    EXPECT_FALSE(LATCH.stopped());
}

TEST(ShutdownLatchTest, StopMakesStoppedTrue)
{
    orion::app::ShutdownLatch latch;
    latch.stop();
    EXPECT_TRUE(latch.stopped());
}

TEST(ShutdownLatchTest, StopIsIdempotent)
{
    orion::app::ShutdownLatch latch;
    latch.stop();
    latch.stop();
    EXPECT_TRUE(latch.stopped());
}

TEST(ShutdownLatchTest, WaitReturnsAfterStop)
{
    orion::app::ShutdownLatch latch;

    std::thread stopper([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
        latch.stop();
    });

    const auto START = std::chrono::steady_clock::now();
    latch.wait();
    const auto ELAPSED = std::chrono::steady_clock::now() - START;

    stopper.join();
    EXPECT_TRUE(latch.stopped());
    EXPECT_GE(ELAPSED, std::chrono::milliseconds{10});
    EXPECT_LT(ELAPSED, std::chrono::milliseconds{500});
}

TEST(ShutdownLatchTest, WaitReturnsImmediatelyIfAlreadyStopped)
{
    orion::app::ShutdownLatch latch;
    latch.stop();

    const auto START = std::chrono::steady_clock::now();
    latch.wait();
    const auto ELAPSED = std::chrono::steady_clock::now() - START;

    EXPECT_LT(ELAPSED, std::chrono::milliseconds{5});
}

TEST(ShutdownLatchTest, MultipleWaitersAllWake)
{
    orion::app::ShutdownLatch latch;
    std::atomic<int>          woken{0};

    std::vector<std::thread> waiters;
    waiters.reserve(4);
    for (int i = 0; i < 4; ++i)
    {
        waiters.emplace_back([&] {
            latch.wait();
            ++woken;
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds{10});
    EXPECT_EQ(woken.load(), 0);

    latch.stop();
    for (auto& thr : waiters)
    {
        thr.join();
    }
    EXPECT_EQ(woken.load(), 4);
}

TEST(ShutdownLatchTest, SigtermTriggersShutdown)
{
    const orion::app::ShutdownLatch LATCH;

    std::thread sender([] {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
        kill(getpid(), SIGTERM); // NOLINT(misc-include-cleaner)
    });

    LATCH.wait();
    sender.join();
    EXPECT_TRUE(LATCH.stopped());
}
