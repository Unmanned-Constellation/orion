#pragma once

#include <string>

#include <CLI/CLI.hpp>

namespace orion::app
{

/// Configuration common to every Orion microservice.
struct ServiceConfig
{
    /// @brief Vehicle identifier. Defaults to the system hostname; overridable via
    ///        --vehicle-id / VEHICLE_ID for dev and testing.
    std::string vehicle_id;
    /// @brief Minimum log level for the service logger. Defaults to "info".
    std::string log_level{"info"};
};

/// Registers --vehicle-id / VEHICLE_ID and --log-level / LOG_LEVEL on @p app.
///
/// Call before app.parse(). The caller adds service-specific options after this
/// returns, then calls CLI11_PARSE or app.parse().
void addServiceConfig(CLI::App& app, ServiceConfig& cfg);

} // namespace orion::app
