#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "orion/transport/message_header.hpp"
#include "orion/transport/publisher.hpp"
#include "orion/transport/subscriber.hpp"

namespace orion::transport::test
{

/// Captures every serialized Envelope passed to send(). Use with Publisher<T> in service tests.
/// Thread-safe: send() may be called from a scheduler thread while the test thread polls
/// sentCount().
class FakePublisherBackend final : public PublisherBackend
{
  public:
    void send(std::string_view bytes) override
    {
        auto lock = std::lock_guard{mu_};
        sent_.emplace_back(bytes);
    }

    [[nodiscard]] auto sent() const -> std::vector<std::string>
    {
        auto lock = std::lock_guard{mu_};
        return sent_;
    }

    [[nodiscard]] auto sentCount() const -> std::size_t
    {
        auto lock = std::lock_guard{mu_};
        return sent_.size();
    }

  private:
    mutable std::mutex       mu_;
    std::vector<std::string> sent_;
};

/// Holds a RawCallback and lets tests inject raw Envelope bytes to trigger it.
/// Construct with makeRawCallback<T>() to get typed delivery without a real session.
class FakeSubscriptionHandle final : public SubscriptionHandle
{
  public:
    explicit FakeSubscriptionHandle(RawCallback callback) : callback_(std::move(callback)) {}

    void inject(std::string_view bytes) { callback_(bytes); }

  private:
    RawCallback callback_;
};

/// Typed result of decoding a single serialized message from the transport.
template <typename T>
struct Received
{
    T             msg;
    MessageHeader header;
};

/// Decodes one serialized envelope into a typed message and MessageHeader.
/// Returns nullopt if the bytes are unparseable or the type_url does not match T.
template <typename T>
auto decode(std::string_view bytes) -> std::optional<Received<T>>
{
    auto result   = std::optional<Received<T>>{};
    auto callback = makeRawCallback<T>(
        [&](const T& msg, const MessageHeader& hdr) { result = Received<T>{msg, hdr}; });
    callback(bytes);
    return result;
}

/// Decodes all envelopes captured by a FakePublisherBackend into typed messages.
template <typename T>
auto decodeAll(const FakePublisherBackend& backend) -> std::vector<Received<T>>
{
    auto results  = std::vector<Received<T>>{};
    auto callback = makeRawCallback<T>(
        [&](const T& msg, const MessageHeader& hdr) { results.push_back({msg, hdr}); });
    for (const auto& bytes : backend.sent())
    {
        callback(bytes);
    }
    return results;
}

} // namespace orion::transport::test
