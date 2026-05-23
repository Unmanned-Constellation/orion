# Cross-Compilation and Multi-Platform Build

Orion is developed on x86_64 workstations and deployed to a Jetson Orin Nano (ARM64). The toolchain
is designed so both environments use the same Dockerfile, the same VS Code workflow, and the same
`initialize_conan.sh` script — the architecture is detected automatically at each stage.

For the full rationale behind this strategy, see
[ADR-0003](adr/0003-multi-platform-build-and-deploy.md).

## Dockerfile

`docker/Dockerfile` uses BuildKit's `TARGETARCH` argument to select the base image:

| `TARGETARCH` | Base image | Approximate size |
|---|---|---|
| `amd64` | `ubuntu:22.04` | ~80 MB |
| `arm64` | `nvcr.io/nvidia/deepstream:7.1-triton-l4t` | ~15 GB |

The full build toolchain (clang-18, cmake, ninja, conan, ccache) is installed identically on top
of both bases. No QEMU emulation is used — builds on the Orin are native ARM64.

## Conan profile selection

`initialize_conan.sh` detects the host architecture at runtime:

```bash
case "$(uname -m)" in
    x86_64)           ARCH=x86_64 ;;
    aarch64 | arm64)  ARCH=arm64  ;;
esac
```

It then installs using `conan/profiles/${ARCH}/release` and `conan/profiles/${ARCH}/debug`.

The ARM64 profiles set `arch=armv8`, which is Conan's identifier for the ARMv8-A ISA
(aarch64/arm64).

## Zenoh architecture mapping

The `zenoh-c` custom recipe (`conan/recipes/zenoh-c/conanfile.py`) maps Conan arch identifiers to
the filenames used by zenoh-c's GitHub Releases:

| Conan `arch` | GitHub Release asset suffix |
|---|---|
| `x86_64` | `x86_64-unknown-linux-gnu` |
| `armv8` | `aarch64-unknown-linux-gnu` |

This mapping is what allows the same recipe to fetch the correct pre-built binary for each target.

## Building on the Jetson

The workflow on the Orin is identical to x86:

1. Clone the repo and open in VS Code
2. Click **Reopen in Container** — VS Code detects the ARM64 host and BuildKit selects the L4T
   base image automatically
3. Wait for `post-create.sh` to complete — the first run pulls the L4T image (~15 GB) and builds
   any Conan packages not yet in the cache, which takes significantly longer than x86
4. Run **CMake: Build All**

One-time Jetson configuration (Docker runtime, GPU access) is documented separately in
[orin-setup.md](orin-setup.md).

## DeepStream availability

DeepStream APIs are only present in the L4T base image. Per [ADR-0002](adr/0002-deepstream-over-raw-gstreamer.md),
all hardware-dependent code sits behind an abstract interface. The DeepStream adapter is never
compiled or linked in the x86 dev environment — only the stub implementation is used there.

## Cross-compiling from x86 to ARM64

The `cross-arm64` CMake preset and its companion toolchain file
(`cmake/toolchains/aarch64-linux-gnu.cmake`) allow an x86 machine to produce ARM64 binaries
without needing a Jetson in front of you. This is what the CI job uses to verify that ARM64
builds do not break silently.

### Prerequisites

```bash
# GCC cross package provides the aarch64-linux-gnu sysroot headers and linker
apt-get install -y gcc-aarch64-linux-gnu
```

### Conan install (cross)

Cross-compilation requires a two-profile Conan install that distinguishes the *build* machine
(x86, runs the compiler) from the *host* machine (ARM64, runs the resulting binary):

```bash
conan install . \
  --build=missing \
  --profile:build=conan/profiles/x86_64/debug \
  --profile:host=conan/profiles/arm64/debug \
  --output-folder=build/CrossArm64 \
  --lockfile=conan.lock
```

The `--output-folder` flag bypasses `cmake_layout()` and places the generated
`conan_toolchain.cmake` directly in `build/CrossArm64/`, where the preset expects it.

### Build

```bash
cmake --preset cross-arm64
cmake --build --preset cross-arm64
```

There is no `ctest` step — the resulting binaries target `aarch64-linux-gnu` and cannot execute
on x86.

### Toolchain file

`cmake/toolchains/aarch64-linux-gnu.cmake` sets:
- `CMAKE_C_COMPILER_TARGET` and `CMAKE_CXX_COMPILER_TARGET` to `aarch64-linux-gnu` — tells
  clang-18 which target triple to emit code for
- `CMAKE_SYSROOT` to `/usr/aarch64-linux-gnu` — the cross sysroot from `gcc-aarch64-linux-gnu`
- `CMAKE_FIND_ROOT_PATH_MODE_*` — restricts `find_*` calls to the sysroot only
- Then `include()`s Conan's generated toolchain for package paths and compiler flags

The CMake preset sets `CMAKE_SYSTEM_NAME=Linux` and `CMAKE_SYSTEM_PROCESSOR=aarch64` via
`cacheVariables`, which prevents CMake from trying to run compiler detection tests natively.

### CI

The **Cross-compile (ARM64)** CI job installs `gcc-aarch64-linux-gnu`, runs the two-profile
Conan install with a separate cache bucket (`conan-arm64-*`), and builds. ARM64 packages are
cached independently from x86 packages to avoid key collisions.

## Static linking constraint

`protobuf` and `abseil` must remain statically linked (`shared=False`, which is the Conan default).

The DeepStream L4T image bundles the Triton Inference Server, which ships its own versions of both
libraries. Static linking embeds symbols directly into Orion's binaries so the dynamic linker never
sees both versions simultaneously, avoiding runtime symbol conflicts.

**If shared linking is ever introduced for either library, compatibility with the Triton runtime on
the Orin must be validated explicitly before merging.**

## Future: deploy profile

[ADR-0003](adr/0003-multi-platform-build-and-deploy.md) notes that a
`conan/profiles/arm64/deploy` profile should be added when DeepStream integration begins. This
profile will enable Orin-specific CMake flags such as `ORION_ENABLE_DEEPSTREAM`,
`CMAKE_CUDA_ARCHITECTURES=87`, and TensorRT library paths that are not needed during normal
development.
