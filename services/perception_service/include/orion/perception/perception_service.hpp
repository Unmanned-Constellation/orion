#pragma once

#include "orion/perception/perception_backend.hpp"
#include "orion/transport/publisher.hpp"
#include "orion/v1/detection.pb.h"

namespace orion::perception
{

/// Reactive perception microservice.
///
/// Owns the Zenoh publisher and wires the PerceptionBackend callback to it.
/// Follows the start/wait/stop lifecycle defined in ADR-0016.
class PerceptionService
{
  public:
    PerceptionService(PerceptionBackend&                                     backend,
                      orion::transport::Publisher<orion::v1::DetectionFrame> publisher);

    void start();
    void stop();

  private:
    PerceptionBackend*                                     backend_;
    orion::transport::Publisher<orion::v1::DetectionFrame> publisher_;
};

} // namespace orion::perception
