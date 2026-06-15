#include "orion/app/service_bootstrapper.hpp"

#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <CLI/CLI.hpp>
#include <spdlog/common.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "orion/app/service_config.hpp"
#include "orion/version.hpp"

namespace orion::app
{

namespace
{

constexpr auto        LOG_PATTERN    = "[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] %v";
constexpr std::size_t MAX_FILE_SIZE  = std::size_t{10} * 1024 * 1024;
constexpr std::size_t MAX_FILE_COUNT = 3;

} // namespace

ServiceBootstrapper::ServiceBootstrapper(std::string_view service_name)
    : service_name_{service_name}
{
}

auto ServiceBootstrapper::withOptions(std::function<void(CLI::App&)> opt_fn) -> ServiceBootstrapper&
{
    options_fn_ = std::move(opt_fn);
    return *this;
}

auto ServiceBootstrapper::withLogger(std::shared_ptr<spdlog::logger> logger) -> ServiceBootstrapper&
{
    logger_ = std::move(logger);
    return *this;
}

auto ServiceBootstrapper::buildLogger(spdlog::level::level_enum level)
    -> std::shared_ptr<spdlog::logger>
{
    const auto log_dir = std::filesystem::path{"/var/log/orion"} / service_name_;
    std::filesystem::create_directories(log_dir);
    const auto log_path = log_dir / (service_name_ + ".log");

    auto sinks = std::vector<spdlog::sink_ptr>{
        std::make_shared<spdlog::sinks::stderr_color_sink_mt>(),
        std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            log_path.string(), MAX_FILE_SIZE, MAX_FILE_COUNT),
    };
    for (auto& sink : sinks)
    {
        sink->set_pattern(LOG_PATTERN);
    }

    auto log = std::make_shared<spdlog::logger>(service_name_, sinks.begin(), sinks.end());
    log->set_level(level);
    return log;
}

auto ServiceBootstrapper::run(int argc, const char* const* argv) -> ServiceContext
{
    auto app = CLI::App{std::string{service_name_}};
    app.set_version_flag("--version", ORION_VERSION_STRING);

    auto cfg = ServiceConfig{};
    addServiceConfig(app, cfg);

    if (options_fn_)
    {
        options_fn_(app);
    }

    try
    {
        app.parse(argc, argv);
    }
    catch (const CLI::ParseError& e)
    {
        std::exit(app.exit(e)); // NOLINT(concurrency-mt-unsafe)
    }

    const auto level = spdlog::level::from_str(cfg.log_level);
    auto       log   = logger_ ? logger_ : buildLogger(level);
    log->set_level(level);

    return ServiceContext{std::move(cfg.vehicle_id), service_name_, std::move(log), &latch_};
}

auto ServiceBootstrapper::run(int argc, char** argv) -> ServiceContext
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast)
    return run(argc, (const char* const*)argv);
}

} // namespace orion::app
