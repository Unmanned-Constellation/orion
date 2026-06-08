#include <CLI/CLI.hpp>

#include "orion/app/clock_service.hpp"
#include "orion/app/crash_handler.hpp"
#include "orion/app/frame_scheduler.hpp"
#include "orion/app/logger_factory.hpp"
#include "orion/app/service_config.hpp"
#include "orion/app/shutdown_latch.hpp"
#include "orion/clock/clock.hpp"
#include "orion/transport/session.hpp"
#include "orion/version.hpp"

// NOLINTNEXTLINE(bugprone-exception-escape)
auto main(int argc, char** argv) -> int
{
    auto latch = orion::app::ShutdownLatch{};
    auto crash = orion::app::CrashHandler{};

    auto app = CLI::App{"clock-service"};
    app.set_version_flag("--version", ORION_VERSION_STRING);

    auto cfg     = orion::app::ServiceConfig{};
    auto scale   = 1.0;
    auto rate_hz = 100.0;

    orion::app::addServiceConfig(app, cfg);
    app.add_option("--scale", scale, "Sim speed relative to wall time")->envname("SIM_SCALE");
    app.add_option("--rate-hz", rate_hz, "Publish rate in Hz")->envname("SIM_RATE_HZ");

    CLI11_PARSE(app, argc, argv);

    orion::app::LoggerFactory::init(cfg.log_level);
    auto log = orion::app::LoggerFactory::get("clock-service");
    log->info("starting — vehicle={} scale={} rate_hz={}", cfg.vehicle_id, scale, rate_hz);

    auto session   = orion::transport::Session::create({cfg.vehicle_id, "clock-service"});
    auto clock     = std::make_shared<orion::clock::WallClock>();
    auto scheduler = orion::app::FrameScheduler{rate_hz, clock, &latch};
    auto svc       = orion::app::ClockService::create( // NOLINT(misc-const-correctness)
        scale,
        cfg.vehicle_id,
        session,
        scheduler);

    log->info("running");
    scheduler.run();
    log->info("stopped");
}
