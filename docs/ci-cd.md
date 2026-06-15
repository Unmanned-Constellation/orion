# CI/CD

Continuous integration runs on GitHub Actions. Three workflows exist:

| Workflow file | Triggers | Purpose |
|---|---|---|
| `.github/workflows/ci.yml` | All PRs | Build, test, lint, sanitize, coverage, fuzz, docs, proto schema |
| `.github/workflows/changelog.yml` | Push of `v*` tag | Generate CHANGELOG and create GitHub Release |
| `.github/workflows/docs.yml` | Push to `main`, manual dispatch | Build Sphinx/Doxygen site and deploy to GitHub Pages |

## CI container image

All jobs run inside a pre-built container image published to GitHub Container
Registry (GHCR):

```
ghcr.io/unmanned-constellation/orion/ci:latest
```

The image is built from `.devcontainer/Dockerfile` and published as a multi-platform manifest
covering both `linux/amd64` and `linux/arm64`. The Dockerfile selects the base automatically
via `TARGETARCH`: `ubuntu:22.04` for amd64, `nvcr.io/nvidia/deepstream:7.1-triton-l4t` for
arm64. The same toolchain layer (clang-18, cmake, ninja, ccache, conan, clang-tidy,
clang-format, libclang-rt, llvm, doxygen, sphinx, gersemi, buf) is installed on top of
whichever base is selected.

`ci-image.yml` runs three jobs: `build-amd64` on an `ubuntu-22.04` runner, `build-arm64`
natively on an `ubuntu-22.04-arm` runner (avoiding slow QEMU emulation for the L4T layer),
and `merge` which combines the two platform digests into the `orion/ci:latest` manifest.
Docker GHA layer caching is scoped per-platform (`scope=amd64`, `scope=arm64`) to prevent
cross-platform cache collisions.

**Push policy:** on `pull_request`, both build jobs run to validate the Dockerfile on each
platform but do not push — the `merge` job is skipped entirely on PRs. Pushes to `:latest`
only happen on merge to `main`. This prevents an unreviewed Dockerfile change from
overwriting the image used by concurrent CI runs.

**Conan packages are baked into the image.** During the image build, `conan install` runs
for all profiles relevant to that platform (amd64: debug + release; arm64: debug). Packages
are stored in `CONAN_HOME=/opt/conan`, which is outside `$HOME` and therefore unaffected by
GitHub Actions' home-directory mount (`/github/home`). CI jobs find pre-built packages
instantly — `conan install` takes ~2 seconds instead of 20-30 minutes. The image is
automatically rebuilt whenever `conanfile.py`, `conan.lock`, or anything under `conan/`
changes.

```{mermaid}
flowchart LR
    classDef trigger fill:#e8f5e9,stroke:#2e7d32,stroke-width:2px,color:#000
    classDef action fill:#e1f5fe,stroke:#01579b,stroke-width:2px,color:#000
    classDef merge fill:#f3e5f5,stroke:#4a148c,stroke-width:2px,color:#000

    T1["Dockerfile Changed<br/>(or Manual Run)"]:::trigger --> B1("Build amd64<br/>(ubuntu-22.04)"):::action
    T1 --> B2("Build arm64 / L4T<br/>(ubuntu-22.04-arm)"):::action
    B1 & B2 --> M("Merge manifest<br/>orion/ci:latest"):::merge
```

## Custom actions

### `.github/actions/setup-builder`

Composite action used by every build job. It configures ccache and Conan inside the CI
container.

**Inputs**

| Input | Required | Default | Description |
|---|---|---|---|
| `conan-profile` | yes | - | Path to the Conan host profile (e.g. `conan/profiles/x86_64/debug`) |
| `cache-key-prefix` | no | `conan` | Prefix used for ccache keys |

**Steps (in order)**

| Step | What it does |
|---|---|
| Pin CONAN_HOME | Writes `CONAN_HOME=/opt/conan` to `$GITHUB_ENV` — GitHub Actions overrides `HOME=/github/home` inside containers, which causes Conan to ignore the Docker `ENV` and fall back to `$HOME/.conan2`; this step re-pins it |
| Configure ccache | Sets `CMAKE_C_COMPILER_LAUNCHER=ccache`, `cache_dir=$HOME/.cache/ccache`, `base_dir=$GITHUB_WORKSPACE`, caps at 1 GB |
| Compute ccache key | Writes the exact cache key to `$GITHUB_OUTPUT` so the calling job can save ccache after the build |
| Restore ccache | `~/.cache/ccache` keyed on `<prefix>-<os>-<sha>`, restores from most recent prior run |
| Configure Conan profile | `conan profile detect --force` - picks up clang-18 via `CC`/`CXX` |
| Register local recipes remote | Adds `conan/` as `orion-local` (priority 0, `local-recipes-index` type) |
| Install dependencies | `conan install --profile=<conan-profile> --lockfile=conan.lock` — completes in ~2 seconds against the pre-baked image cache |

Each calling job adds a **Save ccache** step as its final step (after the build), using
`steps.setup.outputs.ccache-key`. This ensures compiled object files are cached even if
tests or lint fail.

The ccache `cache_dir` is explicitly set to `~/.cache/ccache` to override the
`CCACHE_DIR=/ccache` environment variable baked into the container image (which is a volume
mount path in the devcontainer, not available in CI).

## CI jobs

The job dependency graph is:

```{mermaid}
flowchart LR
    classDef trigger fill:#e8f5e9,stroke:#2e7d32,stroke-width:2px,color:#000
    classDef container fill:#e1f5fe,stroke:#01579b,stroke-width:2px,color:#000
    classDef gate fill:#f5f5f5,stroke:#9e9e9e,stroke-width:2px,color:#000

    Trigger["Push to main / PR"]:::trigger

    Trigger --> Fmt["Format<br/>(C++ & CMake)"]:::container
    Trigger --> Pro["Proto Schema<br/>(Lint & Break)"]:::container
    Trigger --> Doc["Docs Coverage<br/>(Doxygen/Sphinx)"]:::container

    Gate1(("All<br/>Pass")):::gate

    Fmt & Pro & Doc --> Gate1

    Gate1 --> BldDbg["Build & Lint<br/>(Debug)"]:::container
    Gate1 --> BldRel["Build & Test<br/>(Release)"]:::container
    Gate1 --> BldArm["Build & Test<br/>(ARM64)"]:::container

    BldDbg --> San["Sanitize<br/>(ASan + UBSan)"]:::container
    BldDbg --> TSan["ThreadSanitizer"]:::container
    BldDbg --> Cov["Coverage<br/>(Min 80%)"]:::container
    BldDbg --> Fuz["Fuzz<br/>(Smoke Test)"]:::container
    BldDbg --> Bnch["Benchmark<br/>(Compile Only)"]:::container
```

`format`, `docs`, and `proto` run in parallel. `build` and `build-release` start once all
three pass. `sanitize`, `tsan`, `coverage`, `fuzz`, and `bench` all `needs: [build]` — they
start after the debug build completes and restore its warm Conan cache, avoiding a cold
dependency rebuild on every run.

Every build job sets `CC=clang-18` and `CXX=clang++-18` so Conan and CMake use clang rather
than the runner's default GCC - required because Conan injects `-stdlib=libstdc++` and GCC
rejects that flag.

All jobs run inside the CI container image. The multi-platform manifest means Docker
automatically pulls the correct variant — amd64 on `ubuntu-22.04` runners, arm64 (L4T) on
`ubuntu-22.04-arm` runners.

### Format

Checks all C++ and CMake files are correctly formatted. No build required - fastest failing job.
Runs in the CI container; clang-format-18 and gersemi are pre-installed.

| Check | Tool | Command |
|---|---|---|
| C++ formatting | clang-format-18 | `--dry-run --Werror` on `*.cpp`/`*.hpp` in `benchmarks/`, `libs/`, `proto/`, `tests/` |
| CMake formatting | gersemi | `--check .` |

If this job fails, run **Format: C++** and **Format: CMake** locally, then push again.

### Proto schema

Runs two `buf` checks against the `proto/` directory. Runs in the CI container;
`buf` is pre-installed.

| Step | Command | What it catches |
|---|---|---|
| Lint | `buf lint` | Style violations - package naming, field conventions, etc. |
| Breaking changes | `buf breaking --against '.git#branch=main'` | Backward-incompatible schema changes (removed fields, renamed messages, etc.) |

### Docs coverage

Validates that all public C++ symbols are documented and that the Sphinx site
builds without warnings. Runs in the CI container; doxygen and all Sphinx packages
are pre-installed. All downstream jobs gate on `format`, `docs`, and `proto` via `needs:`.

| Step | Command |
|---|---|
| Run Doxygen | `doxygen docs/Doxyfile` - emits XML to `docs/_build/doxygen/xml/` and HTML to `docs/_build/doxygen/html/` |
| Build Sphinx site | `sphinx-build -W -b html docs docs/_build/html` |

`-W` promotes any Sphinx warning to an error. The Doxyfile sets
`WARN_AS_ERROR = FAIL_ON_WARNINGS`, so an undocumented public symbol also fails the job.

See [documentation.md](documentation.md) for what must be documented and how to
write Doxygen comments.

### Build and lint (debug)

Full debug build plus clang-tidy static analysis. Uses `.github/actions/setup-builder` with
`conan-profile: conan/profiles/x86_64/debug`. clang-tidy-18 is pre-installed in the CI container.

After setup:

| Step | Command |
|---|---|
| Configure | `cmake --preset debug` |
| Build | `cmake --build --preset debug` |
| Test | `ctest --preset debug` |
| Lint | `run-clang-tidy-18 -p build/Debug` scoped to `libs/`, `proto/`, `tests/` |

### Build and test (release)

Identical setup to the debug job but uses `conan/profiles/x86_64/release` and the `release` CMake
preset. Validates that release-mode optimisations and `NDEBUG` do not expose latent undefined
behaviour that Debug masks.

### Sanitize (ASan + UBSan)

Runs after `build` (`needs: [build]`). Builds and tests with
`-fsanitize=address,undefined -fno-sanitize-recover=all`. Catches heap/stack overflows,
use-after-free, and undefined behaviour that clang-tidy cannot see statically. Restores the
debug Conan cache saved by `build` - no cold dependency rebuild.

### Thread Sanitizer

Runs after `build` (`needs: [build]`). Builds and tests with `-fsanitize=thread`. Finds data
races in concurrent code. Incompatible with ASan - runs as a separate job. Restores the debug
Conan cache saved by `build`.

### Coverage

Runs after `build` (`needs: [build]`). Restores the debug Conan cache saved by `build`.
Builds and tests with `-fprofile-instr-generate -fcoverage-mapping`. After the test run:

1. Merges raw profile data with `llvm-profdata-18`
2. Generates a line-coverage report with `llvm-cov-18` and appends it to the GitHub Step Summary
3. Fails the job if total line coverage falls below **80%**

Raise the threshold in the **Enforce minimum coverage** step of the `coverage` job as the test
suite grows.

### Build and test (ARM64)

Runs on a native `ubuntu-22.04-arm` runner inside the arm64 variant of the CI container image
(L4T base). Uses `conan/profiles/arm64/debug` and `cmake --preset debug`. Native execution means
tests actually run on ARM64 hardware. Conan packages and ccache are cached separately under keys
prefixed `conan-arm64-` to keep them isolated from the x86_64 caches.

### Benchmark build

Runs after `build` (`needs: [build]`). Uses `conan/profiles/x86_64/release` and the `bench`
CMake preset (`ORION_BENCHMARKS=ON`, Release build type). Compiles `orion_transport_benchmarks` but does
not execute it — benchmark results on shared CI runners are meaningless due to virtualisation
and thermal throttling. The job exists to catch compilation errors early.

To run benchmarks, use the **Benchmark: Transport** VS Code task or:

```bash
cmake --preset bench && cmake --build --preset bench
./build/Bench/benchmarks/orion_transport_benchmarks
```

See [building-and-testing.md](building-and-testing.md) for payload sizes and the p99 < 1 ms
latency target (Jetson Orin Nano).

### Fuzz (smoke test)

Runs after `build` (`needs: [build]`). Restores the debug Conan cache saved by `build`.
Builds the fuzz preset (`ORION_FUZZING=ON`, ASan enabled), then runs each fuzz target for
10 000 iterations. Catches immediate crashes and memory errors.

| Target | Library under test | `max_len` |
|---|---|---|
| `fuzz_topic` | `orion_transport` (topic string parsing) | 4 096 |
| `fuzz_envelope` | `orion_proto` (protobuf envelope deserialisation) | 65 536 |

To run a longer campaign locally:

```bash
cmake --preset fuzz && cmake --build --preset fuzz
./build/Fuzz/tests/fuzz/fuzz_topic   -max_len=4096  -timeout=60 corpus/
./build/Fuzz/tests/fuzz/fuzz_envelope -max_len=65536 -timeout=60 corpus/
```

## Release workflow

`.github/workflows/changelog.yml` triggers when a `v*` tag is pushed. It:

1. Uses `orhun/git-cliff-action` with `cliff.toml` to generate the changelog for the current tag
2. Creates a GitHub Release with the generated body

```{mermaid}
flowchart LR
    classDef trigger fill:#e8f5e9,stroke:#2e7d32,stroke-width:2px,color:#000
    classDef release fill:#f3e5f5,stroke:#4a148c,stroke-width:2px,color:#000

    T3["Tag Pushed<br/>(v*.*.*)"]:::trigger --> R1("Generate Changelog<br/>(git-cliff)"):::release
    R1 --> R2("Publish GitHub Release"):::release
```

See [versioning.md](versioning.md) for how to create a release tag.

## Node.js runtime

All jobs set `FORCE_JAVASCRIPT_ACTIONS_TO_NODE24: true` at the workflow level. This opts into
Node.js 24 ahead of the mandatory transition (June 2026). Remove this variable and pin actions to
their Node.js 24 releases once they ship those versions.

## Dependency caching

Conan packages are baked into the CI container image — there is no per-run Conan cache.
Only ccache (for project source compilation) uses GitHub Actions cache:

| Cache | Path | Key |
|---|---|---|
| ccache objects | `~/.cache/ccache` | `ccache-conan-<os>-<commit SHA>` |

The ccache always restores from the most recent prior entry and saves a new entry per commit,
so only changed translation units recompile. The save runs as the final step of each job
(after the build) so compiled objects are captured even if tests or lint fail.

When `conanfile.py`, `conan.lock`, or `conan/**` changes, `ci-image.yml` automatically
rebuilds the image with fresh packages baked in. All subsequent CI runs pick up the new
packages with zero per-job overhead.

## What blocks a merge

Every job must pass before a PR can be merged. In particular:

- Formatting error → **Format** fails
- Undocumented public symbol or Sphinx warning → **Docs coverage** fails
- Proto style violation → **Proto schema** (lint) fails
- Backward-incompatible schema change → **Proto schema** (breaking) fails
- Build or test failure → any of the build/test jobs fails
- clang-tidy finding → **Build and lint (debug)** fails
- Sanitizer crash or error → **Sanitize** or **Thread Sanitizer** fails
- Coverage below 80% → **Coverage** fails
- ARM64 build or test failure → **Build and test (ARM64)** fails
- Fuzzer crash → **Fuzz** fails

## Relationship to pre-commit hooks

The CI format checks mirror the pre-commit hooks exactly. Pre-commit catches formatting issues
locally on `git commit`; CI catches anything that slipped through (e.g. `--no-verify`). The same
tools and flags are used in both places.

## Adding a new check

1. Add a step or job to `.github/workflows/ci.yml`
2. If appropriate as a pre-commit hook, add it to `.pre-commit-config.yaml`
3. If it warrants a VS Code task, add it to `.vscode/tasks.json`

Keep format checks in the **Format** job (no build dependency) and binary-dependent checks in
their own jobs or in **Build and lint**.
