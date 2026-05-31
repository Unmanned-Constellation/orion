#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include <gtest/gtest.h>

#include "fake_transport.hpp"
#include "orion/transport/message_header.hpp"
#include "orion/transport/publisher.hpp"
#include "orion/transport/session.hpp" // NOLINT(misc-include-cleaner)
#include "orion/transport/subscriber.hpp"
#include "orion/v1/envelope.pb.h"
#include "orion/v1/header.pb.h"

namespace
{

using orion::transport::makeRawCallback;
using orion::transport::MessageHeader;
using orion::transport::Publisher;
using orion::transport::Subscriber;
using orion::transport::test::FakePublisherBackend;
using orion::transport::test::FakeSubscriberBackend;

// Returns a Publisher<Header> backed by a FakePublisherBackend.
// The raw pointer lets tests inspect captured bytes after moving the unique_ptr.
auto makeFakePublisher(FakePublisherBackend*& out_ptr,
                       std::string            source_id = "svc") -> Publisher<orion::v1::Header>
{
    auto fake = std::make_unique<FakePublisherBackend>();
    out_ptr   = fake.get();
    return {std::move(fake), std::move(source_id)};
}

} // namespace

// ── Publisher unit tests ──────────────────────────────────────────────────────
// Verify envelope serialization without a real transport session.

TEST(PublisherTest, SerializesTypeUrl)
{
    FakePublisherBackend* ptr = nullptr;
    auto                  pub = makeFakePublisher(ptr);
    pub.publish(orion::v1::Header{}, 0);

    auto env = orion::v1::Envelope{};
    ASSERT_TRUE(env.ParseFromString(ptr->sent()[0]));
    EXPECT_EQ(env.type_url(), "orion.v1.Header");
}

TEST(PublisherTest, StampsCapturedAtNs)
{
    FakePublisherBackend* ptr = nullptr;
    auto                  pub = makeFakePublisher(ptr);
    pub.publish(orion::v1::Header{}, 123'456'789ULL);

    auto env = orion::v1::Envelope{};
    ASSERT_TRUE(env.ParseFromString(ptr->sent()[0]));
    EXPECT_EQ(env.header().captured_at_ns(), 123'456'789ULL);
}

TEST(PublisherTest, StampsSourceId)
{
    FakePublisherBackend* ptr = nullptr;
    auto                  pub = makeFakePublisher(ptr, "nav-service");
    pub.publish(orion::v1::Header{}, 0);

    auto env = orion::v1::Envelope{};
    ASSERT_TRUE(env.ParseFromString(ptr->sent()[0]));
    EXPECT_EQ(env.header().source_id(), "nav-service");
}

TEST(PublisherTest, EachPublishProducesOneMessage)
{
    FakePublisherBackend* ptr = nullptr;
    auto                  pub = makeFakePublisher(ptr);
    pub.publish(orion::v1::Header{}, 0);
    pub.publish(orion::v1::Header{}, 1);
    pub.publish(orion::v1::Header{}, 2);

    EXPECT_EQ(ptr->sentCount(), 3U);
}

// ── Subscriber unit tests ─────────────────────────────────────────────────────
// Verify envelope decoding and callback dispatch without a real transport session.

TEST(SubscriberTest, DeliversDecodedMessage)
{
    orion::v1::Header received_msg;
    auto              raw = makeRawCallback<orion::v1::Header>(
        [&](const orion::v1::Header& msg, const MessageHeader& /*hdr*/) { received_msg = msg; });

    auto  backend     = std::make_unique<FakeSubscriberBackend>(std::move(raw));
    auto* backend_ptr = backend.get();
    auto  sub         = Subscriber<orion::v1::Header>(std::move(backend));

    FakePublisherBackend* pub_ptr = nullptr;
    auto                  pub     = makeFakePublisher(pub_ptr);
    auto                  msg     = orion::v1::Header{};
    msg.set_source_id("test");
    pub.publish(msg, 0);

    backend_ptr->inject(pub_ptr->sent()[0]);
    EXPECT_EQ(received_msg.source_id(), "test");
}

TEST(SubscriberTest, ForwardsCapturedAtNs)
{
    MessageHeader received_hdr;
    auto          raw = makeRawCallback<orion::v1::Header>(
        [&](const orion::v1::Header& /*msg*/, const MessageHeader& hdr) { received_hdr = hdr; });

    auto  backend     = std::make_unique<FakeSubscriberBackend>(std::move(raw));
    auto* backend_ptr = backend.get();
    auto  sub         = Subscriber<orion::v1::Header>(std::move(backend));

    FakePublisherBackend* pub_ptr = nullptr;
    auto                  pub     = makeFakePublisher(pub_ptr);
    pub.publish(orion::v1::Header{}, 99'000ULL);

    backend_ptr->inject(pub_ptr->sent()[0]);
    EXPECT_EQ(received_hdr.captured_at_ns, 99'000ULL);
}

TEST(SubscriberTest, ForwardsSourceId)
{
    MessageHeader received_hdr;
    auto          raw = makeRawCallback<orion::v1::Header>(
        [&](const orion::v1::Header& /*msg*/, const MessageHeader& hdr) { received_hdr = hdr; });

    auto  backend     = std::make_unique<FakeSubscriberBackend>(std::move(raw));
    auto* backend_ptr = backend.get();
    auto  sub         = Subscriber<orion::v1::Header>(std::move(backend));

    FakePublisherBackend* pub_ptr = nullptr;
    auto                  pub     = makeFakePublisher(pub_ptr, "perception");
    pub.publish(orion::v1::Header{}, 0);

    backend_ptr->inject(pub_ptr->sent()[0]);
    EXPECT_EQ(received_hdr.source_id, "perception");
}

TEST(SubscriberTest, DropsTypeMismatch)
{
    auto called = false;
    auto raw    = makeRawCallback<orion::v1::Envelope>(
        [&](const orion::v1::Envelope& /*msg*/, const MessageHeader& /*hdr*/) { called = true; });

    auto  backend     = std::make_unique<FakeSubscriberBackend>(std::move(raw));
    auto* backend_ptr = backend.get();
    auto  sub         = Subscriber<orion::v1::Envelope>(std::move(backend));

    // Publish a Header but subscribe for Envelope — must be dropped.
    FakePublisherBackend* pub_ptr = nullptr;
    auto                  pub     = makeFakePublisher(pub_ptr);
    pub.publish(orion::v1::Header{}, 0);

    backend_ptr->inject(pub_ptr->sent()[0]);
    EXPECT_FALSE(called);
}

TEST(SubscriberTest, DropsMalformedBytes)
{
    auto called = false;
    auto raw    = makeRawCallback<orion::v1::Header>(
        [&](const orion::v1::Header& /*msg*/, const MessageHeader& /*hdr*/) { called = true; });

    auto  backend     = std::make_unique<FakeSubscriberBackend>(std::move(raw));
    auto* backend_ptr = backend.get();
    auto  sub         = Subscriber<orion::v1::Header>(std::move(backend));

    backend_ptr->inject("not a valid protobuf");
    EXPECT_FALSE(called);
}

// ── Zenoh integration tests ───────────────────────────────────────────────────
// Verify that the Zenoh backend wires publisher and subscriber end-to-end.
// Transport logic is covered by the unit tests above — one roundtrip is enough here.

namespace
{

bool waitFor(const std::atomic<bool>&  flag,
             std::chrono::milliseconds timeout = std::chrono::milliseconds{500})
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

TEST(ZenohSessionTest, RoundtripDelivery) // NOLINT(readability-function-cognitive-complexity)
{
    auto session = orion::transport::Session::create({
        .vehicle_id   = "test-vehicle",
        .service_name = "test-service",
    });

    std::atomic<bool> received{false}; // NOLINT(misc-const-correctness)
    orion::v1::Header got;
    MessageHeader     got_hdr;

    auto sub = session.subscribe<orion::v1::Header>(
        "orion/test-vehicle/system/roundtrip",
        [&](const orion::v1::Header& msg, const MessageHeader& hdr) {
            got     = msg;
            got_hdr = hdr;
            received.store(true);
        });

    auto pub = session.advertise<orion::v1::Header>("orion/test-vehicle/system/roundtrip");

    auto sent = orion::v1::Header{};
    sent.set_source_id("hello");
    pub.publish(sent, 42);

    ASSERT_TRUE(waitFor(received)) << "Subscriber callback did not fire within timeout";
    EXPECT_EQ(got.source_id(), "hello");
    EXPECT_EQ(got_hdr.captured_at_ns, 42U);
    EXPECT_EQ(got_hdr.source_id, "test-service");
}
