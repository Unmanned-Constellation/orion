#pragma once

#include <string>
#include <vector>

#include "orion/transport/publisher.hpp"
#include "orion/transport/subscriber.hpp"

namespace orion::transport::test
{

/// Captures every serialized Envelope passed to send(). Use with Publisher<T> in service tests.
class FakePublisherBackend final : public PublisherBackend
{
  public:
    void send(std::string_view bytes) override { sent_.emplace_back(bytes); }

    [[nodiscard]] auto sent() const -> const std::vector<std::string>& { return sent_; }
    [[nodiscard]] auto sentCount() const -> std::size_t { return sent_.size(); }

  private:
    std::vector<std::string> sent_;
};

/// Holds a RawCallback and lets tests inject raw Envelope bytes to trigger it.
/// Construct with makeRawCallback<T>() to get typed delivery without a real session.
class FakeSubscriberBackend final : public SubscriberBackend
{
  public:
    explicit FakeSubscriberBackend(RawCallback callback) : callback_(std::move(callback)) {}

    void inject(std::string_view bytes) { callback_(bytes); }

  private:
    RawCallback callback_;
};

} // namespace orion::transport::test
