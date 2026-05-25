# ADR 0009: SimClock for scaled and replay simulation

## Status
Accepted — phased implementation (Phase 1 complete, Phase 2 in progress, Phase 3 future)

## Context
`WallClock` runs services at real time. `ManualClock` is step-driven and suitable
for unit tests but not for continuous simulation. A third clock implementation is
needed for two scenarios:

1. **Scaled real-time** — run the full service stack at N× wall speed for
   integration testing and hardware-in-the-loop runs on the Orin Nano. All
   services advance together; no external step driver is required.

2. **Replay** — drive time from a recorded log. An external driver publishes the
   current sim time on a well-known Zenoh topic; services block in `sleepUntil`
   until the driver advances past their deadline. This is the long-term target
   for deterministic reproduction of field events (e.g. perception anomalies from
   Cut drone flight logs).

Both scenarios share the same requirement: `sleepUntil` must block until
simulated time reaches the target, not wall time.

`Publisher<T>::publish` already stamps `published_at_ns` using the injected clock
(ADR-0004), so all inter-service timestamps are automatically correct when a
simulation clock is injected — no service code changes required. The clock
dependency in `orion_transport` is therefore load-bearing and must not be removed.

## Decision

Two separate classes implement the two simulation modes. Both inherit `Clock` and
live in `namespace orion::clock`.

---

### Phase 1 — `SimClock` (scaled mode, `orion_clock`)

`SimClock` lives directly in `clock.hpp`. It has no dependencies beyond the C++
standard library, preserving `orion_clock`'s zero-dependency property.

**Constructors:**

```cpp
// Production: sim time starts at wall time now, advances at scale× wall rate.
explicit SimClock(double scale);

// Test / replay anchor: sim time starts at sim_start_ns.
explicit SimClock(double scale, uint64_t sim_start_ns);
```

Both constructors throw `std::invalid_argument` if `scale` is `<= 0`, `NaN`, or
`Inf`.

**`nowNs()`** computes:

```
sim_start_ns_ + (wall_now - wall_start_) * scale_
```

`wall_start_` is always captured at construction. `sim_start_ns_` is either
`wall_start_` (no-arg form) or the caller-supplied value.

**`sleepUntil(target_ns)`** uses a correcting loop — it does not return until
`nowNs() >= target_ns`. The loop converts the remaining sim duration to wall time
(`remaining / scale_`) and sleeps, then re-checks. This absorbs OS scheduler
jitter and makes the contract identical to `ManualClock`: callers never wake
before their deadline.

**Thread safety:** `SimClock` has no mutable state after construction. All members
are set once and never modified. `nowNs()` is therefore inherently thread-safe
with no locking required.

---

### Phase 2 — `ExternalClock` stub (`orion_sim_clock`)

`ExternalClock` is the replay-mode clock. It lives in a new CMake target
`orion_sim_clock` (`libs/sim_clock/`) which links both `orion_clock` and
`orion_transport`, breaking the circular dependency that would arise if
`ExternalClock` lived inside `orion_clock`.

**Dependency graph:**

```
orion_clock        (Clock, WallClock, ManualClock, SimClock)
     │
orion_transport    (Session, Publisher, Subscriber — stamps with Clock)
     │
orion_sim_clock    (ExternalClock — depends on both)
```

**Constructor (Phase 2 stub):**

```cpp
explicit ExternalClock(orion::transport::Subscriber<SimTimeUpdate> sub);
```

The constructor accepts the already-created subscriber (wiring of the topic
string and vehicle ID is the caller's responsibility). In Phase 2, all methods
throw `std::logic_error("ExternalClock not yet implemented")`. The CMake target
and dependency graph are structurally correct so that Phase 3 is an in-place
fill-in with no architectural changes.

**Phase 3 implementation (future):**

`ExternalClock` will store `now_ns_` and a condition variable. The subscriber
callback calls `setNow(msg.sim_time_ns())`, advancing the clock and notifying all
`sleepUntil` waiters — the same condvar pattern as `ManualClock`. `nowNs()`
returns the last received timestamp under a mutex.

---

### `SimTimeUpdate` proto

A new message `proto/orion/v1/sim_time_update.proto` carries time updates from
the replay driver:

```proto
message SimTimeUpdate {
  uint64 sim_time_ns = 1;  // current simulated time, nanoseconds since Unix epoch
  double scale       = 2;  // informational: driver playback speed relative to wall time
}
```

The `scale` field is not used by `ExternalClock` for computation — it is present
for observability (monitoring tools can display playback speed).

**Topic:** `orion/{vehicle_id}/clock/sim_time`

`vehicle_id` is supplied by the caller when constructing the subscriber, consistent
with how `SessionConfig::vehicle_id` is passed throughout the transport layer.
There is no `ORION_VEHICLE_ID` environment variable.

---

## Consequences

- Services require no code changes to run under simulation — only the concrete
  `Clock` passed to `Session::create` and `FrameScheduler` changes.
- `orion_clock` gains `SimClock` with zero new dependencies (Phase 1 complete).
- `orion_sim_clock` is a new CMake target that owns `ExternalClock` and carries
  the `orion_transport` dependency (Phase 2 in progress).
- Scaled mode introduces timing imprecision at very high scale factors (>100×)
  due to OS sleep granularity; the correcting loop minimises but cannot eliminate
  jitter.
- `ExternalClock` in Phase 2 compiles and links correctly but throws at runtime —
  teams must not ship Phase 2 code to production hardware until Phase 3 is complete.
- The replay driver (the external Zenoh publisher of `SimTimeUpdate` messages) is
  out of scope until a real flight log dataset exists to drive it.
