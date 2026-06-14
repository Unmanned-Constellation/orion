#pragma once

#include <mutex>
#include <string>
#include <vector>

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

} // namespace orion::transport::test
