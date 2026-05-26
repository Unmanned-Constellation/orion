#pragma once

#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace orion::clock
{

/// Abstract time source injected into services at startup.
///
/// Provides both time queries and sleeping so that periodic service loops remain
/// correct under simulation — a SimClock can advance faster or slower than wall
/// time, and sleepUntil blocks until simulated time reaches the target rather
/// than wall time.
///
/// Production code uses WallClock. Tests use ManualClock. SimClock runs services
/// at scaled wall speed for integration testing and HIL runs (see ADR-0009).
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

    /// Blocks the calling thread until the clock reaches target_ns.
    ///
    /// For WallClock this is a standard timed sleep. For SimClock this blocks
    /// until the simulation time source advances past the target — allowing
    /// periodic loops to run at simulated rate regardless of wall speed.
    ///
    /// @param target_ns Target time as nanoseconds since the Unix epoch.
    virtual void sleepUntil(uint64_t target_ns) = 0;
};

/// Production Clock implementation backed by std::chrono::system_clock.
class WallClock final : public Clock
{
  public:
    /// @copydoc Clock::nowNs
    [[nodiscard]] uint64_t nowNs() const override
    {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                         std::chrono::system_clock::now().time_since_epoch())
                                         .count());
    }

    /// @copydoc Clock::sleepUntil
    void sleepUntil(uint64_t target_ns) override
    {
        namespace sc = std::chrono;
        auto target  = sc::system_clock::time_point{sc::nanoseconds{target_ns}};
        auto delta   = target - sc::system_clock::now();
        if (delta > sc::nanoseconds::zero())
        {
            std::this_thread::sleep_for(delta);
        }
    }
};

/// Manually-controlled Clock for tests and simulation drivers.
///
/// Time does not advance on its own. Call advance() or setNow() to move it
/// forward. sleepUntil() blocks until the clock is advanced past the target,
/// making it suitable for driving a FrameScheduler from a test thread.
///
/// @note The ManualClock must outlive any thread blocked in sleepUntil(). The
///       destructor wakes all waiters before releasing resources.
class ManualClock final : public Clock
{
  public:
    /// Constructs a ManualClock at the given initial time.
    /// @param initial_ns Starting time in nanoseconds since the Unix epoch.
    explicit ManualClock(uint64_t initial_ns = 0) : now_ns_(initial_ns) {}

    ~ManualClock() override
    {
        {
            std::lock_guard lock(mu_);
            stopped_ = true;
        }
        cv_.notify_all();
    }

    ManualClock(const ManualClock&)            = delete;
    ManualClock& operator=(const ManualClock&) = delete;
    ManualClock(ManualClock&&)                 = delete;
    ManualClock& operator=(ManualClock&&)      = delete;

    /// @copydoc Clock::nowNs
    [[nodiscard]] uint64_t nowNs() const override
    {
        std::lock_guard lock(mu_);
        return now_ns_;
    }

    /// @copydoc Clock::sleepUntil
    void sleepUntil(uint64_t target_ns) override
    {
        std::unique_lock lock(mu_);
        cv_.wait(lock, [&] { return stopped_ || now_ns_ >= target_ns; });
    }

    /// Advances the clock by delta_ns nanoseconds, unblocking any sleepUntil waiters.
    /// @param delta_ns Duration to advance in nanoseconds.
    void advance(uint64_t delta_ns)
    {
        {
            std::lock_guard lock(mu_);
            now_ns_ += delta_ns;
        }
        cv_.notify_all();
    }

    /// Wakes all sleepUntil waiters immediately without advancing time.
    ///
    /// Call this after stopping the shutdown latch so any timer thread blocked
    /// in sleepUntil can re-check its exit condition and return.
    void wake()
    {
        {
            std::lock_guard lock(mu_);
            stopped_ = true;
        }
        cv_.notify_all();
    }

    /// Sets the clock to an absolute time, unblocking any sleepUntil waiters.
    /// @param now_ns Absolute time in nanoseconds since the Unix epoch.
    /// @throws std::invalid_argument if now_ns is less than the current time.
    void setNow(uint64_t now_ns)
    {
        {
            std::lock_guard lock(mu_);
            if (now_ns < now_ns_)
            {
                throw std::invalid_argument("ManualClock::setNow: time cannot go backwards");
            }
            now_ns_ = now_ns;
        }
        cv_.notify_all();
    }

  private:
    mutable std::mutex      mu_;
    std::condition_variable cv_;
    uint64_t                now_ns_;
    bool                    stopped_{false};
};

/// Clock implementation for scaled real-time simulation.
///
/// Sim time advances at `scale` times wall speed, anchored to `sim_start_ns` at
/// construction. `sleepUntil` uses a correcting loop so callers never wake before
/// their sim-time deadline regardless of OS scheduler jitter.
///
/// All members are set once at construction — `nowNs()` is thread-safe with no
/// locking.
///
/// @see ADR-0009
class SimClock final : public Clock
{
  public:
    /// Constructs a SimClock anchored at the current wall time.
    ///
    /// Sim time starts at the wall time of construction and advances at `scale`
    /// times wall speed.
    ///
    /// @param scale  Simulation speed relative to wall time. Must be finite and > 0.
    /// @throws std::invalid_argument if scale is <= 0, NaN, or infinite.
    explicit SimClock(double scale) : SimClock(scale, wallNowNs()) {}

    /// Constructs a SimClock with an explicit sim-time origin.
    ///
    /// @param scale        Simulation speed relative to wall time. Must be finite and > 0.
    /// @param sim_start_ns Sim time at construction, nanoseconds since the Unix epoch.
    /// @throws std::invalid_argument if scale is <= 0, NaN, or infinite.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    SimClock(double scale, uint64_t sim_start_ns)
        : scale_(validateScale(scale)), wall_start_(wallNowNs()), sim_start_ns_(sim_start_ns)
    {
    }

    /// @copydoc Clock::nowNs
    [[nodiscard]] uint64_t nowNs() const override
    {
        const auto ELAPSED = static_cast<double>(wallNowNs() - wall_start_);
        return sim_start_ns_ + static_cast<uint64_t>(ELAPSED * scale_);
    }

    /// @copydoc Clock::sleepUntil
    void sleepUntil(uint64_t target_ns) override
    {
        while (nowNs() < target_ns)
        {
            const auto REMAINING_SIM = static_cast<double>(target_ns - nowNs());
            const auto WALL_WAIT_NS  = static_cast<int64_t>(REMAINING_SIM / scale_);
            if (WALL_WAIT_NS > 0)
            {
                std::this_thread::sleep_for(std::chrono::nanoseconds{WALL_WAIT_NS});
            }
        }
    }

  private:
    static auto validateScale(double scale) -> double
    {
        if (scale <= 0.0 || std::isnan(scale) || std::isinf(scale))
        {
            throw std::invalid_argument("SimClock: scale must be finite and > 0");
        }
        return scale;
    }

    static auto wallNowNs() -> uint64_t
    {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                         std::chrono::system_clock::now().time_since_epoch())
                                         .count());
    }

    double   scale_;
    uint64_t wall_start_;
    uint64_t sim_start_ns_;
};

/// Clock implementation for coordinated faster-than-real-time simulation.
///
/// Driven by an external caller (typically a Zenoh subscriber callback) via update().
/// All services running a CoordinatedClock advance in lockstep when the Clock Service
/// broadcasts a new SimTimeUpdate. sleepUntil() blocks until update() advances past
/// the target — identical condvar pattern to ManualClock.
///
/// Phase 2 stub — nowNs() and sleepUntil() throw std::logic_error until Phase 3.
/// update() and the constructor are structurally correct so Phase 3 is an in-place
/// fill-in with no architectural changes.
///
/// @see ADR-0009
class CoordinatedClock final : public Clock
{
  public:
    CoordinatedClock()                                           = default;
    CoordinatedClock(const CoordinatedClock&)                    = delete;
    auto operator=(const CoordinatedClock&) -> CoordinatedClock& = delete;
    CoordinatedClock(CoordinatedClock&&)                         = delete;
    auto operator=(CoordinatedClock&&) -> CoordinatedClock&      = delete;
    ~CoordinatedClock() override                                 = default;

    /// Advances the clock to sim_time_ns and unblocks any sleepUntil waiters.
    /// Called by the service's SimTimeUpdate subscriber callback.
    /// @param sim_time_ns  New simulated time, nanoseconds since the Unix epoch.
    void update(uint64_t /*sim_time_ns*/)
    {
        throw std::logic_error("CoordinatedClock not yet implemented");
    }

    /// @throws std::logic_error always — not yet implemented.
    [[nodiscard]] uint64_t nowNs() const override
    {
        throw std::logic_error("CoordinatedClock not yet implemented");
    }

    /// @throws std::logic_error always — not yet implemented.
    void sleepUntil(uint64_t /*target_ns*/) override
    {
        throw std::logic_error("CoordinatedClock not yet implemented");
    }
};

} // namespace orion::clock
