# Roadmap

Orion follows a milestone-based roadmap. Each milestone maps to one or more epics and the
architectural decisions that govern them. Milestones are tracked on
[GitHub](https://github.com/Unmanned-Constellation/orion/milestones).

---

## M1 — Core Runtime ✅

**Status:** Complete  
**GitHub milestone:** [M1](https://github.com/Unmanned-Constellation/orion/milestone/1)

Establishes the deterministic execution model and the simulation clock foundation. All
subsequent services schedule their work on the FrameScheduler and derive time from a
`TimeSource`.

| Epic | ADR | Description |
|------|-----|-------------|
| [#10 FrameScheduler](https://github.com/Unmanned-Constellation/orion/issues/10) | [ADR-0008](adr/0008-frame-scheduler.md) | Major/minor frame executor with configurable tick rate |
| [#11 SimClock Phase 1](https://github.com/Unmanned-Constellation/orion/issues/11) | [ADR-0009](adr/0009-sim-clock.md) | Scaled real-time clock (`SimClock`) for HIL testing |

**Foundation ADRs** (no dedicated epic — established before milestone tracking):

| ADR | Description |
|-----|-------------|
| [ADR-0001](adr/0001-zenoh-as-transport.md) | Zenoh as the publish/subscribe transport |
| [ADR-0002](adr/0002-deepstream-over-raw-gstreamer.md) | DeepStream over raw GStreamer for perception |
| [ADR-0003](adr/0003-multi-platform-build-and-deploy.md) | Multi-platform build and deploy strategy |
| [ADR-0004](adr/0004-transport-abstraction.md) | Transport abstraction layer |
| [ADR-0005](adr/0005-topic-naming-scheme.md) | Topic naming scheme |

---

## M2 — Observability ✅

**Status:** Complete  
**GitHub milestone:** [M2](https://github.com/Unmanned-Constellation/orion/milestone/2)

Gives operators and developers the tools to diagnose failures in the field. A symbolized stack
trace on crash and structured log output are the minimum bar before deploying to hardware.

| Epic | ADR | Description |
|------|-----|-------------|
| [#12 CrashHandler](https://github.com/Unmanned-Constellation/orion/issues/12) | [ADR-0010](adr/0010-crash-handler.md) | Symbolized stack traces on fault signals via backward-cpp |
| — | [ADR-0011](adr/0011-structured-logging.md) | Async structured logging via spdlog and `LoggerFactory` |

---

## M3 — Simulation ✅

**Status:** Complete  
**GitHub milestone:** [M3](https://github.com/Unmanned-Constellation/orion/milestone/3)

Extends the clock subsystem to support replay-driven simulation. An `ExternalClock` stub
receives `SimTimeUpdate` messages over Zenoh, allowing recorded flight logs to drive the full
runtime at arbitrary speeds.

| Epic | ADR | Description |
|------|-----|-------------|
| [#14 CoordinatedClock Phase 3](https://github.com/Unmanned-Constellation/orion/issues/14) | [ADR-0009](adr/0009-sim-clock.md) | Full `CoordinatedClock` implementation and Clock Service publisher |
| — | [ADR-0017](adr/0017-simulation-driving-model.md) | Simulation driving model (flight-log replay) |

---

## M3.5 — Service Infrastructure ✅

**Status:** Complete

Cross-cutting infrastructure that every microservice needs before deployment to
hardware. Benchmarking validates the transport; CLI11 replaces the hand-rolled
config stopgap in `clock_service` and establishes the pattern for all future
services.

| ADR | Description |
|-----|-------------|
| [ADR-0018](adr/0018-benchmarking-infrastructure.md) | Google Benchmark harness, same-process Zenoh latency and throughput |
| [ADR-0019](adr/0019-cli-and-env-config.md) | CLI11 for argument and env var parsing; `orion_main` shared config library |

---

## M4 — Gimbal Subsystem 🔄

## M4 — Gimbal Subsystem 🔄

**Status:** In progress (not yet on a GitHub milestone)  
**Epic:** [#38](https://github.com/Unmanned-Constellation/orion/issues/38)

Integrates the Storm32 BGC gimbal over MAVLink v2, with a 50 Hz `GimbalService` outer loop
scheduled on the `FrameScheduler` and a kinematics solver decoupled from the control loop.

| ADR | Description |
|-----|-------------|
| [ADR-0012](adr/0012-gimbal-signal-routing.md) | Gimbal signal routing |
| [ADR-0013](adr/0013-gimbal-control-loop-separation.md) | Gimbal control loop separation |
| [ADR-0014](adr/0014-gimbal-kinematics.md) | Gimbal kinematics |
| [ADR-0015](adr/0015-gimbal-fc-integration.md) | Gimbal FC integration |

---

## M5 — External Interfaces ⏳

**Status:** Deferred — architecture decided, implementation not yet scheduled  
**GitHub milestone:** [M4](https://github.com/Unmanned-Constellation/orion/milestone/4)

Exposes Orion's runtime state to external consumers. The edge network service aggregates
multi-protocol traffic at the network boundary; the public API service presents a REST and
WebSocket interface to customer systems.

| Epic | ADR | Description |
|------|-----|-------------|
| [#15 Edge Network Service](https://github.com/Unmanned-Constellation/orion/issues/15) | [ADR-0006](adr/0006-edge-network-service.md) | Multi-protocol gateway |
| [#16 Public API Service](https://github.com/Unmanned-Constellation/orion/issues/16) | [ADR-0007](adr/0007-public-api-service.md) | REST + WebSocket customer interface |

---

## M6 — Reactive Execution ⏳

**Status:** Deferred — architecture decided, implementation not yet scheduled  
**Epic:** [#44](https://github.com/Unmanned-Constellation/orion/issues/44)

Transitions the perception pipeline from periodic polling to an event-driven model. Sensor
callbacks trigger processing immediately rather than waiting for the next frame boundary,
reducing latency for time-sensitive detections.

| ADR | Description |
|-----|-------------|
| [ADR-0016](adr/0016-reactive-vs-periodic-execution-model.md) | Reactive vs. periodic execution model |

---

## v1.0.0 Definition of Done

v1.0.0 is the **software foundation release** — the runtime is stable enough to host the first
microservice and the build is verified on the target hardware. It does not require any
application-level subsystem (gimbal, perception, etc.) to be complete.

- [x] M1 — Core Runtime (FrameScheduler + SimClock Phase 1)
- [x] M2 — Observability (CrashHandler + structured logging)
- [x] M3 — Simulation (ExternalClock + Clock Service publisher)
- [ ] ARM64 build compiles and tests pass on the Jetson Orin Nano

M4 (Gimbal), M5 (External Interfaces), and M6 (Reactive Execution) are post-1.0 scope.
