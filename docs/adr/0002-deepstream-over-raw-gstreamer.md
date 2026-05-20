# ADR 0002: DeepStream over raw GStreamer + TensorRT

## Status
Accepted

## Context
The Perception Service requires hardware-accelerated inference on the Jetson Orin Nano. Two approaches were considered:

- **Raw GStreamer + TensorRT** — full control over the inference integration and metadata schema. TensorRT libraries are unavailable on x86_64 without a GPU, so hardware-dependent pipeline stages cannot run in the devcontainer.
- **DeepStream SDK** — NVIDIA's opinionated inference pipeline framework built on GStreamer. Provides `nvinfer`, `nvtracker`, and `NvDsObjectMeta` out of the box. NVIDIA ships a prebuilt multi-arch Docker image (`nvcr.io/nvidia/deepstream`) that runs on both x86_64 (devcontainer) and ARM64 (Jetson).

## Decision
Use DeepStream as the pipeline framework for the Perception Service.

## Consequences
- Detection Metadata is sourced from `NvDsObjectMeta`. A thin adapter layer translates this to the Zenoh topic schema.
- The devcontainer base image is `nvcr.io/nvidia/deepstream`, giving a consistent environment across x86_64 and ARM64.
- Camera ingestion (`nvarguscamerasrc`) and TensorRT inference (`nvinfer`) stages are hardware-dependent and can only be fully tested on the Jetson.
- Hardware-agnostic code (Zenoh IPC, CMake scaffolding, serialization) can be built and tested in the devcontainer without Jetson hardware.
