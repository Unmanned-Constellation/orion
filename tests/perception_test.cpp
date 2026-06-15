#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "fake_transport.hpp"
#include "orion/perception/fake_perception_backend.hpp"
#include "orion/perception/perception_service.hpp"
#include "orion/transport/publisher.hpp"
#include "orion/v1/detection.pb.h"

using orion::perception::FakePerceptionBackend;
using orion::perception::PerceptionService;
using orion::transport::Publisher;
using orion::transport::test::decode;
using orion::transport::test::FakePublisherBackend;

namespace
{

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
auto makeFrame(std::string camera_id, uint32_t width, uint32_t height) -> orion::v1::DetectionFrame
{
    auto frame = orion::v1::DetectionFrame{};
    frame.set_camera_id(std::move(camera_id));
    frame.set_frame_width(width);
    frame.set_frame_height(height);
    return frame;
}

struct ServiceFixture
{
    FakePublisherBackend* fake_ptr{nullptr};

    auto makeService(FakePerceptionBackend& backend,
                     uint64_t               budget_ns = 33'000'000ULL) -> PerceptionService
    {
        auto fake      = std::make_unique<FakePublisherBackend>();
        fake_ptr       = fake.get();
        auto publisher = Publisher<orion::v1::DetectionFrame>{std::move(fake), "perception"};
        return PerceptionService{backend, std::move(publisher), nullptr, budget_ns};
    }
};

} // namespace

// ── PerceptionService ─────────────────────────────────────────────────────────

TEST(PerceptionServiceTest, RoundTripPreservesDetectionFrame)
{
    auto backend = FakePerceptionBackend{};
    auto fix     = ServiceFixture{};
    auto svc     = fix.makeService(backend); // NOLINT(misc-const-correctness)

    auto  frame = makeFrame("downward", 960, 600);
    auto* det   = frame.add_detections();
    det->set_class_id(3U);
    det->set_confidence(0.91F);
    det->set_track_id(42U);

    svc.start();
    backend.emit(frame, 0U);

    ASSERT_EQ(fix.fake_ptr->sentCount(), 1U);
    auto received = decode<orion::v1::DetectionFrame>(fix.fake_ptr->sent()[0]);
    ASSERT_TRUE(received.has_value());
    const auto& result = *received; // NOLINT(bugprone-unchecked-optional-access)
    EXPECT_EQ(result.msg.camera_id(), "downward");
    EXPECT_EQ(result.msg.frame_width(), 960U);
    EXPECT_EQ(result.msg.frame_height(), 600U);
    ASSERT_EQ(result.msg.detections_size(), 1);
    EXPECT_EQ(result.msg.detections(0).class_id(), 3U);
    EXPECT_FLOAT_EQ(result.msg.detections(0).confidence(), 0.91F);
    EXPECT_EQ(result.msg.detections(0).track_id(), 42U);
}

TEST(PerceptionServiceTest, CapturedAtNsMatchesBackendTimestamp)
{
    auto backend = FakePerceptionBackend{};
    auto fix     = ServiceFixture{};
    auto svc     = fix.makeService(backend); // NOLINT(misc-const-correctness)

    constexpr auto expected_ts = uint64_t{999'000'000ULL};
    svc.start();
    backend.emit(makeFrame("forward", 960, 600), expected_ts);

    ASSERT_EQ(fix.fake_ptr->sentCount(), 1U);
    auto received = decode<orion::v1::DetectionFrame>(fix.fake_ptr->sent()[0]);
    ASSERT_TRUE(received.has_value());
    const auto& result = *received; // NOLINT(bugprone-unchecked-optional-access)
    EXPECT_EQ(result.header.captured_at_ns, expected_ts);
}

// ── Observability ─────────────────────────────────────────────────────────────

TEST(PerceptionServiceTest, FrameCountIsZeroBeforeAnyEmit)
{
    auto backend = FakePerceptionBackend{};
    auto fix     = ServiceFixture{};
    auto svc     = fix.makeService(backend); // NOLINT(misc-const-correctness)
    svc.start();
    EXPECT_EQ(svc.frameCount(), 0U);
}

TEST(PerceptionServiceTest, FrameCountIncrementsPerEmit)
{
    auto backend = FakePerceptionBackend{};
    auto fix     = ServiceFixture{};
    auto svc     = fix.makeService(backend); // NOLINT(misc-const-correctness)
    svc.start();
    backend.emit(makeFrame("cam", 960, 600), 0U);
    backend.emit(makeFrame("cam", 960, 600), 0U);
    EXPECT_EQ(svc.frameCount(), 2U);
}

TEST(PerceptionServiceTest, OverrunCountedWhenLatencyExceedsBudget)
{
    auto backend = FakePerceptionBackend{};
    auto fix     = ServiceFixture{};
    auto svc     = fix.makeService(backend, /*budget_ns=*/10U); // NOLINT(misc-const-correctness)
    svc.start();
    auto frame = makeFrame("cam", 960, 600);
    frame.set_pipeline_latency_ns(100U);
    backend.emit(frame, 0U);
    EXPECT_EQ(svc.overrunCount(), 1U);
}

TEST(PerceptionServiceTest, NoOverrunWhenFrameWithinBudget)
{
    auto backend = FakePerceptionBackend{};
    auto fix     = ServiceFixture{};
    auto svc     = fix.makeService(backend, /*budget_ns=*/1000U); // NOLINT(misc-const-correctness)
    svc.start();
    auto frame = makeFrame("cam", 960, 600);
    frame.set_pipeline_latency_ns(5U);
    backend.emit(frame, 0U);
    EXPECT_EQ(svc.overrunCount(), 0U);
}

// ── Basic wiring ──────────────────────────────────────────────────────────────

TEST(PerceptionServiceTest, EmitTriggersPublish)
{
    auto backend = FakePerceptionBackend{};
    auto fix     = ServiceFixture{};
    auto svc     = fix.makeService(backend); // NOLINT(misc-const-correctness)

    svc.start();
    backend.emit(makeFrame("forward", 960, 600), 0U);

    EXPECT_EQ(fix.fake_ptr->sentCount(), 1U);
}

// ── FakePerceptionBackend ─────────────────────────────────────────────────────

TEST(FakePerceptionBackendTest, EmitBeforeStartIsNoop)
{
    auto backend = FakePerceptionBackend{};
    auto called  = false;
    // No start() — emit should not crash and should not invoke any callback
    backend.emit(makeFrame("forward", 960, 600), 0U);
    EXPECT_FALSE(called);
}

TEST(FakePerceptionBackendTest, StopPreventsCallbackAfterEmit)
{
    auto backend    = FakePerceptionBackend{};
    auto call_count = 0;
    backend.start([&](auto /*frame*/, auto /*ts*/) { ++call_count; });

    backend.emit(makeFrame("forward", 960, 600), 0U);
    backend.stop();
    backend.emit(makeFrame("forward", 960, 600), 0U);

    EXPECT_EQ(call_count, 1);
}

TEST(FakePerceptionBackendTest, ScriptedFramesFireInOrderAtStart)
{
    auto script = std::vector<FakePerceptionBackend::ScriptedFrame>{
        {makeFrame("cam0", 960, 600), 100U},
        {makeFrame("cam1", 960, 600), 200U},
        {makeFrame("cam2", 960, 600), 300U},
    };
    auto backend = FakePerceptionBackend{std::move(script)};

    auto received = std::vector<std::pair<std::string, uint64_t>>{};
    backend.start([&](const auto& frame, auto captured_at_ns) {
        received.emplace_back(frame.camera_id(), captured_at_ns);
    });

    ASSERT_EQ(received.size(), 3U);
    EXPECT_EQ(received[0], std::make_pair(std::string{"cam0"}, uint64_t{100}));
    EXPECT_EQ(received[1], std::make_pair(std::string{"cam1"}, uint64_t{200}));
    EXPECT_EQ(received[2], std::make_pair(std::string{"cam2"}, uint64_t{300}));
}

TEST(FakePerceptionBackendTest, EmitFiresCallbackWithCorrectFrame)
{
    auto backend = FakePerceptionBackend{};

    auto received_frame = orion::v1::DetectionFrame{};
    auto received_ts    = uint64_t{0};
    backend.start([&](auto frame, auto captured_at_ns) {
        received_frame = frame;
        received_ts    = captured_at_ns;
    });

    backend.emit(makeFrame("forward", 960, 600), 12345U);

    EXPECT_EQ(received_frame.frame_width(), 960U);
    EXPECT_EQ(received_frame.frame_height(), 600U);
    EXPECT_EQ(received_frame.camera_id(), "forward");
    EXPECT_EQ(received_ts, 12345U);
}
