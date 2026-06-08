#pragma once

#include <cstdint>
#include <functional>

#include "orion/v1/detection.pb.h"

namespace orion::perception
{

using DetectionCallback = std::function<void(orion::v1::DetectionFrame, uint64_t captured_at_ns)>;

class PerceptionBackend
{
  public:
    /// @cond
    PerceptionBackend()                                            = default;
    PerceptionBackend(const PerceptionBackend&)                    = default;
    auto operator=(const PerceptionBackend&) -> PerceptionBackend& = default;
    PerceptionBackend(PerceptionBackend&&)                         = default;
    auto operator=(PerceptionBackend&&) -> PerceptionBackend&      = default;
    virtual ~PerceptionBackend()                                   = default;
    /// @endcond

    virtual void start(DetectionCallback callback) = 0;
    virtual void stop()                            = 0;
};

} // namespace orion::perception
