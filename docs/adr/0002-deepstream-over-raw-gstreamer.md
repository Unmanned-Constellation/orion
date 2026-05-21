# ADR 0002: DeepStream over raw GStreamer + TensorRT

## Status
Amended — see ADR 0003 for deployment strategy and dev/deploy image split.

## Context
The Perception Service requires hardware-accelerated inference on the Jetson Orin Nano. Two approaches were considered:

- **Raw GStreamer + TensorRT** — full control over the inference integration and metadata schema. TensorRT libraries are unavailable on x86_64 without a GPU, so hardware-dependent pipeline stages cannot run in the devcontainer.
- **DeepStream SDK** — NVIDIA's opinionated inference pipeline framework built on GStreamer. Provides `nvinfer`, `nvtracker`, and `NvDsObjectMeta` out of the box.

## Decision
Use DeepStream as the pipeline framework for the Perception Service on the Orin Nano.

The Perception Service will be designed behind an abstract interface so that hardware-dependent
DeepStream stages can be swapped for a stub/mock implementation at compile time. This allows the
transport layer and business logic to be built and tested on x86 without any DeepStream dependency.

## Consequences
- Detection metadata is sourced from `NvDsObjectMeta`. A thin adapter layer translates this to the Zenoh topic schema.
- The Perception Service interface is defined independently of DeepStream so it can be mocked in the dev environment.
- The devcontainer uses a lightweight base image (see ADR 0003). DeepStream is only present in the deploy image.
- Camera ingestion (`nvarguscamerasrc`) and TensorRT inference (`nvinfer`) stages are hardware-dependent and can only be fully tested on the Jetson.
- Hardware-agnostic code (Zenoh IPC, CMake scaffolding, serialization) is built and tested in the devcontainer without Jetson hardware.
