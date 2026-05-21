# ADR 0003: Multi-platform build and deploy strategy

## Status
Accepted

## Context
Orion is developed on x86_64 workstations but deployed to a Jetson Orin Nano (ARM64). ADR 0002
established DeepStream as the Perception Service pipeline framework, running only on the Orin.

The devcontainer and deploy environments have fundamentally different requirements:

| Concern | Dev (x86_64) | Deploy (ARM64) |
|---|---|---|
| Base image | Lightweight Ubuntu | DeepStream L4T |
| DeepStream | Not needed | Required |
| TensorRT / CUDA | Not needed | Required |
| Build toolchain | Full (cmake, clang, conan) | Minimal or pre-built |
| Camera / inference | Mocked | Real hardware |

Using the full DeepStream image (`nvcr.io/nvidia/deepstream:7.1-gc-triton-devel`) as the dev base
adds ~15 GB of dead weight to an environment that never calls a DeepStream API.

## Decision
Split into two images:

1. **`docker/Dockerfile.devel`** — `ubuntu:22.04` base. Dev toolchain only (cmake, ninja, clang,
   conan, pre-commit). No DeepStream, no CUDA. Used for the devcontainer on x86.

2. **`docker/Dockerfile.deploy`** — `nvcr.io/nvidia/deepstream:7.1-triton-l4t` base. Runtime only.
   Used for ARM64 images deployed to the Orin Nano for HIL tests and real flights.

The Perception Service will expose an abstract interface. On x86 this is satisfied by a stub
implementation; on the Orin it is satisfied by the real DeepStream adapter. This keeps DeepStream
out of the dev build entirely.

## Build and deploy workflow (to be implemented)
- Dev builds: normal devcontainer workflow, no change.
- HIL/deploy builds: `docker buildx build --platform linux/arm64 -f docker/Dockerfile.deploy`
  using QEMU emulation on the host. Accepted as slow since HIL builds are infrequent.
- Deploy: built image pushed or SCP'd to the Orin Nano.

## Consequences
- `docker/Dockerfile.devel` is simplified significantly — no NVIDIA registry, much smaller image.
- A new `docker/Dockerfile.deploy` is needed (not yet written).
- The Perception Service interface must be defined before DeepStream integration begins so the mock
  and real implementations can be developed in parallel.
- QEMU must be available on the dev machine for ARM64 builds (`docker buildx` prerequisite).
- DeepStream-specific code is never compiled or linked in the dev environment.
