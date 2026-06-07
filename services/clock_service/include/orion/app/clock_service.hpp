#pragma once

#include <string_view>

#include "orion/app/frame_scheduler.hpp"
#include "orion/clock/clock.hpp"
#include "orion/transport/publisher.hpp"
#include "orion/transport/session.hpp"
#include "orion/v1/sim_time_update.pb.h"

namespace orion::app
{

/// Publishes SimTimeUpdate on every FrameScheduler tick, driving CoordinatedClock services.
///
/// Backed internally by a SimClock — each tick broadcasts the current scaled sim time
/// over the Zenoh bus so all services using CoordinatedClock advance in lockstep.
///
/// @see ADR-0009
class ClockService
{
  public:
    /// Constructs a ClockService that publishes at the FrameScheduler tick rate.
    ///
    /// @param scale      Simulation speed relative to wall time. Must be finite and > 0.
    /// @param publisher  Publisher bound to the sim_time topic. Takes ownership.
    /// @param scheduler  FrameScheduler to register the tick callback on.
    ClockService(double                                                scale,
                 orion::transport::Publisher<orion::v1::SimTimeUpdate> publisher,
                 FrameScheduler&                                       scheduler);

    /// Creates a ClockService from a live Session, advertising on the vehicle's sim_time topic.
    ///
    /// @param scale       Simulation speed relative to wall time.
    /// @param vehicle_id  Vehicle identifier, used to construct the topic path.
    /// @param session     Transport session used to create the publisher.
    /// @param scheduler   FrameScheduler to register the tick callback on.
    static auto create(double                     scale,
                       std::string_view           vehicle_id,
                       orion::transport::Session& session,
                       FrameScheduler&            scheduler) -> ClockService;

  private:
    orion::clock::SimClock                                sim_clock_;
    orion::transport::Publisher<orion::v1::SimTimeUpdate> publisher_;
    double                                                scale_;
};

} // namespace orion::app
