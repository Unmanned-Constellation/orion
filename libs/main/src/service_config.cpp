#include "orion/app/service_config.hpp"

#include <array>
#include <cstddef>
#include <string>

#include <unistd.h>

#include <CLI/CLI.hpp>

namespace orion::app
{

namespace
{

// POSIX guarantees hostnames fit in 255 bytes; use a safe fixed buffer.
constexpr std::size_t MAX_HOSTNAME = 256;

auto hostnameOrEmpty() -> std::string
{
    auto buf = std::array<char, MAX_HOSTNAME>{};
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    if (gethostname(buf.data(), buf.size()) == 0)
    {
        return std::string{buf.data()};
    }
    return {};
}

} // namespace

void addServiceConfig(CLI::App& app, ServiceConfig& cfg)
{
    cfg.vehicle_id = hostnameOrEmpty();

    app.add_option("--vehicle-id", cfg.vehicle_id, "Vehicle identifier (defaults to hostname)")
        ->envname("VEHICLE_ID");
    app.add_option("--log-level", cfg.log_level, "Log level (trace/debug/info/warn/error)")
        ->envname("LOG_LEVEL");
    app.add_option("--clock", cfg.clock_mode, "Time source: wall, sim:<scale>, or coordinated")
        ->envname("CLOCK_MODE");
}

} // namespace orion::app
