# ADR 0008: Major/Minor Frame Executor (`FrameScheduler`)

## Status
Accepted

## Context
`PeriodicTimer` gives each callback its own thread. This works for simple
services with a single loop, but a service with multiple components running at
different rates (e.g. IMU read at 100 Hz, telemetry at 10 Hz, diagnostics at
1 Hz) would require one thread per component. At the scale of 10 services with
5 components each, that is 50 threads competing for 6 cores on the Jetson Orin
Nano, with three compounding problems:

1. **Harmonic wakeup collisions.** At tick 0 and every common-divisor interval
   thereafter, all threads whose rates share a factor wake simultaneously. With
   10 services on a 6-core CPU, scheduler thrashing degrades timing precision on
   the fastest loops.

2. **Head-of-line blocking across threads.** Separating components onto
   different threads requires lock-free queues or atomics to pass data between
   them within the same service. A 100 Hz estimator reading from a 100 Hz IMU
   thread still requires synchronization.

3. **Wasted wakeups.** A timer-driven estimator that wakes at 100 Hz to find no
   new sensor data wastes CPU cycles and pollutes the cache.

The major/minor frame model, standard in aerospace hard real-time systems,
eliminates all three problems: one thread per service, one timer ticking at the
greatest common divisor of all component rates (the "minor frame"), callbacks
registered by tick divisor.

## Decision

Replace `PeriodicTimer` with `FrameScheduler` — a single-threaded,
time-triggered executor that runs all components of a service sequentially on
one thread.

```cpp
// Service main — all components on one thread
auto sched = FrameScheduler(100.0, clock, latch);    // minor frame = 100 Hz
sched.every(1,   [&] { imu.read(); });                // 100 Hz — every tick
sched.every(1,   [&] { estimator.update(); });         // 100 Hz — every tick
sched.every(10,  [&] { telemetry.publish(); });        // 10 Hz  — every 10th tick
sched.every(100, [&] { diagnostics.check(); });        // 1 Hz   — every 100th tick
sched.run();                                           // blocks until latch stops
```

### Rate expression — tick divisors

Rates are expressed as integer divisors of the minor frame, not as Hz values.
`every(N, cb)` schedules `cb` to run on ticks where `tick_count % N == 0`.
This makes the frame model explicit: all registered rates must evenly divide
the minor frame rate. A divisor that does not evenly divide the minor frame is
a programming error caught by assertion at registration time.

### Sequential execution within a tick

Callbacks registered for the current tick run in registration order on a single
thread. No synchronization is required between components within the same
service. Ordering matters and is the caller's responsibility: register
`imu.read()` before `estimator.update()` so the estimator always sees fresh
data.

### Overrun semantics

If the combined wall time of all callbacks in a tick exceeds the minor frame
period, `FrameScheduler`:

1. Increments an overrun counter (readable via `overrunCount()`).
2. Advances `next_tick_` by one period from the *intended* deadline (`next_tick_
   += period_ns_`), not from the end of the slow tick. If the intended deadline
   has already passed, `sleepUntil` returns immediately and the next tick fires
   at once — recovering one period at a time until the scheduler catches up to
   the clock.

Logging is the responsibility of the calling service, not `FrameScheduler`.
Library code does not log. Services that need overrun visibility should poll
`overrunCount()` and route warnings through their own logging facility.

If the intended next deadline has already passed (the overrun was severe), the
next tick fires immediately. Overruns on heavy ticks (where multiple divisors
coincide) are expected occasionally on Linux; chronic overruns indicate a WCET
budget violation and should be investigated.

### Real-time scheduling

`FrameScheduler` accepts an optional `rt_priority` constructor parameter:

```cpp
// Production — SCHED_FIFO priority 40 (requires CAP_SYS_NICE or systemd unit)
auto sched = FrameScheduler(100.0, clock, latch, /*rt_priority=*/40);

// Development / tests — SCHED_OTHER (default, no elevated capability needed)
auto sched = FrameScheduler(100.0, clock, latch);
```

When `rt_priority > 0`, `run()` calls `pthread_setschedparam(SCHED_FIFO,
rt_priority)` before entering the tick loop. Higher-rate schedulers should
receive higher priorities so that a slow telemetry tick on one service cannot
preempt a fast control tick on another. The `systemd` unit for each service
sets `AmbientCapabilities=CAP_SYS_NICE` to grant the required capability
without running as root.

Recommended priority assignments on the Jetson:

| Rate class | `rt_priority` |
|---|---|
| ≥ 100 Hz (control, estimation) | 40 |
| 10–99 Hz (sensing, planning)   | 30 |
| < 10 Hz (telemetry, diagnostics) | 20 |

### Shutdown

At the top of every tick, `FrameScheduler` checks `latch.stopped()`. If
true, `run()` returns. Maximum shutdown latency is one minor frame period
(10 ms at 100 Hz), which is operationally negligible.

### Registration lifetime

`every()` may only be called before `run()`. Calling `every()` after `run()`
has been entered asserts — registration is a setup-phase operation and the
callback list is owned exclusively by the run loop with no synchronization
required on the hot path.

### `PeriodicTimer` retirement

`PeriodicTimer` is deleted in the same PR that introduces `FrameScheduler`. No
service code currently uses `PeriodicTimer` — it exists only as a header and
its own tests. Those tests are superseded by `FrameScheduler` tests. The
`clock.hpp` doc comment referencing `PeriodicTimer` is updated to reference
`FrameScheduler`.

## Consequences

- Thread count is O(services), not O(components). The full system runs on at
  most one thread per service regardless of how many components are registered.
- No synchronization is required between components within the same service.
  Cross-service data sharing still requires lock-free queues or atomics.
- Callback ordering within a tick is the caller's responsibility.
  `FrameScheduler` does not validate or enforce dependency order.
- All rates registered on a `FrameScheduler` must evenly divide the minor frame
  rate. Non-harmonic rates (e.g. a 30 Hz camera on a 100 Hz frame) require
  either a separate `FrameScheduler` or rounding to the nearest harmonic.
- Heavy ticks (where multiple divisors coincide) have a larger WCET budget
  requirement than light ticks. Service authors must account for this when
  choosing the minor frame rate and registering callbacks.
- `SimClock` and `ManualClock` work identically — clock injection and the
  `sleepUntil` contract are unchanged.
- Production deployments require `CAP_SYS_NICE` (via `systemd`
  `AmbientCapabilities`) to use `SCHED_FIFO`. Development and test runs use the
  default `SCHED_OTHER` path without elevated capabilities.
- `PeriodicTimer` is removed entirely. There is no deprecation period — no
  service code depends on it at the time of this ADR.
