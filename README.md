# Orion

Autonomy platform for the NVIDIA Jetson Orin Nano, developed in a VS Code devcontainer with a
Conan 2 / CMake / Ninja toolchain.

## Hardware targets

| Target | Architecture | Base image |
|--------|-------------|------------|
| Dev workstation | x86_64 | Ubuntu 22.04 |
| Jetson Orin Nano | arm64 | DeepStream 7.1 L4T |

## Quick start

1. Clone the repo and open it in VS Code
2. When prompted, click **Reopen in Container**
3. Wait for the container to finish — `post-create.sh` runs automatically and handles Conan install
   and CMake configure for both presets
4. Run the **CMake: Build All** task (`Ctrl+Shift+B`)

IntelliSense (clangd) activates after the first successful build generates
`build/Debug/compile_commands.json`.

For first-time setup on a Jetson Orin Nano, see [docs/orin-setup.md](docs/orin-setup.md).

## Repository layout

| Path | Contents |
|------|----------|
| `cmake/` | CMake helper modules (`tools.cmake` — format, lint, docs targets) |
| `conan/` | Conan profiles and custom recipes for zenoh-c and zenoh-cpp |
| `docker/` | Unified Dockerfile (selects base image by architecture) |
| `docs/` | Process documentation and architecture decision records |
| `libs/` | C++ library targets (transport, …) |
| `proto/` | Protobuf schema definitions |
| `scripts/` | Environment setup scripts (`initialize_conan.sh`) |

## Documentation

- [Development environment](docs/development-environment.md) — devcontainer internals, volume
  mounts, VS Code tasks, pre-commit hooks
- [Dependency management](docs/dependency-management.md) — Conan profiles, adding dependencies,
  lockfile workflow
- [Building and testing](docs/building-and-testing.md) — CMake presets, proto codegen, formatting,
  linting, Doxygen
- [Cross-compilation](docs/cross-compilation.md) — x86 dev vs Jetson deploy, arch profiles, static
  linking constraints
- [Versioning](docs/versioning.md) — git-tag-based semantic versioning
- [Orin setup](docs/orin-setup.md) — one-time Jetson configuration after flashing JetPack
- [Architecture decisions](docs/adr/) — ADR-0001 Zenoh transport, ADR-0002 DeepStream,
  ADR-0003 multi-platform build strategy
