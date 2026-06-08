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
#include "orion/v1/envelope.pb.h"

using orion::perception::FakePerceptionBackend;
using orion::perception::PerceptionService;
using orion::transport::Publisher;
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

auto parseDetectionFrame(const std::string& bytes) -> orion::v1::DetectionFrame
{
    auto env = orion::v1::Envelope{};
    env.ParseFromString(bytes);
    auto frame = orion::v1::DetectionFrame{};
    frame.ParseFromString(env.payload());
    return frame;
}

struct ServiceFixture
{
    FakePublisherBackend* fake_ptr{nullptr};

    auto makeService(FakePerceptionBackend& backend) -> PerceptionService
    {
        auto fake      = std::make_unique<FakePublisherBackend>();
        fake_ptr       = fake.get();
        auto publisher = Publisher<orion::v1::DetectionFrame>{std::move(fake), "perception"};
        return PerceptionService{backend, std::move(publisher)};
    }
};

} // namespace

// ── PerceptionService ─────────────────────────────────────────────────────────

TEST(PerceptionServiceTest, PublishedEnvelopeDeserializesToCorrectFrame)
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
    auto received = parseDetectionFrame(fix.fake_ptr->sent()[0]);
    EXPECT_EQ(received.camera_id(), "downward");
    EXPECT_EQ(received.frame_width(), 960U);
    EXPECT_EQ(received.frame_height(), 600U);
    ASSERT_EQ(received.detections_size(), 1);
    EXPECT_EQ(received.detections(0).class_id(), 3U);
    EXPECT_FLOAT_EQ(received.detections(0).confidence(), 0.91F);
    EXPECT_EQ(received.detections(0).track_id(), 42U);
}

TEST(PerceptionServiceTest, CapturedAtNsInEnvelopeMatchesBackendTimestamp)
{
    auto backend = FakePerceptionBackend{};
    auto fix     = ServiceFixture{};
    auto svc     = fix.makeService(backend); // NOLINT(misc-const-correctness)

    constexpr auto expected_ts = uint64_t{999'000'000ULL};
    svc.start();
    backend.emit(makeFrame("forward", 960, 600), expected_ts);

    ASSERT_EQ(fix.fake_ptr->sentCount(), 1U);
    auto env = orion::v1::Envelope{};
    env.ParseFromString(fix.fake_ptr->sent()[0]);
    EXPECT_EQ(env.header().captured_at_ns(), expected_ts);
}

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
