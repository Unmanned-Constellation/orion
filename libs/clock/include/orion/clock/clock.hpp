#pragma once

#include <chrono>
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

} // namespace orion::clock
