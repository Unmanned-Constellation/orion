# ADR 0017: Simulation Driving Model

## Status
Accepted - Clock Service implementation is Phase 3 (future); SimClock SIL is available now.

## Context
Orion services hold a `TimeSource` reference and call `clock->nowNs()` at the point of data
capture and `clock->sleepUntil()` inside `FrameScheduler`. Three clock implementations
exist (ADR-0009): `WallClock` (production), `SimClock` (scaled real-time), and
`CoordinatedClock` (lockstep sim, Phase 3 stub). The clock is injected at construction -
services have no knowledge of which implementation they hold.

ADR-0009 defines the clock implementations and the `SimTimeUpdate` proto. It explicitly
defers the Clock Service publisher and the assembly of a full SIL configuration. This
ADR captures those decisions.

## Decision

### Two simulation modes

| Mode | Clock | Use case |
|---|---|---|
| **Scaled real-time** | `SimClock(scale)` | Integration tests, fast dev-loop SIL runs |
| **Coordinated lockstep** | `CoordinatedClock` | Deterministic Monte Carlo, batch replay |

Scaled real-time requires no external coordination - all services constructed with the
same `scale` at the same wall time agree on sim time automatically. Start all services;
they run at `scale×` wall speed with no orchestration.

Coordinated lockstep requires a Clock Service to broadcast `SimTimeUpdate`. All
`CoordinatedClock` services block in `sleepUntil` until the broadcast advances past
their deadline. This guarantees all services process the same simulated instant before
any of them advance, regardless of host machine load.

### Clock Service - standalone Orion microservice

A dedicated `clock_service` binary owns the `SimTimeUpdate` publisher. It:

1. Creates a `Session` with service name `clock_service`.
2. Publishes `orion/{vehicle_id}/clock/sim_time` (`SimTimeUpdate`) at a configurable
   tick rate (default: minor frame rate of the fastest registered service).
3. Accepts `--scale` and `--start-ns` at launch to set playback speed and epoch.
4. Respects `ShutdownLatch` - exits cleanly on SIGINT/SIGTERM.

The `clock_service` is started **before** all other services in a coordinated SIL run.
`CoordinatedClock` services block in `sleepUntil` from construction until the first
`SimTimeUpdate` arrives; the Clock Service's first publish unblocks them all.

```
SIL startup order (coordinated mode):
  1. clock_service --scale 10 --vehicle alpha
  2. perception_service --clock coordinated --vehicle alpha   (blocks until tick 1)
  3. gimbal_service     --clock coordinated --vehicle alpha   (blocks until tick 1)
  4. decision_service   --clock coordinated --vehicle alpha   (blocks until tick 1)
     ↑ all unblock on first SimTimeUpdate broadcast
```

`clock_service` is not deployed in production. It is a SIL-only binary.

### Clock selection at launch

Each service selects its clock implementation via a `--clock` flag (or equivalent
config) at launch:

| `--clock` value | Implementation | When to use |
|---|---|---|
| `wall` | `WallClock` | Production, HIL |
| `sim:<scale>` | `SimClock(scale)` | Scaled real-time SIL |
| `coordinated` | `CoordinatedClock` | Lockstep SIL (requires clock_service) |

The `FrameScheduler` receives the constructed `TimeSource` - no scheduler change is needed
between production and simulation. The swap is entirely at the service entry point.

### Simulation parity guarantee (periodic domain)

For `FrameScheduler`-based services, simulation parity means:

1. **Same code path.** No `#ifdef SIM` in service logic. The clock is injected; the
   service cannot distinguish wall from sim.
2. **Same tick structure.** `every(N, cb)` divisors, registration order, and overrun
   semantics are identical. `SimClock::sleepUntil` wakes at the simulated deadline;
   the scheduler loop is unmodified.
3. **Deterministic tick sequence.** In coordinated mode, the Clock Service advances
   time in fixed steps. All services process tick `T` before any service advances to
   tick `T+1`. This eliminates ordering artifacts from host machine scheduling variance.

### Reactive domain in simulation

Event-driven services (ADR-0016) are driven by hardware events, not the clock. Their
simulation model - how camera frames or sensor data are injected in place of hardware
callbacks - is deferred to the Perception architecture ADR. The Clock Service does not
currently drive reactive services; they are excluded from coordinated simulation until
that ADR is written.

A SIL run may mix execution domains: `FrameScheduler` services run under
`CoordinatedClock` while the Perception Service runs against real camera hardware (HIL
partial) or is simply omitted from the run.

### `ShutdownLatch::wait()` requirement

`clock_service` and reactive services (ADR-0016) need a blocking `latch.wait()` call,
not just `latch.stopped()` polling. `ShutdownLatch` must expose:

```cpp
void wait();  // blocks until stop() is called
```

This is additive - `stopped()` and `stop()` are unchanged.

## Consequences

- SIL runs require no changes to service logic. The only difference from production is
  the `--clock` flag and the presence of `clock_service` in the process list.
- `SimClock` SIL (scaled mode) is available immediately - no Clock Service needed.
  `CoordinatedClock` SIL is blocked on Phase 3 implementation (ADR-0009).
- `clock_service` is a new CMake executable target in `orion_app` or a dedicated
  `apps/clock_service/` directory. It depends on `orion_transport` and `orion_proto`
  only - no `orion_app` scheduler dependency.
- Adding `ShutdownLatch::wait()` is a non-breaking additive change; existing callers
  of `stopped()` and `stop()` are unaffected.
- Reactive service simulation fidelity (camera replay, sensor injection) is explicitly
  deferred. Batch Monte Carlo simulation is only meaningful for the periodic control
  domain until the Perception ADR resolves the reactive side.
