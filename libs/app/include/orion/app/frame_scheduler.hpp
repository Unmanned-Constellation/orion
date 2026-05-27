#pragma once

#include <atomic>
#include <cassert>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include <pthread.h>

#include "orion/app/shutdown_latch.hpp"
#include "orion/clock/clock.hpp"

namespace orion::app
{

/// Single-threaded, time-triggered executor.
///
/// Runs all registered callbacks on one thread at integer sub-multiples of the
/// minor frame rate. See ADR-0008.
class FrameScheduler
{
  public:
    /// @param rate_hz     Minor frame rate in Hz.
    /// @param clock       Time source — must outlive this object.
    /// @param latch       Shutdown signal — run() returns when latch.stopped().
    /// @param rt_priority SCHED_FIFO priority passed to pthread_setschedparam
    ///                    before the tick loop. 0 (default) keeps SCHED_OTHER.
    FrameScheduler(double                                    rate_hz,
                   std::shared_ptr<orion::clock::TimeSource> clock,
                   ShutdownLatch*                            latch,
                   int                                       rt_priority = 0)
        : period_ns_(periodNsFrom(rate_hz)),
          frame_ticks_(static_cast<uint64_t>(rate_hz)),
          clock_(clockFrom(std::move(clock))),
          latch_(latch),
          rt_priority_(rt_priority),
          next_tick_(clock_->nowNs() + period_ns_)
    {
        assert(latch_ != nullptr && "latch must not be null");
    }

    /// Registers a callback to run on every tick where tick_count % divisor == 0.
    ///
    /// May only be called before run(). Calling after run() has been entered
    /// is a programming error and will assert.
    ///
    /// Preconditions are enforced with assert() and are therefore only checked
    /// in debug builds (NDEBUG disables them in release).
    ///
    /// @param divisor  Tick divisor. Must be > 0 and must evenly divide rate_hz.
    /// @param callback Invocable called on matching ticks.
    template <std::invocable Fn>
    void every(uint64_t divisor, Fn&& callback)
    {
        assert(divisor > 0 && "divisor must be > 0");
        assert(frame_ticks_ % divisor == 0 && "divisor must evenly divide the minor frame");
        assert(!running_.load(std::memory_order_acquire) && "every() called after run()");
        callbacks_.push_back({divisor, std::forward<Fn>(callback)});
    }

    /// Blocks until latch.stopped(), executing registered callbacks each tick.
    void run()
    {
        assert(!running_.exchange(true, std::memory_order_acq_rel) &&
               "run() called more than once");

        if (rt_priority_ > 0)
        {
            sched_param param{};
            param.sched_priority = rt_priority_;
            const int result     = pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
            rt_priority_applied_.store(result == 0, std::memory_order_relaxed);
        }

        while (!latch_->stopped())
        {
            clock_->sleepUntil(next_tick_);

            if (latch_->stopped())
            {
                break;
            }

            const uint64_t tick_start = clock_->nowNs();
            for (auto& entry : callbacks_)
            {
                if (tick_count_ % entry.divisor == 0)
                {
                    entry.callback();
                }
            }
            const uint64_t tick_end = clock_->nowNs();
            const uint64_t elapsed  = tick_end - tick_start;

            if (elapsed > period_ns_)
            {
                overrun_count_.fetch_add(1, std::memory_order_relaxed);
            }
            next_tick_ += period_ns_;

            ++tick_count_;
        }

        running_.store(false, std::memory_order_release);
    }

    /// Number of ticks where the combined callback time exceeded the period.
    /// @return Count of overrun ticks since construction.
    [[nodiscard]] auto overrunCount() const -> uint64_t
    {
        return overrun_count_.load(std::memory_order_acquire);
    }

    /// True if run() successfully applied the requested SCHED_FIFO priority.
    /// Always false when rt_priority is 0 or before run() has been called.
    /// @return Whether pthread_setschedparam succeeded.
    [[nodiscard]] auto rtPriorityApplied() const -> bool
    {
        return rt_priority_applied_.load(std::memory_order_relaxed);
    }

  private:
    struct Entry
    {
        uint64_t              divisor;
        std::function<void()> callback;
    };

    static auto periodNsFrom(double rate_hz) -> uint64_t
    {
        assert(rate_hz > 0.0 && std::floor(rate_hz) == rate_hz &&
               "rate_hz must be a positive integer Hz value");
        return static_cast<uint64_t>(1e9 / rate_hz);
    }

    static auto clockFrom(std::shared_ptr<orion::clock::TimeSource> clock)
        -> std::shared_ptr<orion::clock::TimeSource>
    {
        assert(clock != nullptr && "clock must not be null");
        return clock;
    }

    const uint64_t                            period_ns_;
    const uint64_t                            frame_ticks_;
    std::shared_ptr<orion::clock::TimeSource> clock_;
    ShutdownLatch* const                      latch_;
    const int                                 rt_priority_;
    uint64_t                                  next_tick_;
    // starts at 1: divisor-N callbacks first fire on tick N, 2N, 3N…
    uint64_t              tick_count_{1};
    std::vector<Entry>    callbacks_;
    std::atomic<bool>     running_{false};
    std::atomic<bool>     rt_priority_applied_{false};
    std::atomic<uint64_t> overrun_count_{0};
};

} // namespace orion::app
