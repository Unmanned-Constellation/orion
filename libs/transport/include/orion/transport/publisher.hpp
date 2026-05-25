#pragma once

#include <memory>
#include <string>

#include "orion/clock/clock.hpp"

namespace orion::transport
{

/// Non-template backend — implemented in session_impl.cpp, keeps Zenoh out of this header.
class PublisherBackend
{
  public:
    /// @cond
    virtual ~PublisherBackend() = default;
    /// @endcond

    /// Transmits a serialized Envelope on the underlying transport.
    /// @param[in] bytes  Serialized Envelope bytes to transmit.
    virtual void send(std::string_view bytes) = 0;
};

/// Typed handle for publishing messages of type T on a Zenoh topic.
///
/// Obtained via Session::advertise<T>(). Non-copyable; movable.
/// Calling publish() stamps the MessageHeader, wraps the message in an Envelope,
/// and transmits it on the bus.
template <typename T>
class Publisher
{
  public:
    /// @cond
    Publisher(std::unique_ptr<PublisherBackend>    backend,
              std::shared_ptr<orion::clock::Clock> clock,
              std::string                          source_id)
        : backend_(std::move(backend)), clock_(std::move(clock)), source_id_(std::move(source_id))
    {
    }

    Publisher(const Publisher&)                    = delete;
    auto operator=(const Publisher&) -> Publisher& = delete;
    Publisher(Publisher&&)                         = default;
    auto operator=(Publisher&&) -> Publisher&      = default;
    /// @endcond

    /// Serializes @p msg into an Envelope and publishes it on the bus.
    /// @param[in] msg  Message to serialize and publish.
    void publish(const T& msg);

  private:
    std::unique_ptr<PublisherBackend>    backend_;
    std::shared_ptr<orion::clock::Clock> clock_;
    std::string                          source_id_;
};

} // namespace orion::transport

// Template implementation — included here, but only pulls in proto headers (not Zenoh).
#include "orion/transport/publisher_impl.hpp"
