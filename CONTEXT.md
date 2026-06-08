# Orion - Domain Glossary

## Orion
The on-board software system running on the Jetson Orin Nano. Composed of multiple microservices that communicate over the Zenoh Bus.

## Microservice
A C++ process that publishes and/or subscribes on the Zenoh Bus. Each microservice has a single responsibility and is independently deployable.

## Zenoh Bus
The shared-memory pub-sub transport layer connecting microservices on the same Jetson board. Also routes published topics to off-board consumers over WiFi/radio using the same API and topic hierarchy. Not a broker - there is no intermediary in the data path.

## Perception Service
The microservice responsible for camera ingestion, hardware-accelerated TensorRT inference via DeepStream, and publishing Detection Metadata to the Zenoh Bus.

## Vision Camera
The Arducam Darksee (AR0234 sensor, 1/2.6", global shutter). Connects to the Jetson Orin Nano via MIPI CSI but exposes a V4L2 device node through an on-board FPGA ISP (ImageEK). Output format is UYVY; operated at 960×600@30fps for the inference pipeline. Because the camera bypasses the Jetson's native Argus ISP stack, `v4l2src` is the GStreamer source element — not `nvarguscamerasrc`.

## Detection Metadata
The structured output of the Perception Service. Published as a `DetectionFrame` on the Zenoh Bus once per inference frame. Each `DetectionFrame` contains: frame dimensions (`frame_width`, `frame_height`), and zero or more detections each carrying a bounding box in normalized coordinates [0,1], class ID, confidence score, and a persistent track ID. Track IDs are assigned by `nvtracker` (IOU tracker) and are stable across frames within a session. TensorRT engine runs at FP16 precision. Initial model is a COCO-pretrained RT-DETR-R18 (Ultralytics) used to validate the full pipeline; fine-tuned for drone detection in a later phase.

## DetectionFrame
The unit of output fired by a `PerceptionBackend` callback and published by `PerceptionService` as a single Zenoh message. Contains `frame_width`, `frame_height`, `camera_id` (identifies the source camera within a service instance), `pipeline_latency_ns` (elapsed time from V4L2 frame capture to appsink callback — computed as `appsink_time − captured_at_ns`), and a repeated list of `Detection` entries (normalized bbox, class ID, confidence, track ID). Designed to support multi-camera backends including future stereo/depth configurations.

## PerceptionBackend
An abstract interface internal to `PerceptionService`. Owns the camera pipeline and all hardware interaction. Fires a `DetectionFrame` callback when inference results are available. Has no knowledge of Zenoh or the transport layer. Two implementations: `DeepStreamBackend` (Jetson, conditionally compiled) and `FakePerceptionBackend` (x86/test). `captured_at_ns` reflects true frame capture time: `DeepStreamBackend` derives it from the V4L2 buffer PTS (`GST_BUFFER_PTS`) corrected by a `CLOCK_REALTIME − CLOCK_MONOTONIC` offset computed once at `start()`.

## Inference Video Stream
The `DeepStreamBackend` produces a second output in addition to `DetectionFrame`: a hardware-encoded H.264 video stream with `nvdsosd` overlays rendered before encoding. The `appsink` tee is placed before `nvdsosd` so `pipeline_latency_ns` measures capture-to-inference, not capture-to-encode. The OSD renders in the top-left corner in white text with black border: FPS, pipeline latency (ms), detection count, and per-detection bounding box labels (class name, confidence %, track ID). The stream is sent over UDP+RTP (`rtph264pay → udpsink`) to a configurable `--stream-host` and `--stream-port`. Any standard player (VLC, GStreamer) can receive it. RTSP is deferred until a frontend exists.

## FakePerceptionBackend
The test double for `PerceptionBackend`. Constructed with a `std::vector<DetectionFrame>` and a configurable emit interval. Replays the supplied frames sequentially at that interval when `start()` is called. Also exposes an `emit(DetectionFrame)` method for synchronous on-demand injection in unit tests. Produces fully deterministic output — no randomness.

## Decision Service
A custom C++ microservice that subscribes to Detection Metadata from the Zenoh Bus and produces flight decisions or control commands.

## Off-board Consumer
A remote process - ground station or companion computer - that subscribes to Zenoh topics over WiFi or radio. Indistinguishable from an on-board subscriber at the API level.

## Middleware Service
The microservice that hosts the Zenoh Router and owns the Zenoh bus configuration. Not application logic - configuration and process lifecycle only.

## Topic Schema
The Protobuf-defined message contracts published on Zenoh topics. `.proto` files are the canonical interface between all microservices, on-board and off-board. Schema files live in `proto/` and every service builds against the generated C++ code.

## Zenoh Router
A lightweight Zenoh process running on the Jetson that bridges the local shared-memory bus to the network transport for off-board consumers. Not application code - configuration only.

## Session
The entry point to the transport layer. Created once per microservice at startup with a `SessionConfig` (vehicle ID, service name). Factory for `Publisher<T>` and `Subscriber<T>` instances.

## Publisher
A typed handle returned by `Session::advertise<T>(topic)`. Calling `publish(msg, captured_at_ns)` serializes the message, stamps an `orion::v1::Header`, wraps everything in an `orion::v1::Envelope`, and transmits it on the Zenoh Bus. The calling service supplies `captured_at_ns` - the time the underlying data was captured - by calling `clock->nowNs()` at the point of capture.

## Subscriber
A lifetime handle returned by `Session::subscribe<T>(topic, callback)`. Holds the subscription active until destroyed. The callback receives the typed message and a `MessageHeader`.

## MessageHeader
A plain C++ struct (`captured_at_ns`, `source_id`) delivered to every subscriber callback. Carries transport-level metadata without exposing protobuf types in the callback signature. `captured_at_ns` is the time the underlying data was captured, set by the publishing service. `source_id` is the publishing service's name.

## Envelope
An internal protobuf message (`proto/orion/v1/envelope.proto`) wrapping every transmitted payload. Contains the `Header`, serialized payload bytes, and a `type_url` for runtime type validation. Never visible to service authors - created and consumed exclusively by the transport layer.

## HIL (Hardware-In-the-Loop)
A test mode where real hardware is part of the running system. Uses `WallClock` - time runs at wall speed because the hardware expects it.

## SIL (Software-In-the-Loop)
A test mode where no physical hardware is present - all hardware dependencies are simulated in software. Services run as real processes. Compatible with `WallClock`, `SimClock`, or `CoordinatedClock`.

## Batch Simulation
A SIL mode that replays missions repeatedly at accelerated speed for Monte Carlo-style analysis. Services are driven by `CoordinatedClock`, which receives coordinated sim time from a Clock Service publisher over Zenoh.

## TimeSource
The abstract interface (`nowNs() → uint64_t`, `sleepUntil(uint64_t)`) in `orion_clock`. Each microservice holds its own `TimeSource` reference and calls `clock->nowNs()` at the point of data capture to produce timestamps passed to `Publisher::publish`. Services have no knowledge of whether they are running against a `WallClock`, `SimClock`, or `CoordinatedClock`; the injected implementation determines the time mode.

## WallClock
Concrete `TimeSource` implementation backed by `std::chrono::system_clock`. Used in production and real-time HIL.

## SimClock
A `TimeSource` implementation for scaled real-time simulation. Advances at a fixed multiple of wall speed (`scale×`), computed entirely from the wall clock - no external coordination required. Two services constructed with the same scale at the same wall time will agree on sim time automatically. Suitable for integration testing and fast SIL runs where lockstep coordination is not required.

## CoordinatedClock
A `TimeSource` implementation for coordinated faster-than-real-time simulation. Receives sim time via `update(sim_time_ns)`, called by the service's `SimTimeUpdate` subscriber callback. All services using `CoordinatedClock` advance in lockstep when the Clock Service broadcasts a new timestamp. Phase 2 stub - `update()`, `nowNs()`, and `sleepUntil()` all throw until Phase 3 is implemented.

## Vehicle ID
A human-readable deployment name (`alpha`, `bravo`, `uav-01`) configured via `ORION_VEHICLE_ID`. Appears as the second segment of every per-vehicle topic: `orion/{vehicle_id}/{domain}/{topic}`.

## Topic Domain
The third segment of a per-vehicle topic path. Groups related topics and enables wildcard subscriptions. Initial vocabulary:

| Domain   | What lives here                                  |
|----------|--------------------------------------------------|
| `sensing` | Perception outputs - detections, tracked objects |
| `control` | Actuator commands, flight mode                   |
| `nav`     | Navigation state - position, velocity, attitude  |
| `system`  | Health, diagnostics, service status              |
| `clock`   | Simulation time infrastructure — `SimTimeUpdate` broadcasts |

---

## Gimbal Subsystem

## Gimbal
A custom 3-axis direct-drive stabilization platform that isolates the vision payload from drone kinematics. Mechanically: Drone Base → Yaw Motor → Roll Arm → Pitch Motor → Camera Payload. Controlled by a dedicated gimbal controller running high-speed inner loops, commanded by `GimbalService` over MAVLink.

## GimbalService
The Orion microservice running on the Jetson that owns the gimbal outer control loop. Registered on the `FrameScheduler` at 50 Hz. Reads nav state and targeting intent, computes desired gimbal attitude as a quaternion, and sends `GIMBAL_MANAGER_SET_ATTITUDE` commands to the gimbal controller over UART. Publishes gimbal attitude status on the Zenoh bus.

## Gimbal Controller
The dedicated embedded controller mounted on the gimbal frame (Storm32 BGC for v1). Owns the 1 kHz+ inner FOC loop, onboard IMU, encoder feedback, and yaw travel limit enforcement. Acts as the MAVLink Gimbal Device. Replaced by a custom PCB in a future revision.

## Gimbal Lock
A kinematic singularity in Euler-angle representations that occurs when two rotation axes align, causing loss of one degree of freedom. On a Drone Base → Yaw → Roll → Pitch topology, gimbal lock occurs near nadir (−90° pitch) - precisely the primary surveillance operating mode. Avoided by using quaternions throughout all internal math; see ADR-0014.

## NavState
A protobuf message (planned, Phase 2) published by the Nav Service on the Zenoh bus. Contains vehicle attitude (quaternion), geodetic position, and NED velocity derived from EKF sensor fusion. Consumed by `GimbalService` to compute world-frame pointing commands. In Phase 1, an equivalent state is derived from PX4 MAVLink telemetry by a shim adapter.

## Flight Controller (FC)
The embedded system responsible for vehicle state estimation via EKF (fusing IMU, barometer, GPS). In Phase 1, this is a PX4-based FC connected to the Jetson as a companion computer over MAVLink UART. In Phase 2, a custom FC publishes nav state natively on the Zenoh bus as the Nav Service, removing the MAVLink dependency. See ADR-0015.

## MAVLink Gimbal Protocol v2
The interface contract between `GimbalService` (Gimbal Manager role) and the gimbal controller (Gimbal Device role). Key messages: `GIMBAL_MANAGER_SET_ATTITUDE` (Jetson → controller), `GIMBAL_DEVICE_ATTITUDE_STATUS` (controller → Jetson). Carries attitude as a quaternion natively - no Euler conversion at the wire boundary. See ADR-0013.
