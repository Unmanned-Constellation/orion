#include "orion/app/service_bootstrapper.hpp"

#include <cstdlib>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

#include <CLI/CLI.hpp>
#include <spdlog/common.h>

#include "orion/app/logger_factory.hpp"
#include "orion/app/service_config.hpp"
#include "orion/version.hpp"

namespace orion::app
{

ServiceBootstrapper::ServiceBootstrapper(std::string_view service_name)
    : service_name_{service_name}
{
}

auto ServiceBootstrapper::withOptions(std::function<void(CLI::App&)> opt_fn) -> ServiceBootstrapper&
{
    options_fn_ = std::move(opt_fn);
    return *this;
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
    LoggerFactory::init(service_name_, level);
    auto log = LoggerFactory::get(service_name_);

    return ServiceContext{std::move(cfg.vehicle_id), std::move(log), &latch_};
}

auto ServiceBootstrapper::run(int argc, char** argv) -> ServiceContext
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast)
    return run(argc, (const char* const*)argv);
}

} // namespace orion::app
