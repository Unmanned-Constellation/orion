# orion_clock - Clock Abstraction Library

Time abstraction library for the Orion autonomy platform.

## Overview

`orion_clock` provides a single abstract interface - `TimeSource` - that services use
for all time queries and sleeping. Injecting the clock at construction rather than
calling `std::chrono` directly makes periodic loops testable and simulation-safe:
the same service code that runs at 100 Hz on hardware can be driven at arbitrary
speed under a `ManualClock` in unit tests or under a `SimClock` in
software-in-the-loop runs (see ADR-0009).

The library is header-only (`orion_clock` is an `INTERFACE` CMake target) and has
no runtime dependencies beyond the C++ standard library. The single header is at
`libs/clock/include/orion/clock/clock.hpp`.

The `orion_app` library (`FrameScheduler`, `ShutdownLatch`) builds directly on top
of `orion_clock` and is documented in the same section below.

---

## Class hierarchy

```{mermaid}
classDiagram
    class TimeSource {
        <<abstract>>
        +nowNs() uint64_t
        +sleepUntil(target_ns) void
    }
    class WallClock {
        +nowNs() uint64_t
        +sleepUntil(target_ns) void
    }
    class ManualClock {
        -now_ns_ uint64_t
        -stopped_ bool
        +nowNs() uint64_t
        +sleepUntil(target_ns) void
        +advance(delta_ns) void
        +setNow(now_ns) void
        +wake() void
    }
    class SimClock {
        -scale_ double
        -sim_start_ns_ uint64_t
        -wall_start_ uint64_t
        +nowNs() uint64_t
        +sleepUntil(target_ns) void
    }
    class CoordinatedClock {
        -now_ns_ uint64_t
        +nowNs() uint64_t
        +sleepUntil(target_ns) void
        +update(sim_time_ns) void
        +wake() void
    }
    TimeSource <|-- WallClock : production
    TimeSource <|-- ManualClock : tests
    TimeSource <|-- SimClock : scaled real-time
    TimeSource <|-- CoordinatedClock : lockstep sim
```

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
`ManualClock` runs exactly when the test says it should - not when the OS wakes
it up.

### Monotonicity contract

Implementations must not return a `nowNs()` value less than a previously
returned value from the same instance. `ManualClock::setNow` enforces this
explicitly by throwing if you attempt to set time backwards.

---

## API Reference

### `TimeSource` - abstract base

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
immediately without sleeping. The implementation is a single sleep call - it
does not loop to correct for early wakeup. `FrameScheduler` accounts for this
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
// start multiple FrameScheduler threads all sharing clock...
clock->advance(10'000'000); // all schedulers with a 10 ms period fire at once
```

#### Thread safety

All public methods are thread-safe via an internal mutex. `sleepUntil` releases
the mutex while waiting. `ManualClock` is neither copyable nor movable.

The destructor sets `stopped_` and notifies all waiters, guaranteeing that no
thread remains blocked in `sleepUntil` after the object is destroyed - but the
caller is still responsible for joining any such threads before destroying the
clock to avoid a data race on the clock's internal state.

---

### `SimClock`

Scaled real-time clock for integration testing and fast SIL runs. Advances at
`scale×` wall speed; no external coordination required.

```cpp
// Run at 10× wall speed, sim time anchored to wall time at construction.
orion::clock::SimClock clock(10.0);

// Anchored start - useful in tests where sim_start_ns must be known.
orion::clock::SimClock clock(2.0, 0);  // sim time starts at 0, runs at 2× wall speed
```

**`nowNs()`** computes `sim_start_ns + (wall_now - wall_start) * scale`. All
members are set at construction and never mutated - `nowNs()` is thread-safe
with no locking.

**`sleepUntil(target_ns)`** uses a correcting loop: converts the remaining sim
duration to wall time (`remaining / scale`), sleeps, then re-checks. Callers
never wake before their sim-time deadline regardless of OS scheduler jitter.

| Constructor | Description |
|---|---|
| `SimClock(double scale)` | Sim time starts at wall time now, advances at `scale×`. |
| `SimClock(double scale, uint64_t sim_start_ns)` | Sim time starts at `sim_start_ns`, advances at `scale×`. |

Both throw `std::invalid_argument` if `scale` is `≤ 0`, `NaN`, or infinite.

---

### `CoordinatedClock`

Coordinated faster-than-real-time clock for lockstep multi-service simulation.
Driven externally via `update(sim_time_ns)` - typically called from a Zenoh
subscriber callback receiving `SimTimeUpdate` messages from a Clock Service.

All services sharing a `CoordinatedClock` advance in lockstep when the Clock
Service broadcasts a new timestamp. `sleepUntil` blocks on a condition variable
until `update()` advances past the target - identical pattern to `ManualClock`.

```cpp
auto clock = std::make_shared<orion::clock::CoordinatedClock>();

// Wire the Zenoh subscription in your service:
auto sub = session.subscribe<orion::v1::SimTimeUpdate>(
    "orion/alpha/clock/sim_time",
    [&](const orion::v1::SimTimeUpdate& msg, const auto&) {
        clock->update(msg.sim_time_ns());
    });
```

| Method | Description |
|---|---|
| `void update(uint64_t sim_time_ns)` | Advance clock to `sim_time_ns` and wake all `sleepUntil` waiters. Silently drops backwards updates. |
| `uint64_t nowNs() const` | Returns current sim time. Throws `std::logic_error` if called before the first `update()`. |
| `void sleepUntil(uint64_t target_ns)` | Blocks until `update()` advances past `target_ns`, or `wake()` is called, or the clock is destroyed. |
| `void wake()` | Unblocks all `sleepUntil` callers immediately without advancing time. Call before joining any thread blocked in `sleepUntil`. The destructor calls this automatically. |

---

## `FrameScheduler`

```cpp
#include "orion/app/frame_scheduler.hpp"
namespace orion::app
```

Single-threaded, time-triggered executor. Runs all registered callbacks
sequentially on one thread at integer sub-multiples of the minor frame rate
(see ADR-0008).

```{mermaid}
classDiagram
    class FrameScheduler {
        -period_ns_ uint64_t
        -frame_ticks_ uint64_t
        -clock_ shared_ptr~TimeSource~
        -latch_ ShutdownLatch*
        -tick_count_ uint64_t
        -callbacks_ vector~Entry~
        -overrun_count_ uint64_t
        +every(divisor, callback) void
        +run() void
        +overrunCount() uint64_t
        +rtPriorityApplied() bool
    }
    class Entry {
        <<nested>>
        +divisor uint64_t
        +callback function~void()~
    }
    FrameScheduler *-- Entry : callbacks_
    FrameScheduler --> TimeSource : clock_
    FrameScheduler --> ShutdownLatch : latch_
```

```cpp
orion::app::FrameScheduler sched(
    100.0,   // minor frame rate in Hz - must be a positive integer value
    clock,   // shared_ptr<TimeSource>
    &latch   // ShutdownLatch*
);
sched.every(1,   [&] { imu.read(); });          // 100 Hz - every tick
sched.every(10,  [&] { telemetry.publish(); });  // 10 Hz  - every 10th tick
sched.every(100, [&] { diagnostics.check(); });  // 1 Hz   - every 100th tick
sched.run();  // blocks until latch is stopped
```

`run()` blocks the calling thread. All callbacks execute in registration order
with no synchronisation required between components within the same service.

### Rate expression - tick divisors

`every(N, cb)` schedules `cb` on ticks where `tick_count % N == 0`. Tick count
starts at 1, so the first fire is on tick N. All divisors must evenly divide
the minor frame rate; a non-divisor asserts at registration time.

### Overrun handling

If the combined callback time in a tick exceeds the period, `FrameScheduler`:

1. Increments the overrun counter (readable via `overrunCount()`).
2. Advances `next_tick_` by one period from the intended deadline. If that
   deadline has already passed, the next tick fires immediately - recovering
   one period at a time until the scheduler catches up to the clock.

```
Normal tick:  deadline → sleep → [callbacks] → deadline + period → ...
Overrun tick: deadline → sleep → [slow callbacks] → deadline + period → (immediate if past) ...
```

### Constructor-time deadline anchoring

`next_tick_` is set at construction (`nowNs() + period_ns_`), not inside
`run()`. This eliminates a startup race in tests where the test thread might
advance the clock between construction and the first call to `run()`.

### Method reference

| Method | Description |
|--------|-------------|
| `FrameScheduler(double rate_hz, shared_ptr<TimeSource>, ShutdownLatch*, int rt_priority = 0)` | Construct at the given rate. `rate_hz` must be a positive integer value. `clock` and `latch` must not be null. Optional `rt_priority > 0` sets `SCHED_FIFO` priority on the run thread. Throws `std::invalid_argument` on invalid arguments. |
| `void every(uint64_t divisor, Fn&& callback)` | Register a callback to run every `divisor` ticks. Must be called before `run()`. Asserts `divisor > 0` and `divisor` evenly divides `rate_hz`. |
| `void run()` | Blocks, dispatching callbacks each tick in registration order. Returns when the latch is stopped. Asserts it has not been called previously. |
| `uint64_t overrunCount() const` | Number of ticks where combined callback time exceeded the period. Atomic, readable from any thread. |
| `bool rtPriorityApplied() const` | True if `run()` successfully applied the requested `SCHED_FIFO` priority via `pthread_setschedparam`. Always false when `rt_priority` is 0 or before `run()` is called. |

---

## `ShutdownLatch`

```cpp
#include "orion/app/shutdown_latch.hpp"
namespace orion::app
```

Blocks `main()` until SIGTERM or SIGINT is received, or until `stop()` is called
programmatically. Also acts as the run-loop exit signal for `FrameScheduler`.

```{mermaid}
classDiagram
    class ShutdownLatch {
        -mask_ sigset_t
        -mu_ mutex
        -cv_ condition_variable
        -stopped_ bool
        -watcher_ thread
        +wait() bool
        +stopped() bool
        +stop() void
    }
```

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

`orion_clock` is an `INTERFACE` (header-only) target. `orion_app` is a `STATIC` library:

```cmake
# Clock only
target_link_libraries(my_target PRIVATE orion_clock)

# App (FrameScheduler + ShutdownLatch) - pulls in orion_clock transitively
target_link_libraries(my_target PRIVATE orion_app)
```

Linking either target adds the appropriate include path and enforces `cxx_std_20`.

---

## Usage examples

### Minimal production service

`sched.run()` is the service loop. Everything before it is wiring. Callbacks
execute in registration order with no inter-component synchronisation required.

```cpp
#include "orion/clock/clock.hpp"
#include "orion/app/frame_scheduler.hpp"
#include "orion/app/shutdown_latch.hpp"

int main()
{
    orion::app::ShutdownLatch latch;   // must be first - sets signal mask before any other threads
    auto clock = std::make_shared<orion::clock::WallClock>();
    orion::app::FrameScheduler sched(100.0, clock, &latch);

    // construct dependencies...
    ImuReader      imu;
    GpsReader      gps;
    StateEstimator estimator;
    Controller     controller;
    Publisher      publisher;

    sched.every(1,  [&] { imu.read(); gps.read(); });          // 100 Hz
    sched.every(1,  [&] { estimator.update(); });               // 100 Hz, sees fresh sensor data
    sched.every(1,  [&] { controller.step(); });                // 100 Hz
    sched.every(10, [&] { publisher.publish(estimator.state()); }); // 10 Hz

    sched.run();
    // returns when SIGTERM/SIGINT is received or latch.stop() is called
}
```

### Unit test with ManualClock

```cpp
TEST(MyServiceTest, TicksAtRate)
{
    orion::app::ShutdownLatch latch;
    auto clock = std::make_shared<orion::clock::ManualClock>(0);
    orion::app::FrameScheduler sched(100.0, clock, &latch);
    std::atomic<int> count{0};

    sched.every(1, [&] { ++count; });
    std::thread runner([&] { sched.run(); });

    // Each 10 ms advance fires exactly one tick.
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

After a severe overrun the scheduler catches up by firing missed ticks
immediately - one per period - rather than skipping them.

```cpp
TEST(MyServiceTest, OverrunCatchesUp)
{
    orion::app::ShutdownLatch latch;
    auto clock = std::make_shared<orion::clock::ManualClock>(0);
    orion::app::FrameScheduler sched(100.0, clock, &latch);
    std::atomic<int> count{0};

    // First tick burns 3 extra periods inside the callback.
    bool first = true;
    sched.every(1, [&] {
        ++count;
        if (first) { first = false; clock->advance(30'000'000); }
    });

    std::thread runner([&] { sched.run(); });

    clock->advance(10'000'000); // trigger tick 1 (overrun)

    // Ticks 2–4 fire immediately as catch-up; tick 5 requires a normal advance.
    while (count.load() < 4) std::this_thread::sleep_for(1ms);
    EXPECT_EQ(sched.overrunCount(), 1u);

    latch.stop();
    clock->wake();
    runner.join();
}
```

---

## `ClockService`

```cpp
#include "orion/app/clock_service.hpp"
namespace orion::app
```

Publishes `SimTimeUpdate` on every `FrameScheduler` tick, driving all services that use
`CoordinatedClock` to advance in lockstep. Backed internally by a `SimClock` so it does
not need an external time source.

```{mermaid}
classDiagram
    class ClockService {
        -sim_clock_ SimClock
        -publisher_ Publisher~SimTimeUpdate~
        -scale_ double
        +ClockService(scale, publisher, scheduler)
        +create(scale, vehicle_id, session, scheduler) ClockService$
    }
    class SimClock {
        -scale_ double
        -wall_start_ uint64_t
        -sim_start_ns_ uint64_t
        +nowNs() uint64_t
        +sleepUntil(target_ns) void
    }
    class CoordinatedClock {
        +update(sim_time_ns) void
        +nowNs() uint64_t
        +sleepUntil(target_ns) void
    }
    ClockService *-- SimClock : sim_clock_
    ClockService --> Publisher~SimTimeUpdate~ : publisher_
    ClockService --> FrameScheduler : registers tick callback
    CoordinatedClock ..> ClockService : driven by SimTimeUpdate broadcasts
```

### Sim-time broadcast flow

```{mermaid}
sequenceDiagram
    participant FS as FrameScheduler
    participant CS as ClockService
    participant SC as SimClock
    participant Pub as Publisher~SimTimeUpdate~
    participant Zenoh as Zenoh Bus
    participant Sub as Subscriber~SimTimeUpdate~
    participant CC as CoordinatedClock

    FS->>CS: tick callback fires
    CS->>SC: nowNs()
    SC-->>CS: sim_time_ns
    CS->>Pub: publish(SimTimeUpdate{sim_time_ns}, sim_time_ns)
    Pub->>Zenoh: transmit Envelope

    Zenoh->>Sub: raw bytes received
    Sub->>Sub: deserialize → SimTimeUpdate
    Sub->>CC: update(msg.sim_time_ns())
    CC->>CC: now_ns_ = sim_time_ns, cv_.notify_all()
    Note over CC: any thread blocked in sleepUntil() is woken
```

### Method reference

| Method | Description |
|---|---|
| `ClockService(scale, publisher, scheduler)` | Constructs from an already-created publisher. Registers the tick callback on `scheduler`. |
| `ClockService::create(scale, vehicle_id, session, scheduler)` | Factory that calls `session.advertise<SimTimeUpdate>()` on the correct topic, then delegates to the constructor. |
