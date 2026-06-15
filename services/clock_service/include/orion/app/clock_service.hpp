#pragma once

#include <memory>
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
/// The time source is injected — production code passes a SimClock via create();
/// tests pass the same ManualClock that drives the FrameScheduler, making
/// sim_time_ns deterministic and exactly assertable.
///
/// @see ADR-0009
class ClockService
{
  public:
    /// Constructs a ClockService that publishes at the FrameScheduler tick rate.
    ///
    /// @param time_source  Time source queried on every tick. Shared lifetime with callers.
    /// @param scale        Simulation speed relative to wall time. Stamped on each message.
    /// @param publisher    Publisher bound to the sim_time topic. Takes ownership.
    /// @param scheduler    FrameScheduler to register the tick callback on.
    ClockService(std::shared_ptr<orion::clock::TimeSource>             time_source,
                 double                                                scale,
                 orion::transport::Publisher<orion::v1::SimTimeUpdate> publisher,
                 FrameScheduler&                                       scheduler);

    /// Creates a ClockService from a live Session, advertising on the vehicle's sim_time topic.
    ///
    /// Constructs a SimClock internally anchored to wall time at the moment of the call.
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
    std::shared_ptr<orion::clock::TimeSource>             time_source_;
    orion::transport::Publisher<orion::v1::SimTimeUpdate> publisher_;
    double                                                scale_;
};

} // namespace orion::app
