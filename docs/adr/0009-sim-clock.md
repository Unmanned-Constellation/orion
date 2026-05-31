# ADR 0009: SimClock and CoordinatedClock for simulation

## Status
Accepted - phased implementation (Phase 1 complete, Phase 2 in progress, Phase 3 future)

## Context
`WallClock` runs services at real time. `ManualClock` is step-driven and suitable for unit tests
but not for continuous simulation. Two additional clock implementations are needed:

1. **Scaled real-time (`SimClock`)** - run the full service stack at N× wall speed for
   integration testing. All services advance together; no external coordination required.

2. **Coordinated sim time (`CoordinatedClock`)** - a Clock Service publishes `SimTimeUpdate`
   messages over Zenoh; all services block in `sleepUntil` until the broadcast advances past
   their deadline. This enables deterministic faster-than-real-time simulation where all services
   stay in lockstep regardless of processing variance.

Both scenarios share the same requirement: `sleepUntil` must block until simulated time reaches
the target, not wall time.

Services own their own `TimeSource` reference and pass `captured_at_ns` to `Publisher::publish()`
directly (ADR-0004). The clock implementation is therefore swappable per-service at startup
with no transport changes required.

## Decision

All clock implementations live in `orion_clock` (`libs/clock/clock.hpp`) with zero external
dependencies. `orion_clock` and `orion_transport` have no dependency on each other.

```
orion_clock      (Clock, WallClock, ManualClock, SimClock, CoordinatedClock - zero deps)
orion_transport  (Session, Publisher, Subscriber - zero clock dep)
services         (link both independently)
```

---

### Phase 1 - `SimClock` (complete)

`SimClock` has no dependencies beyond the C++ standard library.

**Constructors:**

```cpp
explicit SimClock(double scale);
SimClock(double scale, uint64_t sim_start_ns);
```

Both throw `std::invalid_argument` if `scale` is `<= 0`, `NaN`, or `Inf`.

**`nowNs()`** computes:

```
sim_start_ns_ + (wall_now - wall_start_) * scale_
```

**`sleepUntil(target_ns)`** uses a correcting loop - converts remaining sim duration to wall
time (`remaining / scale_`) and sleeps, then re-checks. Callers never wake before their deadline.

**Thread safety:** no mutable state after construction - inherently thread-safe, no locking.

---

### Phase 2 - `CoordinatedClock` stub (in progress)

`CoordinatedClock` is the coordinated-sim-time clock. It lives in `clock.hpp` alongside the
other implementations - no separate CMake target needed.

**Design:** `CoordinatedClock` exposes an `update(uint64_t sim_time_ns)` method. The calling
service subscribes to `orion/{vehicle_id}/clock/sim_time` and calls `clock->update(msg.sim_time_ns())`
in the subscriber callback. The clock itself has no knowledge of Zenoh or transport.

This mirrors the `ManualClock` pattern: an external driver advances time, and `sleepUntil`
waiters unblock when time passes their target. `ManualClock` is driven by test threads;
`CoordinatedClock` is driven by a Zenoh subscriber callback.

**Phase 2 stub:** constructor compiles correctly. `update()`, `nowNs()`, and `sleepUntil()`
all throw `std::logic_error("CoordinatedClock not yet implemented")`.

**Phase 3 implementation (future):**

`CoordinatedClock` stores `now_ns_` and a condition variable. `update()` sets `now_ns_` under
a mutex and notifies all `sleepUntil` waiters - identical condvar pattern to `ManualClock`.
`nowNs()` returns the last received timestamp under the same mutex.

---

### `SimTimeUpdate` proto

`proto/orion/v1/sim_time_update.proto` carries time updates from the Clock Service:

```proto
message SimTimeUpdate {
  uint64 sim_time_ns = 1;  // current simulated time, nanoseconds since Unix epoch
  double scale       = 2;  // informational: playback speed relative to wall time
}
```

`scale` is not used by `CoordinatedClock` for computation - present for observability only.

**Topic:** `orion/{vehicle_id}/clock/sim_time`

---

## Consequences

- `orion_clock` remains zero-dependency. No separate clock library for coordinated mode.
- The service that uses `CoordinatedClock` owns the Zenoh subscription and calls `update()`.
- `SimClock` and `CoordinatedClock` cover both simulation modes with no transport coupling.
- Scaled mode introduces timing imprecision at very high scale factors (>100×) due to OS sleep
  granularity; the correcting loop minimises but cannot eliminate jitter.
- The Clock Service publisher (the external broadcaster of `SimTimeUpdate`) is out of scope
  until a concrete simulation driver exists.
