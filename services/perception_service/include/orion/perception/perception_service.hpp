#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

#include <spdlog/logger.h>

#include "orion/perception/perception_backend.hpp"
#include "orion/transport/publisher.hpp"
#include "orion/v1/detection.pb.h"

namespace orion::perception
{

/// Reactive perception microservice.
///
/// Owns the Zenoh publisher and wires the PerceptionBackend callback to it.
/// Follows the start/wait/stop lifecycle defined in ADR-0016. Counts frames
/// and detects pipeline latency overruns; logs per-frame at debug level and
/// warns when pipeline_latency_ns exceeds latency_budget_ns.
class PerceptionService
{
  public:
    /// @param backend           Detection source. Must outlive this service.
    /// @param publisher         Zenoh publisher for DetectionFrame messages.
    /// @param log               Logger for per-frame and overrun messages. Null = silent.
    /// @param latency_budget_ns Per-frame latency threshold for overrun detection.
    PerceptionService(PerceptionBackend&                                     backend,
                      orion::transport::Publisher<orion::v1::DetectionFrame> publisher,
                      std::shared_ptr<spdlog::logger>                        log = nullptr,
                      uint64_t latency_budget_ns                                 = 33'000'000ULL);

    void start();
    void stop();

    /// Total frames published since start().
    [[nodiscard]] auto frameCount() const -> uint64_t;

    /// Frames whose pipeline_latency_ns exceeded latency_budget_ns since start().
    [[nodiscard]] auto overrunCount() const -> uint64_t;

  private:
    PerceptionBackend*                                     backend_;
    orion::transport::Publisher<orion::v1::DetectionFrame> publisher_;
    std::shared_ptr<spdlog::logger>                        log_;
    uint64_t                                               latency_budget_ns_;
    std::atomic<uint64_t>                                  frame_count_{0};
    std::atomic<uint64_t>                                  overrun_count_{0};
};

} // namespace orion::perception
