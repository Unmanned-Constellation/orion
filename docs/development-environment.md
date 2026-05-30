# Development Environment

The devcontainer is the only supported development environment. Everything below describes what
`.devcontainer/devcontainer.json` and `post-create.sh` set up and why.

## Base image

`docker/Dockerfile` uses BuildKit's `TARGETARCH` argument to select the base image automatically:

| Architecture | Base image | Notes |
|---|---|---|
| `amd64` (x86_64) | `ubuntu:22.04` | Lightweight; no NVIDIA dependencies |
| `arm64` | `nvcr.io/nvidia/deepstream:7.1-triton-l4t` | Full L4T with DeepStream, CUDA, TensorRT |

The full build toolchain (cmake, ninja, clang-18, conan, ccache) is installed identically on top
of both bases. See [ADR-0003](adr/0003-multi-platform-build-and-deploy.md) for the rationale.

## apt packages

All packages are installed in a single `RUN` layer in `docker/Dockerfile`.

| Package | Purpose |
|---|---|
| `cmake` | Build system - configures and drives the ninja build |
| `make` | Required by some Conan-managed dependencies (e.g. zlib) that use the Unix Makefiles generator |
| `ninja-build` | Fast parallel build backend used by all CMake presets |
| `git` | Required by CMake's version detection (`git describe`) and by pre-commit |
| `python3-pip` | Installs Conan, pre-commit, gersemi, and Sphinx deps |
| `clang-18` | C++ compiler (`clang`, `clang++`) |
| `clang-format-18` | Code formatter - enforced by pre-commit hook and CI |
| `clang-tidy-18` | Static analyser - runs as part of the debug build in CI |
| `clangd-18` | Language server powering VS Code IntelliSense |
| `libclang-rt-18-dev` | Compiler-RT runtime libraries: ASan, UBSan, TSan, and libFuzzer |
| `llvm-18` | LLVM tools used for coverage: `llvm-profdata`, `llvm-cov` |
| `ccache` | Compiler cache - keeps incremental rebuilds fast across container rebuilds |
| `doxygen` | Parses C++ doc comments and emits XML consumed by Sphinx/Breathe |
| `openssh-client` | Allows SSH-based git operations (push, fetch) using the host's forwarded agent |

`protoc` is intentionally absent - it is managed by Conan (`tool_requires("protobuf/5.29.3")`) to guarantee the compiler version matches the runtime library exactly. The Conan-managed `protoc` is available after running `initialize_conan.sh` via the `conanbuild.sh` environment script generated into `build/Debug/generators/` and `build/Release/generators/`.

## Volume mounts

Mounts keep expensive state outside the container so it survives rebuilds and image updates.

| Host path | Container path | Type | Purpose |
|---|---|---|---|
| `~/.cache/orion-ccache` | `/ccache` | bind | Compiler cache - incremental rebuilds stay fast across container rebuilds |
| `~/.cache/orion-deps` | `/root/.conan2` | bind | Conan package cache - avoids re-downloading dependencies |
| `orion-vscode-server` | `/root/.vscode-server` | volume | VS Code server and installed extensions |
| `orion-cmake-tools` | `/root/.local/share/CMakeTools` | volume | CMake Tools extension state |
| `orion-claude-profile` | `/root/.claude` | volume | Claude Code configuration |
| `orion-agents-profile` | `/root/.agents` | volume | Agents configuration |
| `~/.ssh` | `/root/.ssh` | bind (read-only) | Host SSH keys forwarded into container |

The two bind mounts under `~/.cache/` are created by `initializeCommand` before the container
starts, so Docker never creates them as root-owned directories.

## Git identity

`GIT_AUTHOR_NAME`, `GIT_AUTHOR_EMAIL`, `GIT_COMMITTER_NAME`, and `GIT_COMMITTER_EMAIL` are passed
into the container via `remoteEnv`. This means commits made inside the devcontainer carry the same
identity as commits made on the host without any extra git config.

`SSH_AUTH_SOCK` is also forwarded so SSH-based git operations work transparently.

## Post-create setup

`post-create.sh` runs once after the container is created:

```bash
pre-commit install
bash scripts/environment/initialize_conan.sh
```

`pre-commit install` registers the formatting hooks into `.git/hooks/` so they run on every
`git commit`. `initialize_conan.sh` does the heavy lifting: it detects the host architecture,
registers the vendored Zenoh recipes as a local remote, runs `conan install` for both the debug
and release profiles, and configures both CMake presets. See [dependency management](dependency-management.md)
for a full walkthrough of what `initialize_conan.sh` does.

The **Conan: Install** VS Code task re-runs `initialize_conan.sh` on demand and is the correct
way to refresh the environment after changing `conanfile.py` or switching machines.

## Pre-commit hooks

Three hooks are configured in `.pre-commit-config.yaml`:

| Hook | Stage | Trigger | Effect |
|---|---|---|---|
| `clang-format` | `pre-commit` | Any `*.cpp` / `*.hpp` staged | Fails if any C++ file needs reformatting |
| `gersemi` | `pre-commit` | Any `CMakeLists.txt` / `*.cmake` staged | Fails if any CMake file needs reformatting |
| `conan-lockfile` | `pre-commit` | `conanfile.py` staged | Fails if a dependency line changed but `conan.lock` was not re-staged |

The formatting hooks do not auto-fix. Use the **Format: C++** and **Format: CMake** VS Code tasks
to fix before committing.

## Sanitizer and coverage tools

`libclang-rt-18-dev` (ASan, UBSan, TSan, libFuzzer runtime libraries) and `llvm-18`
(`llvm-profdata-18`, `llvm-cov-18`) are included in the devcontainer image. The sanitize,
tsan, coverage, and fuzz CMake presets work without any manual package installation.

`devcontainer.json` passes `--security-opt seccomp=unconfined` via `runArgs`. This removes the
Docker seccomp restriction that would otherwise block ThreadSanitizer's `personality()` syscall,
allowing `ctest --preset tsan` to run inside the container. On WSL2 with an older kernel this
syscall may still fail at the kernel level - see [testing.md](testing.md) for details.

## VS Code extensions

All extensions are declared in `devcontainer.json` and installed automatically when the container is created.

| Extension | ID | Purpose |
|---|---|---|
| clangd | `llvm-vs-code-extensions.vscode-clangd` | C++ IntelliSense, go-to-definition, inline diagnostics, and format-on-save via clang-format |
| CodeLLDB | `vadimcn.vscode-lldb` | Native debugger - launch and attach to C++ binaries with full LLDB support |
| CMake Tools | `ms-vscode.cmake-tools` | CMake integration - configure, build, and select presets from the status bar |
| vscode-proto3 | `zxh404.vscode-proto3` | Syntax highlighting and formatting for `.proto` files |
| Claude Code | `anthropic.claude-code` | AI coding assistant |
| Live Server | `ritwickdey.LiveServer` | One-click local HTTP server for previewing the generated Sphinx docs site (`docs/_build/html/`) |

## clangd and IntelliSense

clangd is configured in `.vscode/settings.json` to read the compilation database from the
active CMake preset's build directory via `${command:cmake.buildDirectory}`. This resolves
dynamically - switching presets in the CMake Tools status bar automatically points clangd at
the correct `compile_commands.json` without any manual steps.

`compile_commands.json` is generated by CMake (`CMAKE_EXPORT_COMPILE_COMMANDS ON`) and only
exists after a configure step. Run **CMake: Configure** (or **CMake: Build All**) once after
opening the container to fully activate IntelliSense. After switching presets, run the
configure step for the new preset and then restart the language server
(**clangd: Restart language server** in the Command Palette) if diagnostics do not update.

## VS Code tasks

All tasks are defined in `.vscode/tasks.json`. Run them via **Terminal → Run Task…** or the
keyboard shortcut for the default build task.

| Task | What it does |
|---|---|
| **Conan: Install** | Runs `initialize_conan.sh` - wipes `build/`, installs deps, configures both presets |
| **Conan: Sync Dependencies** | Same but passes `--no-clean` - syncs deps without wiping the Ninja build cache |
| **Conan: Install (Offline)** | Same as **Conan: Install** with `--no-remote`; no network access required |
| **Conan: Reinstall (Clean)** | Purges the entire local Conan cache, then reinstalls from scratch |
| **Conan: Create Lockfile** | Regenerates `conan.lock` from the current `conanfile.py` |
| **CMake: Configure** | Runs `cmake --preset <debug\|release>` |
| **CMake: Build All** | Configures and builds all targets for the selected preset (default build task) |
| **CMake: Build Target…** | Configures then builds a single named CMake target |
| **Format: C++** | Runs clang-format in-place on all C++ source files |
| **Format: CMake** | Runs gersemi in-place on all CMake files |
| **Lint: C++** | Runs `run-clang-tidy` on `libs/` and `proto/` source |
| **Docs: Build** | Runs Doxygen then Sphinx via the `docs` CMake target - HTML site written to `docs/_build/html/` |
| **CI: Check Format (C++)** | Dry-run clang-format - fails if any file needs reformatting |
| **CI: Check Format (CMake)** | Dry-run gersemi - fails if any CMake file needs reformatting |
| **CI: Check All** | Runs both CI format checks in parallel |
