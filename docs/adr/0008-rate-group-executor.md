# ADR 0008: Rate-group executor for multi-rate service components

## Status
Proposed

## Context
`PeriodicTimer` gives each callback its own thread. This works well for simple
services with a single loop, but a service with multiple components running at
different rates (e.g. IMU read at 1000 Hz, state estimate at 100 Hz, telemetry
at 10 Hz) would require one thread per component. At the scale of 10 services
with 5 components each, that is 50 threads competing for 6 cores on the Jetson
Orin Nano. Most threads would be sleeping at any given moment, and OS scheduler
jitter on wakeup degrades timing precision for real-time components.

## Decision

Replace the single-callback `PeriodicTimer` with a `RateGroup` that owns a list
of callbacks, all executing sequentially within each tick on a single thread.
Services group components by rate and assign one `RateGroup` per distinct rate.

```cpp
RateGroup fast(100.0, clock, &latch);
fast.add([&] { imu.read(); });
fast.add([&] { estimator.update(); });
fast.add([&] { controller.step(); });

RateGroup slow(10.0, clock, &latch);
slow.add([&] { telemetry.publish(); });
slow.add([&] { diagnostics.check(); });

std::thread t1([&] { fast.run(); });
std::thread t2([&] { slow.run(); });
```

Key design choices:

**Sequential execution within a group.** Callbacks run in registration order
inside each tick. This avoids intra-tick data races between components at the
same rate without requiring locks. If ordering matters (e.g. sensor read before
estimator update), the caller controls it via registration order.

**One thread per rate, not per callback.** The thread count is bounded by the
number of distinct rates in the system — typically 4-6 — regardless of how many
components are registered. 10 services × 5 components never produces more
threads than there are distinct rates across all services.

**Overrun semantics are group-level.** If the total wall time of all callbacks
in a tick exceeds the period, the group counts one overrun and schedules the
next tick from the end of the slow tick (no catch-up burst). The overrun counter
is readable from any thread via `overrunCount()`.

**`PeriodicTimer` is retired.** `RateGroup` supersedes it. Existing single-
callback usage is migrated by registering one callback — the API is compatible
in practice.

## Consequences

- Thread count is O(distinct rates) rather than O(components). Realistic maximum
  is ~6 threads for the full system.
- A slow callback in a group delays every subsequent callback in the same tick.
  Components with hard per-callback deadlines that differ within the same rate
  should be split into separate groups.
- Callback ordering within a group is the caller's responsibility. The `RateGroup`
  does not enforce or validate dependency order.
- `SimClock` and `ManualClock` work identically to `PeriodicTimer` — clock
  injection is unchanged.
- `PeriodicTimer` tests are superseded by `RateGroup` tests. The existing test
  patterns (tick via `ManualClock`, verify count, verify overrun) apply directly.
