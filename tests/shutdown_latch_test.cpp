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
    const auto latch = orion::app::ShutdownLatch{};
    EXPECT_FALSE(latch.stopped());
}

TEST(ShutdownLatchTest, StopMakesStoppedTrue)
{
    auto latch = orion::app::ShutdownLatch{};
    latch.stop();
    EXPECT_TRUE(latch.stopped());
}

TEST(ShutdownLatchTest, StopIsIdempotent)
{
    auto latch = orion::app::ShutdownLatch{};
    latch.stop();
    latch.stop();
    EXPECT_TRUE(latch.stopped());
}

TEST(ShutdownLatchTest, WaitReturnsAfterStop)
{
    auto latch = orion::app::ShutdownLatch{};

    std::thread stopper([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
        latch.stop();
    });

    const auto start = std::chrono::steady_clock::now();
    latch.wait();
    const auto elapsed = std::chrono::steady_clock::now() - start;

    stopper.join();
    EXPECT_TRUE(latch.stopped());
    EXPECT_GE(elapsed, std::chrono::milliseconds{10});
    EXPECT_LT(elapsed, std::chrono::milliseconds{500});
}

TEST(ShutdownLatchTest, WaitReturnsImmediatelyIfAlreadyStopped)
{
    auto latch = orion::app::ShutdownLatch{};
    latch.stop();

    const auto start = std::chrono::steady_clock::now();
    latch.wait();
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_LT(elapsed, std::chrono::milliseconds{5});
}

TEST(ShutdownLatchTest, MultipleWaitersAllWake)
{
    auto             latch = orion::app::ShutdownLatch{};
    std::atomic<int> woken{0};

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
    const auto latch = orion::app::ShutdownLatch{};

    std::thread sender([] {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
        kill(getpid(), SIGTERM);
    });

    latch.wait();
    sender.join();
    EXPECT_TRUE(latch.stopped());
}
