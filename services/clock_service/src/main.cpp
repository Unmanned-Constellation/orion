#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

#include "orion/app/clock_service.hpp"
#include "orion/app/crash_handler.hpp"
#include "orion/app/frame_scheduler.hpp"
#include "orion/app/logger_factory.hpp"
#include "orion/app/shutdown_latch.hpp"
#include "orion/clock/clock.hpp"
#include "orion/transport/session.hpp"

namespace
{

auto requireEnv(const char* name) -> std::string
{
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    const auto* val = std::getenv(name);
    if (val == nullptr || std::string_view{val}.empty())
    {
        throw std::runtime_error(std::string{"Required environment variable not set: "} + name);
    }
    return val;
}

auto optEnvDouble(const char* name, double fallback) -> double
{
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    const auto* val = std::getenv(name);
    if (val == nullptr || std::string_view{val}.empty())
    {
        return fallback;
    }
    return std::stod(val);
}

} // namespace

// NOLINTNEXTLINE(bugprone-exception-escape)
int main()
{
    auto latch = orion::app::ShutdownLatch{};
    auto crash = orion::app::CrashHandler{};

    const auto vehicle_id = requireEnv("ORION_VEHICLE_ID");
    const auto scale      = optEnvDouble("ORION_SIM_SCALE", 1.0);
    const auto rate_hz    = optEnvDouble("ORION_SIM_RATE_HZ", 100.0);

    orion::app::LoggerFactory::init("clock-service");
    auto log = orion::app::LoggerFactory::get("clock-service");
    log->info("starting — vehicle={} scale={} rate_hz={}", vehicle_id, scale, rate_hz);

    auto session   = orion::transport::Session::create({vehicle_id, "clock-service"});
    auto clock     = std::make_shared<orion::clock::WallClock>();
    auto scheduler = orion::app::FrameScheduler{rate_hz, clock, &latch};
    auto svc       = orion::app::ClockService::create(
        scale, vehicle_id, session, scheduler); // NOLINT(misc-const-correctness)

    log->info("running");
    scheduler.run();
    log->info("stopped");
}
