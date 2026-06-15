#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "fake_transport.hpp"
#include "orion/app/clock_service.hpp"
#include "orion/app/frame_scheduler.hpp"
#include "orion/app/shutdown_latch.hpp"
#include "orion/clock/clock.hpp"
#include "orion/topic/topic.hpp"
#include "orion/transport/publisher.hpp"
#include "orion/transport/session.hpp"
#include "orion/transport/subscriber.hpp"
#include "orion/v1/sim_time_update.pb.h"

using orion::app::ClockService;
using orion::app::FrameScheduler;
using orion::app::ShutdownLatch;
using orion::clock::CoordinatedClock;
using orion::clock::ManualClock;
using orion::clock::WallClock;
using orion::transport::Publisher;
using orion::transport::test::decodeAll;
using orion::transport::test::FakePublisherBackend;

namespace
{

constexpr uint64_t PERIOD_NS = 10'000'000; // 100 Hz

struct ClockServiceFixture
{
    FakePublisherBackend*        fake_ptr{nullptr};
    std::shared_ptr<ManualClock> clock{std::make_shared<ManualClock>(0)};
    ShutdownLatch                latch;
    FrameScheduler               scheduler{100.0, clock, &latch};

    auto makeService(double scale) -> ClockService
    {
        auto fake      = std::make_unique<FakePublisherBackend>();
        fake_ptr       = fake.get();
        auto publisher = Publisher<orion::v1::SimTimeUpdate>{std::move(fake), "clock-service"};
        return ClockService{clock, scale, std::move(publisher), scheduler};
    }
};

} // namespace

TEST(ClockServiceTest, NothingPublishedBeforeFirstTick)
{
    auto fix = ClockServiceFixture{};
    auto svc = fix.makeService(1.0); // NOLINT(misc-const-correctness)
    EXPECT_EQ(fix.fake_ptr->sentCount(), 0U);
}

TEST(ClockServiceTest, PublishesOneMessagePerTick)
{
    auto fix = ClockServiceFixture{};
    auto svc = fix.makeService(1.0); // NOLINT(misc-const-correctness)

    auto runner = std::thread{[&] { fix.scheduler.run(); }};

    fix.clock->advance(PERIOD_NS);
    for (int i = 0; i < 100 && fix.fake_ptr->sentCount() < 1; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    EXPECT_EQ(fix.fake_ptr->sentCount(), 1U);

    fix.latch.stop();
    fix.clock->wake();
    runner.join();
}

TEST(ClockServiceTest, PublishedTimeIsMonotonic)
{
    auto fix = ClockServiceFixture{};
    auto svc = fix.makeService(1.0); // NOLINT(misc-const-correctness)

    auto runner = std::thread{[&] { fix.scheduler.run(); }};

    for (int i = 0; i < 3; ++i)
    {
        fix.clock->advance(PERIOD_NS);
        for (int j = 0; j < 100 && fix.fake_ptr->sentCount() < i + 1; ++j)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
    }

    fix.latch.stop();
    fix.clock->wake();
    runner.join();

    auto msgs = decodeAll<orion::v1::SimTimeUpdate>(*fix.fake_ptr);
    ASSERT_EQ(msgs.size(), 3U);

    EXPECT_GT(msgs[1].msg.sim_time_ns(), msgs[0].msg.sim_time_ns());
    EXPECT_GT(msgs[2].msg.sim_time_ns(), msgs[1].msg.sim_time_ns());
}

TEST(ClockServiceTest, PublishedSimTimeMatchesClockExactly)
{
    auto fix = ClockServiceFixture{};
    auto svc = fix.makeService(1.0); // NOLINT(misc-const-correctness)

    auto runner = std::thread{[&] { fix.scheduler.run(); }};

    fix.clock->advance(PERIOD_NS);
    for (int i = 0; i < 100 && fix.fake_ptr->sentCount() < 1; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    fix.latch.stop();
    fix.clock->wake();
    runner.join();

    auto msgs = decodeAll<orion::v1::SimTimeUpdate>(*fix.fake_ptr);
    ASSERT_EQ(msgs.size(), 1U);
    EXPECT_EQ(msgs[0].msg.sim_time_ns(), PERIOD_NS);
}

TEST(ClockServiceTest, PublishedScaleMatchesConstructorArg)
{
    auto fix = ClockServiceFixture{};
    auto svc = fix.makeService(4.0); // NOLINT(misc-const-correctness)

    auto runner = std::thread{[&] { fix.scheduler.run(); }};

    fix.clock->advance(PERIOD_NS);
    for (int i = 0; i < 100 && fix.fake_ptr->sentCount() < 1; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    fix.latch.stop();
    fix.clock->wake();
    runner.join();

    auto msgs = decodeAll<orion::v1::SimTimeUpdate>(*fix.fake_ptr);
    ASSERT_EQ(msgs.size(), 1U);
    EXPECT_DOUBLE_EQ(msgs[0].msg.scale(), 4.0);
}

// ── Zenoh integration test ────────────────────────────────────────────────────
// Verifies ClockService publishes SimTimeUpdate over a real Zenoh session.
// Transport correctness is covered above — one roundtrip is enough here.

namespace
{

auto waitForFlag(const std::atomic<bool>&  flag,
                 std::chrono::milliseconds timeout = std::chrono::milliseconds{500}) -> bool
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!flag.load())
    {
        if (std::chrono::steady_clock::now() >= deadline)
        {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    return true;
}

} // namespace

// Verifies the coordinated clock wiring pattern: a CoordinatedClock driven by a
// SimTimeUpdate subscriber advances when ClockService publishes.
TEST(ClockServiceZenohTest, CoordinatedClockDrivenBySimTimeUpdate)
{
    auto session = orion::transport::Session::create({
        .vehicle_id   = "test",
        .service_name = "coord-test",
    });

    auto coord_clock = std::make_shared<CoordinatedClock>();
    auto initialized = std::atomic<bool>{false}; // NOLINT(misc-const-correctness)

    // Wiring pattern: subscriber drives coord_clock->update() on every broadcast.
    auto sim_sub = session.subscribe<orion::v1::SimTimeUpdate>(
        orion::topic::clock::simTime("test"),
        [coord_clock, &initialized](const orion::v1::SimTimeUpdate& msg,
                                    const orion::transport::MessageHeader& /*hdr*/) {
            coord_clock->update(msg.sim_time_ns());
            initialized.store(true);
        });

    auto wall_clock = std::make_shared<WallClock>();
    auto latch      = orion::app::ShutdownLatch{};
    auto scheduler  = FrameScheduler{100.0, wall_clock, &latch};
    auto svc        = ClockService::create( // NOLINT(misc-const-correctness)
        1.0,
        "test",
        session,
        scheduler);

    auto runner = std::thread{[&] { scheduler.run(); }};

    ASSERT_TRUE(waitForFlag(initialized)) << "CoordinatedClock never received a SimTimeUpdate";
    EXPECT_GT(coord_clock->nowNs(), 0U);

    latch.stop();
    coord_clock->wake();
    runner.join();
}

TEST(ClockServiceZenohTest, SimTimeUpdateArrivesOverWire)
{
    auto session = orion::transport::Session::create({
        .vehicle_id   = "test",
        .service_name = "clock-service",
    });

    auto received = std::atomic<bool>{false}; // NOLINT(misc-const-correctness)
    auto got      = orion::v1::SimTimeUpdate{};

    auto sub = session.subscribe<orion::v1::SimTimeUpdate>(
        "orion/test/clock/sim_time",
        [&](const orion::v1::SimTimeUpdate& msg, const orion::transport::MessageHeader& /*hdr*/) {
            got = msg;
            received.store(true);
        });

    auto latch     = orion::app::ShutdownLatch{};
    auto clock     = std::make_shared<orion::clock::WallClock>();
    auto scheduler = orion::app::FrameScheduler{100.0, clock, &latch};
    auto svc       = orion::app::ClockService::create(
        2.0, "test", session, scheduler); // NOLINT(misc-const-correctness)

    auto runner = std::thread{[&] { scheduler.run(); }};

    ASSERT_TRUE(waitForFlag(received)) << "SimTimeUpdate did not arrive within timeout";
    EXPECT_GT(got.sim_time_ns(), 0U);
    EXPECT_DOUBLE_EQ(got.scale(), 2.0);

    latch.stop();
    runner.join();
}
