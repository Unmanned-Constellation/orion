#include "orion/perception/perception_service.hpp"

namespace orion::perception
{

PerceptionService::PerceptionService(
    PerceptionBackend& backend, orion::transport::Publisher<orion::v1::DetectionFrame> publisher)
    : backend_(&backend), publisher_(std::move(publisher))
{
}

void PerceptionService::start()
{
    backend_->start([this](const orion::v1::DetectionFrame& frame, uint64_t captured_at_ns) {
        publisher_.publish(frame, captured_at_ns);
    });
}

void PerceptionService::stop() { backend_->stop(); }

} // namespace orion::perception
