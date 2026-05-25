#pragma once

#include <cstdint>
#include <string>

namespace orion::transport
{

/// Transport-level metadata delivered to every Subscriber callback.
///
/// Extracted from the internal Envelope by the transport layer; service authors
/// never create or inspect Envelope directly.
struct MessageHeader
{
    /// Nanoseconds since Unix epoch when the transport published this message.
    uint64_t published_at_ns{0};

    /// Service name of the publisher, from SessionConfig::service_name.
    std::string source_id;
};

} // namespace orion::transport
