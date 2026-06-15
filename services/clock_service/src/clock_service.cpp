#include "orion/app/clock_service.hpp"

#include <memory>
#include <string>
#include <utility>

#include "orion/topic/topic.hpp"

namespace orion::app
{

ClockService::ClockService(std::shared_ptr<orion::clock::TimeSource>             time_source,
                           double                                                scale,
                           orion::transport::Publisher<orion::v1::SimTimeUpdate> publisher,
                           FrameScheduler&                                       scheduler)
    : time_source_(std::move(time_source)), publisher_(std::move(publisher)), scale_(scale)
{
    scheduler.every(1, [this] {
        const auto now = time_source_->nowNs();
        auto       msg = orion::v1::SimTimeUpdate{};
        msg.set_sim_time_ns(now);
        msg.set_scale(scale_);
        publisher_.publish(msg, now);
    });
}

auto ClockService::create(double                     scale,
                          std::string_view           vehicle_id,
                          orion::transport::Session& session,
                          FrameScheduler&            scheduler) -> ClockService
{
    auto topic       = orion::topic::clock::simTime(vehicle_id);
    auto publisher   = session.advertise<orion::v1::SimTimeUpdate>(topic);
    auto time_source = std::make_shared<orion::clock::SimClock>(scale);
    return ClockService{std::move(time_source), scale, std::move(publisher), scheduler};
}

} // namespace orion::app
