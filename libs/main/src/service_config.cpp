#include "orion/app/service_config.hpp"

#include <CLI/CLI.hpp>

namespace orion::app
{

void addServiceConfig(CLI::App& app, ServiceConfig& cfg)
{
    app.add_option("--vehicle-id", cfg.vehicle_id, "Vehicle identifier")
        ->envname("VEHICLE_ID")
        ->required();
    app.add_option("--log-level", cfg.log_level, "Log level (trace/debug/info/warn/error)")
        ->envname("LOG_LEVEL");
}

} // namespace orion::app
