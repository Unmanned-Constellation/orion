#pragma once

#include <functional>
#include <memory>
#include <string_view>

#include "orion/transport/message_header.hpp"

namespace orion::transport
{

using RawCallback = std::function<void(std::string_view)>;

/// RAII handle that keeps a transport subscription alive for the lifetime of Subscriber<T>.
/// No behavioral interface — destroying this cancels the underlying Zenoh subscription.
class SubscriptionHandle
{
  public:
    /// @cond
    SubscriptionHandle()                                             = default;
    SubscriptionHandle(const SubscriptionHandle&)                    = default;
    auto operator=(const SubscriptionHandle&) -> SubscriptionHandle& = default;
    SubscriptionHandle(SubscriptionHandle&&)                         = default;
    auto operator=(SubscriptionHandle&&) -> SubscriptionHandle&      = default;
    virtual ~SubscriptionHandle()                                    = default;
    /// @endcond
};

/// Lifetime handle for a typed subscription on the message bus.
///
/// Obtained via Session::subscribe<T>(). Destroying this object cancels the
/// subscription. Non-copyable; movable. The deserialization and typed callback
/// are baked in at construction — service code only holds the handle.
///
/// @tparam T  Protobuf message type to deserialize and deliver to the callback.
template <typename T>
class Subscriber
{
  public:
    /// Callback signature delivered to every matching message.
    using Callback = std::function<void(const T&, const MessageHeader&)>;

    /// @cond
    explicit Subscriber(std::unique_ptr<SubscriptionHandle> backend) : backend_(std::move(backend))
    {
    }

    Subscriber(const Subscriber&)                    = delete;
    auto operator=(const Subscriber&) -> Subscriber& = delete;
    Subscriber(Subscriber&&)                         = default;
    auto operator=(Subscriber&&) -> Subscriber&      = default;
    ~Subscriber()                                    = default;
    /// @endcond

  private:
    /// @brief Transport backend holding the subscription handle and raw callback.
    std::unique_ptr<SubscriptionHandle> backend_;
};

} // namespace orion::transport

// IWYU pragma: keep — exposes makeRawCallback<T> to consumers of Subscriber<T>.
#include "orion/transport/subscriber_impl.hpp"
