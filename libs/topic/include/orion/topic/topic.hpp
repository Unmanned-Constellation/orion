#pragma once

#include <string>
#include <string_view>

namespace orion::topic
{

namespace sensing
{
/// Returns `orion/{vehicle_id}/sensing/detections`.
/// @throws std::invalid_argument if vehicle_id is empty or contains '/'.
auto detections(std::string_view vehicle_id) -> std::string;

/// Returns `orion/*/sensing/detections` — all vehicles.
constexpr auto allDetections() -> std::string_view { return "orion/*/sensing/detections"; }
} // namespace sensing

namespace clock
{
/// Returns `orion/{vehicle_id}/clock/sim_time`.
/// @throws std::invalid_argument if vehicle_id is empty or contains '/'.
auto simTime(std::string_view vehicle_id) -> std::string;
} // namespace clock

} // namespace orion::topic
