# ADR 0015: Gimbal FC Integration Path - PX4 Short-Term, Zenoh Nav Service Long-Term

## Status
Accepted

## Context
The `GimbalService` outer loop (see ADR-0013) computes desired gimbal attitude from
mission intent and the current vehicle state. "Vehicle state" means at minimum:

- **Attitude** (roll, pitch, yaw in the world frame) - to compute the gimbal-to-world
  rotation and stabilize the payload against drone maneuvers
- **Position** (latitude, longitude, altitude) - for geo-referenced target pointing
  and ground-track tracking
- **Velocity** - for predictive lead compensation when tracking moving targets

Without a vehicle state source, the outer loop can only perform open-loop pointing
(absolute gimbal angles with no world-frame reference). Open-loop pointing cannot
compensate for drone attitude changes and cannot geo-reference a target.

A production vehicle state estimate requires sensor fusion: IMU, barometer, GPS,
and optionally magnetometer and airspeed. This fusion is non-trivial and is
conventionally owned by a dedicated flight controller (FC) running an EKF.

Orion does not yet have a flight controller in its stack. This ADR establishes a
two-phase integration path that unblocks gimbal development now while preserving
the long-term architecture.

## Decision

### Phase 1 - PX4 flight controller via MAVLink (current)

A PX4 flight controller is integrated as a companion-computer partner to the Jetson.
PX4 runs its EKF (ECL EKF2) fusing IMU, barometer, and GPS, and publishes the
resulting state estimate as MAVLink telemetry over a UART or USB serial link to the
Jetson.

The Jetson receives:
- `ATTITUDE_QUATERNION` - vehicle attitude as a quaternion at 50–100 Hz
- `GLOBAL_POSITION_INT` - geodetic position and altitude
- `LOCAL_POSITION_NED` - NED position and velocity for tracking computations

A lightweight MAVLink receive loop on the Jetson (separate from the `FrameScheduler`
tick loop) ingests these messages, converts them to Orion internal types, and
publishes them on the Zenoh bus as a nav state topic. The `GimbalService` subscribes
to this topic like any other Orion subscriber.

PX4 is chosen over ArduPilot for Phase 1 because:
- MAVLink Gimbal Protocol v2 support is cleaner and more complete in PX4
- PX4's companion computer integration is well-documented and consistent
- The manager/device gimbal architecture (ADR-0013) aligns with PX4's own gimbal
  model

### Phase 2 - Custom FC publishing nav state over Zenoh (long-term)

When the Orion stack acquires its own flight controller (custom or otherwise), the
vehicle state estimate is published natively as an Orion microservice on the Zenoh
bus - a **Nav Service** that subscribes to raw sensor topics (IMU, GPS, baro) and
publishes a fused `NavState` protobuf message.

In Phase 2:
- The MAVLink receive loop is removed
- `GimbalService` subscribes to the Zenoh nav topic directly, with no change to its
  internal logic
- The `NavState` proto message schema is designed once and shared across all
  consumers (gimbal, decision service, telemetry)

The Phase 1 MAVLink shim is an adapter, not a permanent design. It is explicitly
scoped to the companion-computer period and is removed when the Nav Service exists.

### Capability gates

`GimbalService` degrades gracefully when vehicle state is unavailable:

| State available | Gimbal capability |
|---|---|
| Full nav state (attitude + position + velocity) | Geo-referenced targeting, predictive tracking |
| Attitude only | Inertially-stabilized pointing, no geo-reference |
| None | Open-loop angle commands only |

The current operating mode is logged and published on the gimbal state topic so
ground station operators are aware of degraded capability.

## Consequences

- Phase 1 introduces a MAVLink dependency on the Jetson for nav state ingestion.
  This dependency is confined to a single adapter component; it does not propagate
  into `GimbalService` or any other service.
- The `NavState` proto message must be defined before Phase 2 begins. Its schema
  should be designed to accommodate all nav consumers, not just the gimbal.
- During development (no FC connected), `GimbalService` operates in open-loop mode.
  This is sufficient for verifying mechanical integration and MAVLink gimbal
  protocol correctness.
- Phase 2 requires the custom FC to publish at sufficient rate for the 50 Hz outer
  loop. A nav state update rate of 50 Hz or higher is required; lower rates
  introduce stale-data latency into the pointing loop.
- The `SimClock` / `CoordinatedClock` infrastructure (ADR-0009) applies to the Nav
  Service in Phase 2: simulated nav state can be injected over Zenoh without
  modifying `GimbalService`, enabling SIL testing of the full pointing loop.
