#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "orion/transport/config.hpp"
#include "orion/transport/publisher.hpp"
#include "orion/transport/subscriber.hpp"

namespace orion::transport
{

class SessionImpl;

/// Entry point to the transport layer. Created once per microservice at startup.
///
/// Factory for Publisher<T> and Subscriber<T> instances. Wraps the underlying
/// transport backend via pimpl — no backend types appear in this header.
/// Non-copyable; movable.
class Session
{
  public:
    /// Opens a transport session with the given configuration.
    ///
    /// @param[in] config  Vehicle ID, service name, and optional backend config path.
    /// @return A connected Session ready to advertise and subscribe.
    /// @throws std::runtime_error if the transport session cannot be opened.
    static auto create(SessionConfig config) -> Session;

    /// Returns a Publisher that sends messages of type T on @p topic.
    ///
    /// @tparam T           Protobuf message type to publish.
    /// @param[in] topic    Full topic key (e.g. "orion/alpha/sensing/detections").
    /// @return A Publisher<T> bound to @p topic.
    template <typename T>
    auto advertise(std::string_view topic) -> Publisher<T>;

    /// Returns a Subscriber that delivers messages of type T from @p topic.
    ///
    /// The subscription is active until the returned Subscriber is destroyed.
    /// Messages whose type_url does not match T are silently dropped.
    ///
    /// @tparam T              Protobuf message type to receive.
    /// @param[in] topic       Full topic key expression (wildcards supported).
    /// @param[in] callback    Invoked on the transport delivery thread for each matching message.
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
    /// @brief Wraps a fully-constructed SessionImpl; called only by create().
    /// @param impl  Fully-constructed backend implementation.
    explicit Session(std::unique_ptr<SessionImpl> impl);

    /// @brief Creates a PublisherBackend for @p topic; implemented in session_impl.cpp.
    /// @param topic  Full topic key.
    /// @return Heap-allocated PublisherBackend bound to the topic.
    auto makePublisherBackend(std::string_view topic) -> std::unique_ptr<PublisherBackend>;

    /// @brief Creates a SubscriptionHandle for @p topic with the given raw callback; implemented in
    /// session_impl.cpp.
    /// @param topic     Full topic key expression.
    /// @param callback  Type-erased callback invoked on each received message.
    /// @return Heap-allocated SubscriptionHandle holding the active subscription.
    auto makeSubscriptionHandle(std::string_view topic,
                                RawCallback      callback) -> std::unique_ptr<SubscriptionHandle>;

    /// @brief Pimpl holding the transport backend state.
    std::unique_ptr<SessionImpl> impl_;
    /// @brief Unique source identifier for this session, stamped into every envelope.
    std::string source_id_{};
};

} // namespace orion::transport

#include "orion/transport/session_impl.hpp"
