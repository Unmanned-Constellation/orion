#pragma once

#include <memory>
#include <string_view>

#include <spdlog/spdlog.h>

namespace orion::app
{

/// @brief Process-wide factory for named, hierarchical loggers.
///
/// Call `init()` once at process startup (after ShutdownLatch and CrashHandler).
/// All subsequent code obtains loggers via `get()`. Logger names encode
/// component hierarchy using dot-separated segments:
///
/// @code
/// LoggerFactory::init("autonomy");
/// auto log = LoggerFactory::get("autonomy.stabilizer.pid");
/// log->info("running");
/// @endcode
///
/// All loggers within the process write to a shared stderr sink and a rotating
/// file at `/var/log/orion/<service>/<service>.log`.
class LoggerFactory
{
  public:
    /// @brief Initialise with a stderr + rotating-file sink.
    /// @param service_name First segment of all logger names; also names the log directory.
    /// @param level        Default log level (info if omitted).
    static void init(std::string_view          service_name,
                     spdlog::level::level_enum level = spdlog::level::info);

    /// @brief Initialise with a caller-supplied sink (for tests).
    /// @param sink  Synchronous sink to capture output (e.g. ostream_sink_st).
    /// @param level Default log level (trace if omitted, to capture all output in tests).
    static void initForTest(std::shared_ptr<spdlog::sinks::sink> sink,
                            spdlog::level::level_enum            level = spdlog::level::trace);

    /// @brief Return an existing logger by name, or create one if it does not exist.
    /// @param name Dot-separated logger name (e.g. "autonomy.stabilizer.pid").
    /// @return Shared logger writing to the process-wide sinks.
    /// @pre init() or initForTest() must have been called. Asserts false otherwise.
    static auto get(std::string_view name) -> std::shared_ptr<spdlog::logger>;

    /// @brief Set the log level on all registered loggers.
    /// @param level New minimum level; messages below this are discarded.
    static void setLevel(spdlog::level::level_enum level);

    /// @brief Flush all loggers synchronously. Call before process exit.
    static void flush();

    /// @brief Tear down the factory and drop all loggers. Used by tests.
    static void shutdown();

    LoggerFactory()                                = delete;
    LoggerFactory(const LoggerFactory&)            = delete;
    LoggerFactory& operator=(const LoggerFactory&) = delete;
};

} // namespace orion::app
