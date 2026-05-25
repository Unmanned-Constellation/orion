#include <chrono>
#include <csignal>
#include <thread>

#include <sys/types.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include "orion/app/shutdown_latch.hpp"

using namespace std::chrono_literals;

TEST(ShutdownLatchTest, InitiallyNotStopped)
{
    orion::app::ShutdownLatch latch;
    EXPECT_FALSE(latch.stopped());
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
        std::this_thread::sleep_for(10ms);
        latch.stop();
    });

    const auto start = std::chrono::steady_clock::now();
    latch.wait();
    const auto elapsed = std::chrono::steady_clock::now() - start;

    stopper.join();
    EXPECT_TRUE(latch.stopped());
    EXPECT_GE(elapsed, 10ms);
    EXPECT_LT(elapsed, 500ms);
}

TEST(ShutdownLatchTest, WaitReturnsImmediatelyIfAlreadyStopped)
{
    orion::app::ShutdownLatch latch;
    latch.stop();

    const auto start = std::chrono::steady_clock::now();
    latch.wait();
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_LT(elapsed, 5ms);
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

    std::this_thread::sleep_for(10ms);
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
    orion::app::ShutdownLatch latch;

    std::thread sender([] {
        std::this_thread::sleep_for(10ms);
        kill(getpid(), SIGTERM);
    });

    latch.wait();
    sender.join();
    EXPECT_TRUE(latch.stopped());
}
