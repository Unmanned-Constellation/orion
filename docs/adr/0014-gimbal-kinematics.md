# ADR 0014: Gimbal Kinematics - Quaternion-First Math, Euler Angles at MCU Boundary Only

## Status
Accepted

## Context
The gimbal follows the topology: Drone Base → Yaw Motor → Roll Arm → Pitch Motor →
Camera Payload (see ADR-0012). This topology creates a well-known singularity: when
the Pitch axis approaches nadir (−90°) and the Yaw and Roll axes approach alignment,
the system loses a rotational degree of freedom. Any small perturbation near this
configuration produces large, discontinuous commanded angles - a condition known as
**gimbal lock**.

Nadir pointing (straight down, −90° pitch) is the primary operating mode for aerial
surveillance. A kinematics implementation based on Euler angles (RPY) will hit the
gimbal lock singularity precisely in the most-used configuration.

Euler angles have a second problem: interpolation between two Euler-angle
representations does not follow the shortest arc on the rotation manifold. Tracking
a moving target using Euler-angle interpolation produces unnecessary slew and can
cause the gimbal to take the long way around to a nearby attitude.

Quaternions represent rotations without singularity and interpolate along the shortest
arc (SLERP). They are the standard representation for rotations in aerospace and
robotics for exactly these reasons.

## Decision

All targeting, transformation, and tracking mathematics within the Orion autonomy stack
operates exclusively on **unit quaternions**. Euler angles (pitch, roll, yaw) are
produced only at the final serialization step when generating MAVLink commands for the
gimbal controller.

### Internal representation

Gimbal attitude, target bearing, and all intermediate rotation computations use
`Eigen::Quaterniond` (or an equivalent unit quaternion type). No function in the
`GimbalService` or any library it depends on accepts or returns RPY Euler angles as
part of its core logic.

### Quaternion serialization to MAVLink

MAVLink Gimbal Protocol v2 carries attitude as a quaternion natively in the
`GIMBAL_MANAGER_SET_ATTITUDE` `attitude_q` field (`[q_w, q_x, q_y, q_z]`). No
Euler conversion occurs at the MAVLink boundary - the internal quaternion is
serialized directly into the message fields.

Euler angles (pitch, roll, yaw) are produced only in two narrow cases:

- **Debug / logging output** - human-readable angle display for operators and
  developers; never re-ingested by any control logic.
- **Operator RPY input** - when a ground station or operator interface supplies
  a commanded attitude as RPY values (e.g. "point north, tilt down 45°"), those
  values are converted to a quaternion immediately at the input boundary and the
  RPY representation is discarded. All subsequent math operates on the quaternion.

### Gimbal lock avoidance

With quaternion-internal representation, no gimbal lock singularity exists in the
software. The nadir attitude (camera pointing straight down) is a well-behaved unit
quaternion. Target tracking through nadir, transitions between nadir and oblique
pointing, and SLERP interpolation for smooth slew all operate correctly at any
gimbal attitude.

### Mechanical topology consequences

The Drone Base → Yaw → Roll → Pitch → Camera chain introduces cross-axis inertial
coupling: a change in drone pitch changes the effective moment arm of the Yaw motor.
This coupling is handled in the inner control loop (see ADR-0013) by the gimbal
controller's own IMU-based stabilization. The outer loop quaternion math is
attitude-agnostic and does not model the mechanical coupling explicitly.

## Consequences

- No singularity exists in the tracking or targeting math at any gimbal attitude,
  including nadir and inverted configurations.
- Target tracking through the nadir transition is smooth and continuous.
- SLERP interpolation for commanded slew follows the shortest arc, minimizing
  unnecessary gimbal movement during target handoff or mode transitions.
- All code that processes gimbal attitude must accept and return quaternions.
  Functions that accept `float yaw, float pitch, float roll` parameters are not
  permitted in the internal API.
- Operator-facing interfaces (ground station displays, telemetry) that show
  human-readable angles convert from the internal quaternion representation at
  the display layer only.
- The Eigen linear algebra library (already a candidate dependency for computer
  vision and estimation code) provides `Quaterniond`, `AngleAxisd`, and the
  relevant conversion utilities. No additional quaternion library is required.
