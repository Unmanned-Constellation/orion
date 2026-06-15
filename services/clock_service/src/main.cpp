#include "orion/app/clock_service.hpp"
#include "orion/app/frame_scheduler.hpp"
#include "orion/app/service_bootstrapper.hpp"
#include "orion/transport/session.hpp"

// NOLINTNEXTLINE(bugprone-exception-escape)
auto main(int argc, char** argv) -> int
{
    auto scale   = 1.0;
    auto rate_hz = 100.0;

    auto bootstrap = orion::app::ServiceBootstrapper{"clock-service"};
    bootstrap.withOptions([&](CLI::App& app) {
        app.add_option("--scale", scale, "Sim speed relative to wall time")->envname("SIM_SCALE");
        app.add_option("--rate-hz", rate_hz, "Publish rate in Hz")->envname("SIM_RATE_HZ");
    });
    auto ctx = bootstrap.run(argc, argv);

    ctx.log->info("starting — vehicle={} scale={} rate_hz={}", ctx.vehicle_id, scale, rate_hz);

    auto session   = orion::transport::Session::create({ctx.vehicle_id, ctx.service_name});
    auto scheduler = orion::app::FrameScheduler{rate_hz, ctx.clock, ctx.latch};
    auto svc       = orion::app::ClockService::create( // NOLINT(misc-const-correctness)
        scale,
        ctx.vehicle_id,
        session,
        scheduler);

    ctx.log->info("running");
    scheduler.run();
    ctx.log->info("stopped");
}
