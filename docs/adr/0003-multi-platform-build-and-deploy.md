# ADR 0003: Multi-platform build and deploy strategy

## Status
Accepted — amended from original (unified Dockerfile, native Orin builds)

## Context
Orion is developed on x86_64 workstations but deployed to a Jetson Orin Nano (ARM64). ADR 0002
established DeepStream as the Perception Service pipeline framework, running only on the Orin.

The devcontainer and deploy environments have fundamentally different requirements:

| Concern | Dev (x86_64) | Deploy (ARM64) |
|---|---|---|
| Base image | Lightweight Ubuntu | DeepStream L4T |
| DeepStream | Not needed | Required |
| TensorRT / CUDA | Not needed | Required |
| Build toolchain | Full (cmake, clang, conan) | Full (same Dockerfile) |
| Camera / inference | Mocked | Real hardware |

Using the full DeepStream image (`nvcr.io/nvidia/deepstream:7.1-triton-l4t`) as the dev base
adds ~15 GB of dead weight to an environment that never calls a DeepStream API.

## Decision

### Unified Dockerfile with architecture detection
A single `docker/Dockerfile` uses BuildKit's `TARGETARCH` to select the base image automatically:

- **AMD64** → `ubuntu:22.04` — lightweight dev base, no NVIDIA dependencies
- **ARM64** → `nvcr.io/nvidia/deepstream:7.1-triton-l4t` — L4T base with DeepStream, CUDA, TensorRT

The full build toolchain (cmake, ninja, clang-18, conan, ccache) is installed on top of both bases
identically. Builds on the Orin are native — no QEMU emulation.

### Abstract interface for hardware-dependent code
The Perception Service exposes an abstract interface. On x86 this is satisfied by a stub
implementation; on the Orin it is satisfied by the real DeepStream adapter. This keeps DeepStream
out of the dev build entirely and allows transport and business logic to be built and tested on x86.

## Dependency conflicts with L4T

The `deepstream:7.1-triton-l4t` image bundles Triton Inference Server, which ships its own versions
of protobuf (~3.21.x) and abseil. These overlap with our Conan dependencies.

| Dep | Our version | L4T/Triton version | Risk |
|---|---|---|---|
| protobuf | 5.29.3 (Conan) | ~3.21.x (Triton system) | Symbol conflict if both loaded |
| abseil | 20240722.0 (Conan) | Triton-bundled | Symbol conflict if both loaded |
| zenoh-c/cpp | 1.9.0 (Conan) | Not present in L4T | No conflict |

**Mitigation:** Conan builds protobuf and abseil as static libraries by default (`shared=False`).
Static linking embeds symbols directly into our binaries so there is no runtime dynamic linker
conflict with Triton's versions. This assumption must be verified explicitly if either dep is ever
switched to shared linking.

The abstract interface pattern also limits the conflict surface — DeepStream APIs are only called
in the Perception Service adapter, which does not directly expose protobuf types across the
DeepStream/application boundary.

## Consequences
- `docker/Dockerfile` is the single source of truth for both dev and deploy environments.
- The Orin devcontainer is opened the same way as x86 — VS Code detects ARM64 and BuildKit selects
  the L4T base automatically.
- QEMU is not required. ARM64 builds are native on the Orin.
- DeepStream-specific code is never compiled or linked in the dev environment.
- Static linking for protobuf and abseil must be preserved. If shared linking is ever introduced,
  protobuf version compatibility with the Triton runtime must be validated on the Orin explicitly.
- A deploy-specific Conan profile (`conan/profiles/arm64/deploy`) should be added when
  DeepStream integration begins to enable Orin-specific CMake flags
  (`ORION_ENABLE_DEEPSTREAM`, `CMAKE_CUDA_ARCHITECTURES=87`, TensorRT paths).
