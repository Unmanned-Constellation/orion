#pragma once

#include <functional>
#include <memory>
#include <string_view>

#include "orion/transport/message_header.hpp"

namespace orion::transport
{

using RawCallback = std::function<void(std::string_view)>;

/// Non-template backend — holds the transport subscription handle and the type-erased raw callback.
/// Implemented in session_impl.cpp. Kept alive as long as Subscriber<T> lives.
class SubscriberBackend
{
  public:
    /// @cond
    SubscriberBackend()                                            = default;
    SubscriberBackend(const SubscriberBackend&)                    = default;
    auto operator=(const SubscriberBackend&) -> SubscriberBackend& = default;
    SubscriberBackend(SubscriberBackend&&)                         = default;
    auto operator=(SubscriberBackend&&) -> SubscriberBackend&      = default;
    virtual ~SubscriberBackend()                                   = default;
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
    explicit Subscriber(std::unique_ptr<SubscriberBackend> backend) : backend_(std::move(backend))
    {
    }

    Subscriber(const Subscriber&)                    = delete;
    auto operator=(const Subscriber&) -> Subscriber& = delete;
    Subscriber(Subscriber&&)                         = default;
    auto operator=(Subscriber&&) -> Subscriber&      = default;
    ~Subscriber()                                    = default;
    /// @endcond

  private:
    std::unique_ptr<SubscriberBackend> backend_;
};

} // namespace orion::transport

// Template implementation — decodes Envelope framing for makeRawCallback<T>.
#include "orion/transport/subscriber_impl.hpp"
