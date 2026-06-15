#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include <CLI/CLI.hpp>
#include <spdlog/logger.h>

#include "orion/app/crash_handler.hpp"
#include "orion/app/shutdown_latch.hpp"
#include "orion/clock/clock.hpp"

namespace orion::app
{

/// Resources handed to a microservice after bootstrap completes.
///
/// Returned by ServiceBootstrapper::run(). latch is a non-owning pointer into
/// the ServiceBootstrapper that created this context; the bootstrapper must
/// outlive the context (both are stack variables in main(), so this holds).
struct ServiceContext
{
    /// Vehicle identifier parsed from --vehicle-id / VEHICLE_ID.
    std::string vehicle_id;
    /// Service name passed to the ServiceBootstrapper constructor.
    std::string service_name;
    /// Logger initialised for this service. Non-null.
    std::shared_ptr<spdlog::logger> log;
    /// Non-owning pointer to the bootstrapper's latch. Never null.
    ShutdownLatch* latch;
    /// Time source constructed from --clock / CLOCK_MODE. Non-null.
    std::shared_ptr<orion::clock::TimeSource> clock;
};

/// Owns the shared startup sequence for every Orion microservice.
///
/// Construct once as a stack variable at the top of main(). It owns
/// ShutdownLatch and CrashHandler — do not create those separately. Call
/// withOptions() to register service-specific CLI options before run().
///
/// @code
///   auto bootstrap = orion::app::ServiceBootstrapper{"perception-service"};
///   auto ctx = bootstrap
///       .withOptions([&](CLI::App& app) {
///           app.add_option("--model-engine", engine_path, "...")->required();
///       })
///       .run(argc, argv);
///
///   auto session = orion::transport::Session::create({ctx.vehicle_id, ctx.service_name});
///   // ... construct service, call ctx.latch.wait() ...
/// @endcode
class ServiceBootstrapper
{
  public:
    /// @param service_name  Used as the CLI app name, logger name, and log directory.
    explicit ServiceBootstrapper(std::string_view service_name);

    ServiceBootstrapper(const ServiceBootstrapper&)                    = delete;
    auto operator=(const ServiceBootstrapper&) -> ServiceBootstrapper& = delete;
    ServiceBootstrapper(ServiceBootstrapper&&)                         = delete;
    auto operator=(ServiceBootstrapper&&) -> ServiceBootstrapper&      = delete;
    ~ServiceBootstrapper()                                             = default;

    /// Registers a callback invoked before argument parsing to add service-specific options.
    ///
    /// @param opt_fn  Called with the CLI::App before parse(). Use to add_option / add_flag.
    /// @return *this for chaining.
    auto withOptions(std::function<void(CLI::App&)> opt_fn) -> ServiceBootstrapper&;

    /// Injects a pre-built logger instead of creating one during run().
    ///
    /// Intended for tests: pass a null-sink logger to avoid file I/O and global state.
    /// @param logger  Logger to surface in ServiceContext::log.
    /// @return *this for chaining.
    auto withLogger(std::shared_ptr<spdlog::logger> logger) -> ServiceBootstrapper&;

    /// Parses arguments, initialises the logger, and returns a ready ServiceContext.
    ///
    /// Calls std::exit() if --help, --version, or a parse error is encountered
    /// (same behaviour as CLI11_PARSE in main()).
    ///
    /// @param argc  Argument count from main().
    /// @param argv  Argument vector from main().
    /// @return Initialised ServiceContext.
    auto run(int argc, const char* const* argv) -> ServiceContext;

    /// Overload accepting the mutable argv pointer type delivered by main().
    ///
    /// @param argc  Argument count from main().
    /// @param argv  Argument vector from main().
    /// @return Initialised ServiceContext.
    auto run(int argc, char** argv) -> ServiceContext;

  private:
    auto buildLogger(spdlog::level::level_enum level) -> std::shared_ptr<spdlog::logger>;

    std::string                     service_name_;
    std::function<void(CLI::App&)>  options_fn_;
    std::shared_ptr<spdlog::logger> logger_;
    ShutdownLatch                   latch_;
    CrashHandler                    crash_;
};

} // namespace orion::app
