#pragma once

#include <atomic>
#include <concepts>
#include <cstdint>
#include <iostream>
#include <memory>

#include "orion/app/shutdown_latch.hpp"
#include "orion/clock/clock.hpp"

namespace orion::app
{

/// Runs a callback at a fixed rate until the ShutdownLatch is stopped.
///
/// The timer calls clock_->sleepUntil() for each tick, so its rate tracks
/// simulated time when a ManualClock or SimClock is injected — the loop runs
/// at wall rate in production and at simulated rate in SIL or batch simulation.
///
/// If a callback takes longer than the tick period, the timer logs the overrun
/// to stderr, increments the overrun counter, and schedules the next tick
/// relative to when the overrun occurred (no catch-up burst).
class PeriodicTimer
{
  public:
    /// @param rate_hz  Callback frequency in Hz.
    /// @param clock    Time source — must outlive this object.
    /// @param latch    Shutdown signal — run() returns when latch.stopped().
    PeriodicTimer(double rate_hz, std::shared_ptr<orion::clock::Clock> clock, ShutdownLatch* latch)
        : period_ns_(static_cast<uint64_t>(1e9 / rate_hz)),
          clock_(std::move(clock)),
          latch_(latch),
          next_tick_(clock_->nowNs() + period_ns_)
    {
    }

    /// Blocks until latch.stopped(), calling callback at each tick.
    ///
    /// @param callback Invocable called once per tick. Must be non-blocking or
    ///                 complete within the tick period to avoid overruns.
    template <std::invocable Fn>
    void run(Fn&& callback)
    {
        while (!latch_->stopped())
        {
            clock_->sleepUntil(next_tick_);

            if (latch_->stopped())
            {
                break;
            }

            uint64_t tick_start = clock_->nowNs();
            callback();
            uint64_t tick_end = clock_->nowNs();

            uint64_t elapsed = tick_end - tick_start;
            if (elapsed > period_ns_)
            {
                ++overrun_count_;
                // Schedule next tick from now rather than catching up — a burst
                // of immediate ticks after an overrun is worse than one missed tick.
                next_tick_ = tick_end + period_ns_;
                std::cerr << "[PeriodicTimer] overrun: callback took " << elapsed
                          << "ns, period is " << period_ns_ << "ns\n";
            }
            else
            {
                next_tick_ += period_ns_;
            }
        }
    }

    /// Number of ticks where the callback exceeded the period duration.
    /// @return Count of overrun ticks since construction.
    [[nodiscard]] uint64_t overrunCount() const
    {
        return overrun_count_.load(std::memory_order_relaxed);
    }

  private:
    uint64_t                             period_ns_;
    std::shared_ptr<orion::clock::Clock> clock_;
    ShutdownLatch*                       latch_;
    uint64_t                             next_tick_;
    std::atomic<uint64_t>                overrun_count_{0};
};

} // namespace orion::app
