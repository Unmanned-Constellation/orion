# ADR 0016: Reactive vs. Periodic Execution Model

## Status
Accepted

## Context
`FrameScheduler` (ADR-0008) defines a clear pattern for services with deterministic,
time-triggered execution: register callbacks at integer sub-multiples of a minor frame
rate, call `run()`, block until shutdown. This model works well for control loops,
state estimators, and any service whose cadence is driven by elapsed time.

Sensor and perception services have a fundamentally different execution model. A camera
pipeline does not produce frames on a schedule - frames arrive when the hardware
completes a capture. DeepStream and GStreamer manage their own internal thread pools,
buffer lifecycles, and backpressure mechanisms. Forcing a camera pipeline into a
`FrameScheduler` tick creates three problems:

1. **Tick/frame misalignment.** Camera frame rate and scheduler tick rate are independent.
   A tick that arrives between frames either stalls waiting for the next frame (blocking
   every other callback on the thread) or skips (wasting the tick). Neither is correct.

2. **Buffer lifecycle hazard.** NVMM buffers on the Jetson are reference-counted and
   must be returned promptly to the pipeline. Holding a buffer reference across a
   scheduler tick boundary creates buffer starvation and pipeline stalls.

3. **Framework inversion.** GStreamer/DeepStream are designed to drive their own event
   loop. Wrapping them in a polling tick works against the framework rather than with it.

Conversely, control and decision services (GimbalService, Decision Service) must not
be coupled to camera timing. A vision pipeline hiccup - dropped frame, thermal
throttle, inference overrun - must not affect gimbal stabilization or flight control.

## Decision

Orion services are divided into two execution domains separated by the Zenoh bus:

### Periodic domain - `FrameScheduler`

Control, decision, and telemetry services register on a `FrameScheduler` and run at
deterministic integer sub-multiples of the minor frame rate. These services:

- Are driven by a `Clock` (wall or simulated)
- Run all components sequentially on one thread
- Never block on external I/O or hardware events
- Read the latest published state from Zenoh on each tick

```cpp
// GimbalService main - periodic, deterministic
auto sched = FrameScheduler(100.0, clock, &latch);
sched.every(2, [&] { gimbal.update(nav_sub.latest(), target_sub.latest()); }); // 50 Hz
sched.run();
```

### Reactive domain - hardware/pipeline-driven

Sensor and perception services are driven by external event sources (camera pipeline
callbacks, hardware interrupts). These services:

- Are **not** registered on a `FrameScheduler`
- Own a pipeline object that manages its own threads
- Follow the start / wait / stop lifecycle:

```cpp
// PerceptionService main - reactive, event-driven
pipeline.start();   // spins up pipeline threads, returns immediately
latch.wait();       // main thread blocks until ShutdownLatch fires
pipeline.stop();    // drain, flush, join pipeline threads
```

`ShutdownLatch` is the universal stop signal for both domains. The main thread of every
Orion service - periodic or reactive - blocks on a single call until shutdown.

### Type 1 reactive services - Zenoh-subscriber-driven

Services that react purely to Zenoh messages (aggregators, health monitors,
protocol bridges) are a degenerate case of the reactive domain. The Zenoh subscriber
callback IS the event. No additional infrastructure is needed - the existing
`Session::subscribe` callback mechanism handles this. The main thread blocks on
`latch.wait()` while callbacks fire on Zenoh's internal thread.

### Type 2 reactive services - hardware/pipeline-driven

Services that react to external hardware (camera, future IMU, future radio) follow the
start/wait/stop pattern above. The pipeline object owns all hardware interaction and
thread management. The service main never touches hardware callbacks directly.

### Zenoh as the domain boundary

All cross-domain communication goes through Zenoh. A reactive service publishes its
output (detection metadata, sensor readings) on a Zenoh topic. A periodic service reads
the latest value from its subscriber on each tick. Neither domain has a direct reference
to the other.

```
Reactive domain              Zenoh bus              Periodic domain
─────────────────            ─────────              ────────────────────────
PerceptionService  ────────► detections ──────────► Decision Service (tick)
                                                  ► GimbalService    (tick)
```

This boundary means a vision pipeline stall or restart has zero effect on control loop
timing. The periodic services continue ticking and consuming the last-known detection.

### No base class - convention only

There is currently one planned hardware-driven service (Perception). A base class for a
single concrete user is premature abstraction. The start/wait/stop convention is
documented here and enforced by code review. A `PipelineService` base class is
introduced if and when a second hardware-driven service is added and the pattern proves
stable across both.

### Perception Service internals deferred

The concrete architecture of the Perception Service - DeepStream programmatic API
shape, buffer types, NVMM vs. CUDA memory, inference pipeline topology, and any sensor
abstraction interface - is deferred to a future ADR. The decision recorded here is the
execution model and domain boundary only.

## Consequences

- `FrameScheduler` is for periodic services only. No sensor I/O, no hardware callbacks,
  no blocking waits inside a tick.
- Periodic services never miss a tick due to camera frame jitter. Vision pipeline
  instability is contained within the reactive domain.
- The Zenoh bus is the only communication path between domains. Cross-domain data
  sharing without Zenoh is a design violation.
- Reactive services run on threads they do not own (pipeline/GStreamer threads). Shared
  state accessed from both a reactive callback and a periodic tick requires a lock-free
  slot or atomic - but such sharing is discouraged; prefer publishing on Zenoh instead.
- Simulation of reactive services (camera replay, sensor injection) is explicitly out of
  scope until the Perception architecture ADR is written. `SimClock` and
  `CoordinatedClock` apply to the periodic domain only; see ADR-0017.
- `ShutdownLatch::wait()` must be added alongside the existing `stopped()` poll and
  `stop()` signal - reactive services need a blocking wait, not just a poll.
