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
