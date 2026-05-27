#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace orion::transport
{

/// Non-template backend — implemented in session_impl.cpp, keeps transport internals out of this
/// header.
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

/// Typed handle for publishing messages of type T on the message bus.
///
/// Obtained via Session::advertise<T>(). Non-copyable; movable.
/// Calling publish() wraps the message in an Envelope with the supplied capture
/// timestamp and transmits it on the bus.
///
/// @tparam T  Protobuf message type to serialize and publish.
template <typename T>
class Publisher
{
  public:
    /// @cond
    Publisher(std::unique_ptr<PublisherBackend> backend, std::string source_id)
        : backend_(std::move(backend)), source_id_(std::move(source_id))
    {
    }

    Publisher(const Publisher&)                    = delete;
    auto operator=(const Publisher&) -> Publisher& = delete;
    Publisher(Publisher&&)                         = default;
    auto operator=(Publisher&&) -> Publisher&      = default;
    /// @endcond

    /// Serializes @p msg into an Envelope and publishes it on the bus.
    /// @param[in] msg             Message to serialize and publish.
    /// @param[in] captured_at_ns  Time the underlying data was captured, nanoseconds
    ///                            since the Unix epoch. Supplied by the calling service.
    void publish(const T& msg, uint64_t captured_at_ns);

  private:
    std::unique_ptr<PublisherBackend> backend_;
    std::string                       source_id_;
};

} // namespace orion::transport

// Template implementation — included here, but only pulls in proto headers (not transport backend).
#include "orion/transport/publisher_impl.hpp"
