# ADR 0013: Gimbal Control Loop Separation - Jetson Outer Loop vs. Dedicated Controller Inner Loop

## Status
Accepted

## Context
A 3-axis direct-drive gimbal requires two distinct control problems to be solved
simultaneously:

1. **Stabilization** - reject high-frequency disturbances (motor vibration, airframe flex,
   propwash) to keep the camera pointing at a stable inertial direction. This requires
   current-controlled FOC (Field-Oriented Control) running at 1 kHz or faster. The
   control loop period is 1 ms or less.

2. **Pointing** - command the gimbal to a desired inertial direction based on mission
   intent (manual operator input, autonomous targeting, or object tracking). This runs
   at the rate of the vision or navigation pipeline - tens of Hz, not hundreds.

The Jetson Orin Nano runs a standard Linux kernel. Linux is not a real-time OS: kernel
preemption, memory management interrupts, and scheduler jitter make sustained
sub-millisecond deterministic loop timing impossible without RT patches and careful
CPU isolation. Running FOC for three motor phases on the Jetson would produce
unpredictable current control, leading to torque ripple, instability, and potential
motor damage.

The AR0234 camera (see ADR-0012) produces frames at 20 fps at full resolution
(1920×1200) and 80 fps at half resolution (960×600). The outer pointing loop is
bounded by this frame rate - there is no value in commanding a new pointing target
faster than new vision data arrives.

## Decision

Strict separation of control concerns across two processing domains:

| Domain | Responsibility | Rate | Hardware |
|---|---|---|---|
| **Inner loop** | FOC current control, motor stabilization, encoder feedback | 1 kHz+ | Dedicated gimbal controller |
| **Outer loop** | Pointing commands, target tracking, mission logic | 50 Hz | Jetson `FrameScheduler` |

### Inner loop - dedicated gimbal controller

The inner loop runs on a dedicated gimbal controller board mounted on the gimbal
frame. For v1, this is the **Storm32 BGC** (32-bit STM32-based, open-source). It owns:

- 3-axis FOC for the BLDC motors
- Onboard IMU for attitude stabilization (gyro-rate feedback)
- Encoder position feedback per axis
- Yaw travel limit enforcement (see ADR-0012)
- MAVLink Gimbal Device role (receives pointing targets, reports attitude)

The Storm32 BGC is the v1 selection because it meets the ±1–2° pointing accuracy
requirement (governed by the 22° HFOV telephoto lens), is low-cost, and is
open-source. Storm32 uses its own serial protocol (STorM32 serial) as the primary
interface; MAVLink support exists but the extent of Gimbal Protocol v2
manager/device compliance must be verified against the firmware version in use.
If full v2 compliance is not available, the MAVLink layer can be implemented as a
thin translation shim on the Jetson side that converts v2 manager commands to
Storm32 serial, preserving the `GimbalService` interface contract unchanged.
A custom PCB replaces the Storm32 BGC in a later revision once the mechanical
design and full-system integration have been validated.

### Outer loop - Jetson `FrameScheduler`

A `GimbalService` runs on the Jetson as a standard Orion microservice. It is
registered on the `FrameScheduler` at **50 Hz** (every other tick on a 100 Hz minor
frame) and:

1. Reads the current vehicle state from the nav topic (see ADR-0015).
2. Reads the current tracking target from the Decision Service (bearing, position, or
   screen coordinates).
3. Computes the desired gimbal attitude in quaternion form (see ADR-0014).
4. Serializes and sends a MAVLink `GIMBAL_MANAGER_SET_ATTITUDE` command to the
   gimbal controller over UART.
5. Reads `GIMBAL_DEVICE_ATTITUDE_STATUS` telemetry from the controller and publishes
   it on the Zenoh bus as a gimbal state topic.

The 50 Hz outer loop rate is chosen to match the vision pipeline's 80 fps half-res
frame rate with margin, while remaining well within the FrameScheduler's budget on
a 100 Hz minor frame.

### Interface - MAVLink Gimbal Protocol v2

The Jetson (`GimbalService`) acts as the **Gimbal Manager**. The Storm32 BGC acts as
the **Gimbal Device**. The protocol is MAVLink Gimbal Protocol v2:

- `GIMBAL_MANAGER_SET_ATTITUDE` - Jetson → controller (desired attitude quaternion)
- `GIMBAL_DEVICE_ATTITUDE_STATUS` - controller → Jetson (actual attitude, flags)
- `GIMBAL_MANAGER_INFORMATION` - controller → Jetson on connect (capabilities, limits)

MAVLink v2 is chosen over v1 (`MOUNT_CONTROL`) because the manager/device separation
maps cleanly to this architecture and v1 is deprecated.

The physical transport is **UART**. The Storm32 BGC does not have a network interface;
UART serial MAVLink is the standard embedded gimbal integration path.

## Consequences

- The Jetson never runs FOC or accesses motor current feedback. It sends and receives
  high-level attitude data only.
- The inner loop continues stabilizing regardless of Jetson load, network conditions,
  or software faults. The gimbal holds its last commanded attitude if the outer loop
  stops sending.
- Pointing latency has a floor of one outer-loop period (20 ms at 50 Hz), plus MAVLink
  serialization and UART transit time (negligible at 115200+ baud for small frames).
- Replacing the Storm32 BGC with a custom PCB in a future revision requires implementing
  MAVLink Gimbal Protocol v2 device-side firmware on the replacement MCU, but does not
  change the `GimbalService` on the Jetson.
- `GimbalService` must be registered on the `FrameScheduler` before `run()` is called.
  The UART file descriptor is opened at service construction time and closed on
  destruction, consistent with the resource-ownership pattern used elsewhere in Orion.
