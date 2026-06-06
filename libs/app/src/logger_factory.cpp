#include "orion/app/logger_factory.hpp"

#include <cassert>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <spdlog/common.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace orion::app
{

namespace
{

constexpr auto        LOG_PATTERN    = "[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] %v";
constexpr std::size_t MAX_FILE_SIZE  = std::size_t{10} * 1024 * 1024; // 10 MB
constexpr std::size_t MAX_FILE_COUNT = 3;

struct State
{
    std::vector<std::shared_ptr<spdlog::sinks::sink>>                sinks;
    std::unordered_map<std::string, std::shared_ptr<spdlog::logger>> loggers;
    spdlog::level::level_enum                                        level = spdlog::level::info;
    bool                                                             initialised = false;
    std::mutex                                                       mu;
};

auto state() -> State&
{
    static State ctx;
    return ctx;
}

auto makeLogger(const std::string& name) -> std::shared_ptr<spdlog::logger>
{
    auto& ctx    = state();
    auto  logger = std::make_shared<spdlog::logger>(name, ctx.sinks.begin(), ctx.sinks.end());
    logger->set_level(ctx.level);
    logger->set_pattern(LOG_PATTERN);
    return logger;
}

} // namespace

void LoggerFactory::init(std::string_view service_name, spdlog::level::level_enum level)
{
    auto&                 ctx = state();
    const std::lock_guard lock(ctx.mu);
    assert(!ctx.initialised && "LoggerFactory::init called more than once");

    const auto log_dir = std::filesystem::path("/var/log/orion") / service_name;
    std::filesystem::create_directories(log_dir);
    const auto log_path = log_dir / (std::string(service_name) + ".log");

    ctx.sinks = {
        std::make_shared<spdlog::sinks::stderr_color_sink_mt>(),
        std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            log_path.string(), MAX_FILE_SIZE, MAX_FILE_COUNT),
    };
    for (auto& sink : ctx.sinks)
    {
        sink->set_pattern(LOG_PATTERN);
    }

    ctx.level       = level;
    ctx.initialised = true;
}

void LoggerFactory::initForTest(std::shared_ptr<spdlog::sinks::sink> sink,
                                spdlog::level::level_enum            level)
{
    auto&                 ctx = state();
    const std::lock_guard lock(ctx.mu);
    assert(!ctx.initialised && "LoggerFactory::initForTest called more than once");

    sink->set_pattern(LOG_PATTERN);
    ctx.sinks       = {std::move(sink)};
    ctx.level       = level;
    ctx.initialised = true;
}

auto LoggerFactory::get(std::string_view name) -> std::shared_ptr<spdlog::logger>
{
    auto&                 ctx = state();
    const std::lock_guard lock(ctx.mu);
    assert(ctx.initialised && "LoggerFactory::get called before init");

    auto key = std::string(name);
    auto pos = ctx.loggers.find(key);
    if (pos != ctx.loggers.end())
    {
        return pos->second;
    }

    auto logger      = makeLogger(key);
    ctx.loggers[key] = logger;
    return logger;
}

void LoggerFactory::setLevel(spdlog::level::level_enum level)
{
    auto&                 ctx = state();
    const std::lock_guard lock(ctx.mu);
    ctx.level = level;
    for (auto& [_, logger] : ctx.loggers)
    {
        logger->set_level(level);
    }
}

void LoggerFactory::flush()
{
    auto&                 ctx = state();
    const std::lock_guard lock(ctx.mu);
    for (auto& [_, logger] : ctx.loggers)
    {
        logger->flush();
    }
}

void LoggerFactory::shutdown()
{
    auto&                 ctx = state();
    const std::lock_guard lock(ctx.mu);
    ctx.loggers.clear();
    ctx.sinks.clear();
    ctx.initialised = false;
}

} // namespace orion::app
