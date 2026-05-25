#pragma once

#include <chrono>
#include <cstdint>

namespace orion::clock
{

/// Abstract time source injected into services at startup.
///
/// Enables deterministic domain timestamps in SIL and Batch Simulation.
/// Production code uses WallClock; simulation injects a SimClock (future work).
class Clock
{
  public:
    /// @cond
    Clock()                        = default;
    Clock(const Clock&)            = default;
    Clock& operator=(const Clock&) = default;
    Clock(Clock&&)                 = default;
    Clock& operator=(Clock&&)      = default;
    virtual ~Clock()               = default;
    /// @endcond

    /// Returns the current time as nanoseconds since the Unix epoch.
    /// @return Nanoseconds since the Unix epoch.
    [[nodiscard]] virtual uint64_t nowNs() const = 0;
};

/// Production Clock implementation backed by std::chrono::system_clock.
class WallClock final : public Clock
{
  public:
    [[nodiscard]] uint64_t nowNs() const override
    {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                         std::chrono::system_clock::now().time_since_epoch())
                                         .count());
    }
};

} // namespace orion::clock
