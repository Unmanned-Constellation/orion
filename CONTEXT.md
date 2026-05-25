# Orion — Domain Glossary

## Orion
The on-board software system running on the Jetson Orin Nano. Composed of multiple microservices that communicate over the Zenoh Bus.

## Microservice
A C++ process that publishes and/or subscribes on the Zenoh Bus. Each microservice has a single responsibility and is independently deployable.

## Zenoh Bus
The shared-memory pub-sub transport layer connecting microservices on the same Jetson board. Also routes published topics to off-board consumers over WiFi/radio using the same API and topic hierarchy. Not a broker — there is no intermediary in the data path.

## Perception Service
The microservice responsible for camera ingestion, hardware-accelerated TensorRT inference via DeepStream, and publishing Detection Metadata to the Zenoh Bus.

## Detection Metadata
The structured output of the Perception Service: bounding boxes, class IDs, and confidence scores derived from `NvDsObjectMeta`. Serialized and published as a Zenoh topic.

## Decision Service
A custom C++ microservice that subscribes to Detection Metadata from the Zenoh Bus and produces flight decisions or control commands.

## Off-board Consumer
A remote process — ground station or companion computer — that subscribes to Zenoh topics over WiFi or radio. Indistinguishable from an on-board subscriber at the API level.

## Middleware Service
The microservice that hosts the Zenoh Router and owns the Zenoh bus configuration. Not application logic — configuration and process lifecycle only.

## Topic Schema
The Protobuf-defined message contracts published on Zenoh topics. `.proto` files are the canonical interface between all microservices, on-board and off-board. Schema files live in `proto/` and every service builds against the generated C++ code.

## Zenoh Router
A lightweight Zenoh process running on the Jetson that bridges the local shared-memory bus to the network transport for off-board consumers. Not application code — configuration only.

## Session
The entry point to the transport layer. Created once per microservice at startup with a `SessionConfig` (vehicle ID, service name) and an injected `Clock`. Factory for `Publisher<T>` and `Subscriber<T>` instances.

## Publisher
A typed handle returned by `Session::advertise<T>(topic)`. Calling `publish(msg)` serializes the message, stamps an `orion::v1::Header` (via the injected `Clock`), wraps everything in an `orion::v1::Envelope`, and transmits it on the Zenoh Bus.

## Subscriber
A lifetime handle returned by `Session::subscribe<T>(topic, callback)`. Holds the subscription active until destroyed. The callback receives the typed message and a `MessageHeader`.

## MessageHeader
A plain C++ struct (`published_at_ns`, `source_id`) delivered to every subscriber callback. Carries transport-level metadata without exposing protobuf types in the callback signature. `published_at_ns` is the time the transport published the message (from the injected Clock). `source_id` is the publishing service's name.

## Envelope
An internal protobuf message (`proto/orion/v1/envelope.proto`) wrapping every transmitted payload. Contains the `Header`, serialized payload bytes, and a `type_url` for runtime type validation. Never visible to service authors — created and consumed exclusively by the transport layer.


## HIL (Hardware-In-the-Loop)
A test mode where real hardware is part of the running system. Uses `WallClock` — time runs at wall speed because the hardware expects it.

## SIL (Software-In-the-Loop)
A test mode where no physical hardware is present — all hardware dependencies are simulated in software. Services run as real processes. Compatible with both `WallClock` and a simulated `Clock`.

## Batch Simulation
A SIL mode that replays missions repeatedly at accelerated speed for Monte Carlo-style analysis. How simulated time is coordinated across microservices is an open design question.

## Clock
An abstract interface (`nowNs() → uint64_t`) in `orion_clock`, injected into each microservice at startup. Microservices use it for all timestamping — domain timestamps in proto message fields and transport metadata. Services have no knowledge of whether they are running against a `WallClock` or a `SimClock`; the injected implementation determines the time mode.

## WallClock
Concrete `Clock` implementation backed by `std::chrono::system_clock`. Injected in production and real-time SIL.

## SimClock
A `Clock` implementation for SIL and Batch Simulation. Subscribes to the Clock Service topic over Zenoh to receive coordinated sim time. Not yet implemented.

## Vehicle ID
A human-readable deployment name (`alpha`, `bravo`, `uav-01`) configured via `ORION_VEHICLE_ID`. Appears as the second segment of every per-vehicle topic: `orion/{vehicle_id}/{domain}/{topic}`.

## Topic Domain
The third segment of a per-vehicle topic path. Groups related topics and enables wildcard subscriptions. Initial vocabulary:

| Domain   | What lives here                                  |
|----------|--------------------------------------------------|
| `sensing` | Perception outputs — detections, tracked objects |
| `control` | Actuator commands, flight mode                   |
| `nav`     | Navigation state — position, velocity, attitude  |
| `system`  | Health, diagnostics, service status              |
