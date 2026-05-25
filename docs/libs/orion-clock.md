# orion_clock — Clock Abstraction Library

Time abstraction library for the Orion autonomy platform.

## Overview

`orion_clock` provides a single abstract interface — `Clock` — that services use
for all time queries and sleeping. Injecting the clock at construction rather than
calling `std::chrono` directly makes periodic loops testable and simulation-safe:
the same service code that runs at 100 Hz on hardware can be driven at arbitrary
speed under a `ManualClock` in unit tests or under a `SimClock` in
hardware-in-the-loop runs (see ADR-0009).

The library is header-only (`orion_clock` is an `INTERFACE` CMake target) and has
no runtime dependencies beyond the C++ standard library. The single header is at
`libs/clock/include/orion/clock/clock.hpp`.

The `orion_app` library (`PeriodicTimer`, `ShutdownLatch`) builds directly on top
of `orion_clock` and is documented in the same section below. `PeriodicTimer` is
superseded by `FrameScheduler` (ADR-0008) and will be removed in the same PR that
introduces it.

---

## Design

### Why dependency injection?

A service loop that calls `steady_clock::now()` directly is coupled to wall time.
In tests this means either running at real speed (slow, flaky) or mocking free
functions (fragile). By accepting a `shared_ptr<Clock>` the service becomes
agnostic about the time source: production code passes `WallClock`, tests pass
`ManualClock`, and a simulation driver can pass a `SimClock` that advances at
10× or 0.1× wall speed.

### Why does `sleepUntil` exist on the interface?

If sleeping were done with `std::this_thread::sleep_for`, the loop would always
sleep for wall time, defeating the point of injectable simulation clocks.
`sleepUntil` lets `ManualClock` block the caller on a condition variable until
simulated time advances, so a `FrameScheduler` driving at 100 Hz under
`ManualClock` runs exactly when the test says it should — not when the OS wakes
it up.

### Monotonicity contract

Implementations must not return a `nowNs()` value less than a previously
returned value from the same instance. `ManualClock::setNow` enforces this
explicitly by throwing if you attempt to set time backwards.

---

## API Reference

### `Clock` — abstract base

```cpp
#include "orion/clock/clock.hpp"
namespace orion::clock
```

| Method | Description |
|--------|-------------|
| `uint64_t nowNs() const` | Current time in nanoseconds since the Unix epoch. |
| `void sleepUntil(uint64_t target_ns)` | Block until clock time ≥ `target_ns`. Returns immediately if already past it. |

`nowNs` is `const`; implementations must be safe to call concurrently from
multiple threads without external locking.

---

### `WallClock`

Production implementation backed by `std::chrono::system_clock`.

```cpp
orion::clock::WallClock clock;
uint64_t now = clock.nowNs();           // nanoseconds since Unix epoch
clock.sleepUntil(now + 20'000'000);    // sleep ~20 ms
```

`sleepUntil` computes the remaining delta at call time and delegates to
`std::this_thread::sleep_for`. If the target is already in the past, it returns
immediately without sleeping. The implementation is a single sleep call — it
does not loop to correct for early wakeup. `PeriodicTimer` accounts for this
by checking the actual clock time after sleep and detecting overruns.

**Thread safety:** fully thread-safe; all methods are effectively stateless.

---

### `ManualClock`

Test and simulation driver. Time does not advance automatically; the test or
simulation controller calls `advance()` or `setNow()` to move it forward.
`sleepUntil()` blocks on a condition variable until the clock is advanced past
the target.

```cpp
orion::clock::ManualClock clock(0);   // start at t = 0 ns

clock.advance(10'000'000);            // +10 ms → t = 10 ms
clock.setNow(50'000'000);             // jump to t = 50 ms (absolute)
// clock.setNow(40'000'000);          // throws: time cannot go backwards
```

#### Method reference

| Method | Description |
|--------|-------------|
| `ManualClock(uint64_t initial_ns = 0)` | Construct at the given starting time. Defaults to 0. |
| `uint64_t nowNs() const` | Returns current simulated time. |
| `void sleepUntil(uint64_t target_ns)` | Blocks until `nowNs() >= target_ns`, `wake()` is called, or the clock is destroyed. |
| `void advance(uint64_t delta_ns)` | Increments time by `delta_ns` and wakes all waiters whose target has been reached. |
| `void setNow(uint64_t now_ns)` | Sets time to an absolute value and wakes all waiters. Throws `std::invalid_argument` if `now_ns < nowNs()`. |
| `void wake()` | Unblocks all `sleepUntil` callers immediately **without** advancing time. Sets an internal stopped flag so callers return cleanly. |

#### `wake()` vs `advance()`

`wake()` sets an internal `stopped_` flag and notifies all waiters. Its purpose
is test teardown: after stopping the `ShutdownLatch`, call `wake()` so any timer
thread blocked in `sleepUntil` can exit without the test having to advance time
further:

```cpp
latch.stop();
clock->wake();    // unblock sleepUntil → timer thread observes stopped latch → exits
runner.join();
```

`advance()` does not set `stopped_` and is the correct call during normal tick
progression. Using `advance()` for teardown is also valid but will fire the
callback one extra time before the timer thread exits.

#### Multiple waiters

`advance()` wakes **all** threads simultaneously whose `sleepUntil` target has
been reached. This is useful in integration tests where several services share
a single `ManualClock`:

```cpp
auto clock = std::make_shared<ManualClock>(0);
// start multiple PeriodicTimer threads all sharing clock...
clock->advance(10'000'000); // all timers with a 10 ms period fire at once
```

#### Thread safety

All public methods are thread-safe via an internal mutex. `sleepUntil` releases
the mutex while waiting. `ManualClock` is neither copyable nor movable.

The destructor sets `stopped_` and notifies all waiters, guaranteeing that no
thread remains blocked in `sleepUntil` after the object is destroyed — but the
caller is still responsible for joining any such threads before destroying the
clock to avoid a data race on the clock's internal state.

---

## `PeriodicTimer`

```cpp
#include "orion/app/periodic_timer.hpp"
namespace orion::app
```

Calls a callback at a fixed rate using an injected `Clock`. The timer runs until
the associated `ShutdownLatch` is stopped.

```cpp
orion::app::PeriodicTimer timer(
    100.0,   // rate in Hz
    clock,   // shared_ptr<Clock>
    &latch   // ShutdownLatch*
);
timer.run([&] { /* called ~100 times per second */ });
```

`run()` blocks the calling thread. Call it from a dedicated thread if you need
the main thread free for other work.

### Overrun handling

If a callback takes longer than the tick period, the timer:

1. Increments the overrun counter (readable via `overrunCount()`).
2. Schedules the **next** tick relative to when the overrun ended, not the
   original missed deadline. This prevents a burst of immediate catch-up ticks
   after a slow callback.

```
Normal tick:   deadline → sleep → callback → deadline + period → ...
Overrun tick:  deadline → sleep → [slow callback] → callback_end + period → ...
```

### Constructor-time deadline anchoring

`next_tick_` is computed at construction (`nowNs() + period_ns_`), not inside
`run()`. This eliminates a startup race in tests where the test thread might
advance the clock between object construction and the first call to `run()`.

### Method reference

| Method | Description |
|--------|-------------|
| `PeriodicTimer(double rate_hz, shared_ptr<Clock>, ShutdownLatch*)` | Construct at the given rate. Anchors the first deadline to construction time. |
| `void run(Fn&& callback)` | Blocks, calling `callback` at each tick. Returns when the latch is stopped. |
| `uint64_t overrunCount() const` | Number of ticks where the callback exceeded the period. Atomic, readable from any thread. |

---

## `ShutdownLatch`

```cpp
#include "orion/app/shutdown_latch.hpp"
namespace orion::app
```

Blocks `main()` until SIGTERM or SIGINT is received, or until `stop()` is called
programmatically. Also acts as the run-loop exit signal for `PeriodicTimer`.

```cpp
int main()
{
    orion::app::ShutdownLatch latch;
    // ... set up services ...
    latch.wait();   // blocks until Ctrl-C, SIGTERM, or latch.stop()
}
```

### Signal handling design

The constructor calls `pthread_sigmask(SIG_BLOCK, ...)` on the calling thread
before spawning an internal watcher thread. All threads created after this
inherit the blocked signal mask, so SIGTERM and SIGINT are delivered exclusively
to the watcher via `sigwait()` rather than asynchronously interrupting arbitrary
service threads. The watcher calls `stop()` when a signal arrives.

`stop()` is idempotent. When called from outside the watcher thread it also
sends `pthread_kill(watcher, SIGTERM)` to unblock the `sigwait` call, ensuring
the destructor's `join()` completes promptly.

**Construct `ShutdownLatch` at the very top of `main()`, before any other
threads are created**, so the blocked signal mask is inherited correctly.

### Method reference

| Method | Description |
|--------|-------------|
| `ShutdownLatch()` | Blocks SIGINT/SIGTERM on the calling thread and starts the watcher. |
| `void wait() const` | Blocks until `stopped()` is true. |
| `bool stopped() const` | Returns true if shutdown has been requested. |
| `void stop()` | Triggers shutdown. Idempotent. Safe to call from any thread. |

---

## CMake integration

`orion_clock` and `orion_app` are both `INTERFACE` (header-only) targets:

```cmake
# Clock only
target_link_libraries(my_target PRIVATE orion_clock)

# App (PeriodicTimer + ShutdownLatch) — pulls in orion_clock transitively
target_link_libraries(my_target PRIVATE orion_app)
```

Linking either target adds the appropriate include path and enforces `cxx_std_20`.

---

## Usage examples

### Minimal production service

`timer.run` is the service loop. Everything before it is wiring; everything
inside it is what the service actually does. The callback has no knowledge of
the clock or shutdown mechanism.

```cpp
#include "orion/clock/clock.hpp"
#include "orion/app/periodic_timer.hpp"
#include "orion/app/shutdown_latch.hpp"

int main()
{
    orion::app::ShutdownLatch latch;   // must be first — sets signal mask before any other threads
    auto clock = std::make_shared<orion::clock::WallClock>();
    orion::app::PeriodicTimer timer(100.0, clock, &latch);

    // construct dependencies...
    ImuReader      imu;
    GpsReader      gps;
    StateEstimator estimator;
    Controller     controller;
    Publisher      publisher;

    timer.run([&] {
        auto state = estimator.update(imu.read(), gps.read());
        controller.step(state);
        publisher.publish(state);
    });
    // returns when SIGTERM/SIGINT is received or latch.stop() is called
}
```

### Unit test with ManualClock

```cpp
TEST(MyServiceTest, TicksAtRate)
{
    orion::app::ShutdownLatch latch;
    auto clock = std::make_shared<orion::clock::ManualClock>(0);
    orion::app::PeriodicTimer timer(100.0, clock, &latch);
    std::atomic<int> count{0};

    std::thread runner([&] { timer.run([&] { ++count; }); });

    // Each 10 ms advance fires exactly one callback.
    clock->advance(10'000'000);
    while (count.load() < 1) std::this_thread::sleep_for(1ms);

    clock->advance(10'000'000);
    while (count.load() < 2) std::this_thread::sleep_for(1ms);

    latch.stop();
    clock->wake();
    runner.join();

    EXPECT_EQ(count.load(), 2);
}
```

### Testing overrun behaviour

```cpp
TEST(MyServiceTest, OverrunCountedAndNoburstAfter)
{
    orion::app::ShutdownLatch latch;
    auto clock = std::make_shared<orion::clock::ManualClock>(0);
    orion::app::PeriodicTimer timer(100.0, clock, &latch);
    std::atomic<int> count{0};

    // Callback burns 3 extra periods worth of simulated time.
    auto slow = [&] {
        ++count;
        clock->advance(30'000'000); // +30 ms inside a 10 ms period
    };

    std::thread runner([&] { timer.run(slow); });

    clock->advance(10'000'000); // trigger first tick
    while (count.load() < 1) std::this_thread::sleep_for(1ms);

    // After the overrun the timer should be sleeping, not firing in a burst.
    std::this_thread::sleep_for(5ms);
    EXPECT_EQ(count.load(), 1);
    EXPECT_GE(timer.overrunCount(), 1u);

    latch.stop();
    clock->wake();
    runner.join();
}
```
