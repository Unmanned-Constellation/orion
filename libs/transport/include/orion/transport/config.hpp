#pragma once

#include <optional>
#include <string>

namespace orion::transport
{

/// Configuration passed to Session::create.
struct SessionConfig
{
    /// Human-readable vehicle identifier.
    /// Appears as the second segment of every per-vehicle topic.
    std::string vehicle_id;

    /// Name of this service.
    /// Stamped as MessageHeader::source_id on every published message.
    std::string service_name;

    /// Path to a backend-specific config file (Zenoh JSON format).
    /// When absent, the transport backend uses its built-in defaults.
    std::optional<std::string> zenoh_config_path;
};

} // namespace orion::transport
