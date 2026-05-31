# Orion

Autonomy platform for the NVIDIA Jetson Orin Nano, developed in a VS Code devcontainer with a
Conan 2 / CMake / Ninja toolchain.

## Hardware targets

| Target | Architecture | Base image |
|--------|-------------|------------|
| Dev workstation | x86_64 | Ubuntu 22.04 |
| Jetson Orin Nano | arm64 | DeepStream 7.1 L4T |

## Requirements

| Requirement | Minimum | Notes |
|---|---|---|
| Docker | 24.0 | Docker Desktop or Docker Engine |
| VS Code | Any | With the [Dev Containers](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-containers) extension |
| Disk space | 10 GB (x86) / 25 GB (arm64) | arm64 pulls the 15 GB DeepStream L4T base image |
| RAM | 8 GB | 16 GB recommended for parallel builds |

On Windows, WSL2 with kernel **6.1 or later** is required. Earlier kernels do not support the
`personality(ADDR_NO_RANDOMIZE)` syscall that ThreadSanitizer uses.

For Jetson Orin Nano setup, see [docs/orin-setup.md](docs/orin-setup.md).

## Quick start

1. Clone the repo and open it in VS Code
2. When prompted, click **Reopen in Container**
3. Wait for the container to finish - `post-create.sh` runs automatically and handles Conan install
   and CMake configure for both presets
4. Run the **CMake: Build All** task (`Ctrl+Shift+B`)

IntelliSense (clangd) activates after the first successful build generates
`build/Debug/compile_commands.json`.

For first-time setup on a Jetson Orin Nano, see [docs/orin-setup.md](docs/orin-setup.md).

## Repository layout

| Path | Contents |
|------|----------|
| `cmake/` | CMake helper modules - `tools.cmake` (format, lint, coverage, docs targets), `toolchains/` (cross-compile toolchain files) |
| `conan/` | Conan profiles and custom recipes for zenoh-c and zenoh-cpp |
| `docker/` | Unified Dockerfile (selects base image by architecture) |
| `docs/` | Process documentation and architecture decision records |
| `libs/` | C++ library targets (transport, …) |
| `proto/` | Protobuf schema definitions |
| `scripts/` | Environment setup scripts (`initialize_conan.sh`) |
| `tests/` | GoogleTest unit tests and libFuzzer fuzz targets (`tests/fuzz/`) |

## Documentation

- [Development environment](docs/development-environment.md) - devcontainer internals, volume
  mounts, VS Code tasks, pre-commit hooks
- [Building and testing](docs/building-and-testing.md) - CMake presets, proto codegen, formatting,
  linting, sanitizers, coverage, fuzzing, Doxygen
- [Testing](docs/testing.md) - unit tests, sanitizers (ASan/UBSan/TSan), coverage, fuzzing with
  libFuzzer: philosophy, writing targets, interpreting output
- [Coding standards](docs/coding-standards.md) - C++ Core Guidelines, HiCPP, CERT, naming
  conventions, hard limits enforced by clang-tidy
- [CI/CD](docs/ci-cd.md) - GitHub Actions jobs, dependency caching, what blocks a merge
- [Dependency management](docs/dependency-management.md) - Conan profiles, adding dependencies,
  lockfile workflow
- [Cross-compilation](docs/cross-compilation.md) - x86 dev vs Jetson deploy, arch profiles, static
  linking constraints
- [Versioning](docs/versioning.md) - git-tag-based semantic versioning, CHANGELOG generation
- [Orin setup](docs/orin-setup.md) - one-time Jetson configuration after flashing JetPack
- [Architecture decisions](docs/adr/) - ADR-0001 Zenoh transport, ADR-0002 DeepStream,
  ADR-0003 multi-platform build strategy, ADR-0004 transport abstraction layer,
  ADR-0005 topic naming scheme, ADR-0006 edge network service (multi-protocol gateway),
  ADR-0007 public API service, ADR-0008 FrameScheduler (major/minor frame executor),
  ADR-0009 sim clock, ADR-0010 crash handler, ADR-0011 logging and health reporting,
  ADR-0012 gimbal signal routing, ADR-0013 gimbal control loop separation,
  ADR-0014 gimbal kinematics, ADR-0015 gimbal FC integration,
  ADR-0016 reactive vs. periodic execution model, ADR-0017 simulation driving model
