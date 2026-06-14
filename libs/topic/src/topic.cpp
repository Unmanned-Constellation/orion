#include "orion/topic/topic.hpp"

#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
void validate(std::string_view vehicle_id)
{
    if (vehicle_id.empty())
    {
        throw std::invalid_argument("orion::topic: vehicle_id must not be empty");
    }
    if (vehicle_id.find('/') != std::string_view::npos)
    {
        throw std::invalid_argument("orion::topic: vehicle_id must not contain '/'");
    }
}
} // namespace

namespace orion::topic::sensing
{

auto detections(std::string_view vehicle_id) -> std::string
{
    validate(vehicle_id);
    return "orion/" + std::string{vehicle_id} + "/sensing/detections";
}

} // namespace orion::topic::sensing

namespace orion::topic::clock
{

auto simTime(std::string_view vehicle_id) -> std::string
{
    validate(vehicle_id);
    return "orion/" + std::string{vehicle_id} + "/clock/sim_time";
}

} // namespace orion::topic::clock
