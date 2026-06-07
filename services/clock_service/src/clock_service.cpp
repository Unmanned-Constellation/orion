#include "orion/app/clock_service.hpp"

#include <string>

namespace orion::app
{

ClockService::ClockService(double                                                scale,
                           orion::transport::Publisher<orion::v1::SimTimeUpdate> publisher,
                           FrameScheduler&                                       scheduler)
    : sim_clock_(scale), publisher_(std::move(publisher)), scale_(scale)
{
    scheduler.every(1, [this] {
        auto msg = orion::v1::SimTimeUpdate{};
        msg.set_sim_time_ns(sim_clock_.nowNs());
        msg.set_scale(scale_);
        publisher_.publish(msg, sim_clock_.nowNs());
    });
}

auto ClockService::create(double                     scale,
                          std::string_view           vehicle_id,
                          orion::transport::Session& session,
                          FrameScheduler&            scheduler) -> ClockService
{
    auto topic     = "orion/" + std::string{vehicle_id} + "/clock/sim_time";
    auto publisher = session.advertise<orion::v1::SimTimeUpdate>(topic);
    return ClockService{scale, std::move(publisher), scheduler};
}

} // namespace orion::app
