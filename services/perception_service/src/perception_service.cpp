#include "orion/perception/perception_service.hpp"

#include <cstdint>
#include <memory>
#include <utility>

#include <spdlog/logger.h>

namespace orion::perception
{

PerceptionService::PerceptionService(
    PerceptionBackend&                                     backend,
    orion::transport::Publisher<orion::v1::DetectionFrame> publisher,
    std::shared_ptr<spdlog::logger>                        log,
    uint64_t                                               latency_budget_ns)
    : backend_(&backend),
      publisher_(std::move(publisher)),
      log_(std::move(log)),
      latency_budget_ns_(latency_budget_ns)
{
}

void PerceptionService::start()
{
    backend_->start([this](const orion::v1::DetectionFrame& frame, uint64_t captured_at_ns) {
        publisher_.publish(frame, captured_at_ns);
        ++frame_count_;

        const auto latency = frame.pipeline_latency_ns();
        if (latency > latency_budget_ns_)
        {
            ++overrun_count_;
            if (log_)
            {
                log_->warn(
                    "pipeline overrun latency={:.1f}ms budget={:.1f}ms detections={} camera={}",
                    static_cast<double>(latency) / 1'000'000.0,
                    static_cast<double>(latency_budget_ns_) / 1'000'000.0,
                    frame.detections_size(),
                    frame.camera_id());
            }
        }

        if (log_)
        {
            log_->debug("frame camera={} latency={:.1f}ms detections={}",
                        frame.camera_id(),
                        static_cast<double>(latency) / 1'000'000.0,
                        frame.detections_size());
        }
    });
}

void PerceptionService::stop() { backend_->stop(); }

auto PerceptionService::frameCount() const -> uint64_t { return frame_count_.load(); }

auto PerceptionService::overrunCount() const -> uint64_t { return overrun_count_.load(); }

} // namespace orion::perception
