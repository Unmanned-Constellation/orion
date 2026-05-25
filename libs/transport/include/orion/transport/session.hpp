#pragma once

#include <functional>
#include <memory>
#include <string_view>

#include "orion/clock/clock.hpp"
#include "orion/transport/config.hpp"
#include "orion/transport/publisher.hpp"
#include "orion/transport/subscriber.hpp"

namespace orion::transport
{

class SessionImpl;

/// Entry point to the transport layer. Created once per microservice at startup.
///
/// Factory for Publisher<T> and Subscriber<T> instances. Holds the underlying
/// Zenoh session via pimpl — no Zenoh types appear in this header.
/// Non-copyable; movable.
class Session
{
  public:
    /// Opens a Zenoh session with the given configuration and clock.
    ///
    /// @param[in] config  Vehicle ID, service name, and optional Zenoh config path.
    /// @param[in] clock   Time source used to stamp MessageHeader::published_at_ns.
    /// @return A connected Session ready to advertise and subscribe.
    /// @throws std::runtime_error if the Zenoh session cannot be opened.
    static auto create(SessionConfig                               config,
                       const std::shared_ptr<orion::clock::Clock>& clock) -> Session;

    /// Returns a Publisher that sends messages of type T on @p topic.
    ///
    /// @tparam T           Protobuf message type to publish.
    /// @param[in] topic    Full Zenoh topic key (e.g. "orion/alpha/sensing/detections").
    /// @return A Publisher<T> bound to @p topic.
    template <typename T>
    auto advertise(std::string_view topic) -> Publisher<T>;

    /// Returns a Subscriber that delivers messages of type T from @p topic.
    ///
    /// The subscription is active until the returned Subscriber is destroyed.
    /// Messages whose type_url does not match T are silently dropped.
    ///
    /// @tparam T              Protobuf message type to receive.
    /// @param[in] topic       Full Zenoh topic key expression (wildcards supported).
    /// @param[in] callback    Invoked on the Zenoh thread for each matching message.
    /// @return A Subscriber<T> whose lifetime controls the subscription.
    template <typename T>
    auto subscribe(std::string_view                 topic,
                   typename Subscriber<T>::Callback callback) -> Subscriber<T>;

    /// @cond
    Session(const Session&)                    = delete;
    auto operator=(const Session&) -> Session& = delete;
    Session(Session&&)                         = default;
    auto operator=(Session&&) -> Session&      = default;
    ~Session();
    /// @endcond

  private:
    explicit Session(std::unique_ptr<SessionImpl> impl);

    // Non-template internals — implemented in session_impl.cpp.
    auto makePublisherBackend(std::string_view topic) -> std::unique_ptr<PublisherBackend>;
    auto makeSubscriberBackend(std::string_view topic,
                               RawCallback      callback) -> std::unique_ptr<SubscriberBackend>;

    std::unique_ptr<SessionImpl>         impl_;
    std::shared_ptr<orion::clock::Clock> clock_{nullptr};
    std::string                          source_id_{};
};

} // namespace orion::transport

#include "orion/transport/session_impl.hpp"
