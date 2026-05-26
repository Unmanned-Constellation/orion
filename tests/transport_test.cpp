#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "orion/transport/config.hpp"
#include "orion/transport/message_header.hpp"
#include "orion/transport/session.hpp" // NOLINT(misc-include-cleaner)

// Minimal protobuf messages used in tests — pulled from the transport's internal protos.
// Real services would use their own domain protos.
#include "orion/v1/envelope.pb.h"
#include "orion/v1/header.pb.h"

namespace
{

orion::transport::SessionConfig makeConfig(const std::string& service_name = "test-service")
{
    return {
        .vehicle_id   = "test-vehicle",
        .service_name = service_name,
    };
}

// Waits up to |timeout| for |flag| to become true. Returns whether it did.
bool waitFor(const std::atomic<bool>&  flag,
             std::chrono::milliseconds timeout = std::chrono::milliseconds{500})
{
    const auto DEADLINE = std::chrono::steady_clock::now() + timeout;
    while (!flag.load())
    {
        if (std::chrono::steady_clock::now() >= DEADLINE)
        {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    return true;
}

} // namespace

// ── RoundtripDelivery ─────────────────────────────────────────────────────────
// Publish a message on a topic and assert the subscriber callback fires with
// the correct field values.
TEST(TransportTest, RoundtripDelivery)
{
    auto session = orion::transport::Session::create(makeConfig());

    std::atomic<bool> received{false}; // NOLINT(misc-const-correctness)
    orion::v1::Header got;

    auto sub = session.subscribe<orion::v1::Header>(
        "orion/test-vehicle/system/roundtrip",
        [&](const orion::v1::Header& msg, const orion::transport::MessageHeader& /*hdr*/) {
            got = msg;
            received.store(true);
        });

    auto pub = session.advertise<orion::v1::Header>("orion/test-vehicle/system/roundtrip");

    orion::v1::Header sent;
    sent.set_source_id("hello");
    sent.set_captured_at_ns(42);
    pub.publish(sent, 0);

    ASSERT_TRUE(waitFor(received))
        << "Subscriber callback did not fire within timeout"; // NOLINT(readability-implicit-bool-conversion)
    EXPECT_EQ(got.source_id(), "hello");
    EXPECT_EQ(got.captured_at_ns(), 42U);
}

// ── HeaderCapturedAtNs ────────────────────────────────────────────────────────
// The transport forwards captured_at_ns from the publish call into MessageHeader.
TEST(TransportTest, HeaderCapturedAtNs)
{
    constexpr uint64_t K_CAPTURED_NS = 123'456'789ULL;
    auto               session       = orion::transport::Session::create(makeConfig());

    std::atomic<bool>               received{false}; // NOLINT(misc-const-correctness)
    orion::transport::MessageHeader got_hdr;         // NOLINT(misc-const-correctness)

    auto sub = session.subscribe<orion::v1::Header>(
        "orion/test-vehicle/system/timestamp",
        [&](const orion::v1::Header& /*msg*/, const orion::transport::MessageHeader& hdr) {
            got_hdr = hdr;
            received.store(true);
        });

    auto pub = session.advertise<orion::v1::Header>("orion/test-vehicle/system/timestamp");
    pub.publish(orion::v1::Header{}, K_CAPTURED_NS);

    ASSERT_TRUE(waitFor(received))
        << "Subscriber callback did not fire within timeout"; // NOLINT(readability-implicit-bool-conversion)
    EXPECT_EQ(got_hdr.captured_at_ns, K_CAPTURED_NS);
}

// ── HeaderSourceId ────────────────────────────────────────────────────────────
// The transport stamps source_id from SessionConfig::service_name.
TEST(TransportTest, HeaderSourceId)
{
    auto session = orion::transport::Session::create(makeConfig("perception-service"));

    std::atomic<bool>               received{false}; // NOLINT(misc-const-correctness)
    orion::transport::MessageHeader got_hdr;         // NOLINT(misc-const-correctness)

    auto sub = session.subscribe<orion::v1::Header>(
        "orion/test-vehicle/system/source",
        [&](const orion::v1::Header& /*msg*/, const orion::transport::MessageHeader& hdr) {
            got_hdr = hdr;
            received.store(true);
        });

    auto pub = session.advertise<orion::v1::Header>("orion/test-vehicle/system/source");
    pub.publish(orion::v1::Header{}, 0);

    ASSERT_TRUE(waitFor(received))
        << "Subscriber callback did not fire within timeout"; // NOLINT(readability-implicit-bool-conversion)
    EXPECT_EQ(got_hdr.source_id, "perception-service");
}

// ── TypeMismatchDropped ───────────────────────────────────────────────────────
// A subscriber for type B must not receive messages published as type A.
// We reuse orion::v1::Header as type A and orion::v1::Envelope as type B.
TEST(TransportTest, TypeMismatchDropped)
{
    auto session = orion::transport::Session::create(makeConfig());

    std::atomic<bool> received{false}; // NOLINT(misc-const-correctness)

    // Subscribe expecting Envelope, but we will publish Header.
    auto sub = session.subscribe<orion::v1::Envelope>(
        "orion/test-vehicle/system/mismatch",
        [&](const orion::v1::Envelope& /*msg*/, const orion::transport::MessageHeader& /*hdr*/) {
            received.store(true);
        });

    auto pub = session.advertise<orion::v1::Header>("orion/test-vehicle/system/mismatch");
    pub.publish(orion::v1::Header{}, 0);

    // Give the message time to arrive — callback must NOT fire.
    std::this_thread::sleep_for(std::chrono::milliseconds{100});
    EXPECT_FALSE(received.load())
        << "Callback fired despite type mismatch"; // NOLINT(readability-implicit-bool-conversion)
}
