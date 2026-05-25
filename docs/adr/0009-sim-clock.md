# ADR 0009: SimClock for scaled and replay simulation

## Status
Proposed

## Context
`WallClock` runs services at real time. `ManualClock` is step-driven and
suitable for unit tests but not for continuous simulation. A third clock
implementation is needed for two scenarios:

1. **Scaled real-time** — run the full service stack at N× wall speed for
   integration testing and hardware-in-the-loop runs. All services advance
   together; no external step driver is required.

2. **Replay** — drive time from a recorded log. An external driver publishes
   the current sim time on a well-known topic; services block in `sleepUntil`
   until the driver advances past their deadline.

Both scenarios share the same requirement: `sleepUntil` must block until
simulated time reaches the target, not wall time.

`Publisher<T>::publish` already stamps `published_at_ns` using the injected
clock (ADR-0004), so all inter-service timestamps are automatically correct
when `SimClock` is injected — no service code changes required.

## Decision

Implement `SimClock` as a third `Clock` implementation with two modes selected
at construction:

**Scaled mode** — `SimClock(double scale)`. Time advances automatically at
`scale × wall rate`. `nowNs()` computes `sim_start + (wall_now - wall_start) * scale`.
`sleepUntil(target_ns)` converts the target back to wall time and calls
`std::this_thread::sleep_for` with the scaled delta.

**Replay mode** — `SimClock(ReplayMode)`. Time does not advance automatically.
An internal subscriber listens on `orion/{vehicle_id}/clock/sim_time` for
`SimTimeUpdate` messages published by the replay driver. On receipt it calls the
same `setNow` / notify path as `ManualClock`, unblocking any `sleepUntil`
waiters whose deadline has been reached.

Key design choices:

**Epoch anchoring.** `nowNs()` in scaled mode computes time relative to the
wall instant at construction (`wall_start_`, `sim_start_`). This ensures
`PeriodicTimer`'s `next_tick_` (anchored at construction) and `nowNs()` are
always in the same reference frame.

**`sleepUntil` precision.** Scaled mode sleeps for `(target_ns - nowNs()) / scale`
wall nanoseconds. At 10× scale a 10 ms sim sleep becomes a 1 ms wall sleep —
below OS timer resolution. Services running at very high sim speeds will
experience wakeup jitter; this is acceptable for non-real-time simulation.

**Replay mode uses the same condvar pattern as `ManualClock`.** The subscriber
callback calls `setNow` which notifies all waiters. `SimClock` in replay mode
is therefore testable with the same `ManualClock`-style test helpers.

**Clock topic.** `orion/{vehicle_id}/clock/sim_time` carries a `SimTimeUpdate`
proto (fields: `sim_time_ns`, `scale`). The vehicle ID is read from the
`ORION_VEHICLE_ID` environment variable at `SimClock` construction.

## Consequences

- Services require no code changes to run under simulation — only the clock
  passed to `Session::create` and `RateGroup` changes.
- Scaled mode introduces timing imprecision at high scale factors (>10×) due
  to OS sleep granularity. Replay mode has no such limit.
- Replay mode introduces a dependency on `orion_transport` from `orion_clock`.
  This may require splitting `SimClock` into a separate `orion_sim_clock` target
  to avoid a circular dependency (`orion_transport` already depends on
  `orion_clock`).
- A `SimTimeUpdate` proto definition must be added to `proto/orion/v1/`.
- The replay driver (a separate tool or test harness) is out of scope for this
  ADR.
