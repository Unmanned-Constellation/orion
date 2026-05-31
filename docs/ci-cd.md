# CI/CD

Continuous integration runs on GitHub Actions. Three workflows exist:

| Workflow file | Triggers | Purpose |
|---|---|---|
| `.github/workflows/ci.yml` | Push to `main`, all PRs | Build, test, lint, sanitize, coverage, fuzz, docs, proto schema |
| `.github/workflows/changelog.yml` | Push of `v*` tag | Generate CHANGELOG and create GitHub Release |
| `.github/workflows/docs.yml` | Push to `main`, manual dispatch | Build Sphinx/Doxygen site and deploy to GitHub Pages |

## CI container image

All x86_64 jobs run inside a pre-built container image published to GitHub Container
Registry (GHCR):

```
ghcr.io/unmanned-constellation/orion/ci:latest
```

The image is built from `.devcontainer/Dockerfile` (amd64 target only - the arm64
target uses an NVIDIA L4T base that is not suitable for GitHub runners). It contains
the full toolchain: clang-18, cmake, ninja, ccache, conan, clang-tidy, clang-format,
libclang-rt, llvm, doxygen, sphinx, gersemi, and buf.

The image is rebuilt by `.github/workflows/ci-image.yml` whenever `.devcontainer/Dockerfile`
or `ci-image.yml` changes. Docker layer caching (`type=gha`) keeps rebuilds fast when only
lower layers change.

**Push policy:** on `pull_request`, the workflow builds the image to validate the Dockerfile
but does not push - it only pushes to `:latest` on merge to `main`. This prevents an
unreviewed Dockerfile change from overwriting the image used by concurrent CI runs.

## Custom actions

### `.github/actions/setup-builder`

Composite action used by every build job. It configures ccache and Conan; for the
native ARM64 runner (which has no container) it also installs the toolchain via apt.

**Inputs**

| Input | Required | Default | Description |
|---|---|---|---|
| `conan-profile` | yes | - | Path to the Conan host profile (e.g. `conan/profiles/x86_64/debug`) |
| `extra-packages` | no | `""` | Extra apt packages (only used when `install-toolchain` is true) |
| `cache-key-prefix` | no | `conan` | Prefix used for Conan package and ccache cache keys |
| `install-toolchain` | no | `"false"` | Set to `"true"` for non-containerized runners (ARM64 native runner) |

**Steps (in order)**

| Step | Condition | What it does |
|---|---|---|
| Install toolchain | `install-toolchain == true` | Installs clang-18, cmake, ninja, ccache from the LLVM apt repository |
| Install Conan | `install-toolchain == true` | `pip install conan` |
| Cache pip | `install-toolchain == true` | `~/.cache/pip` keyed on OS |
| Configure ccache | always | Sets `CMAKE_C_COMPILER_LAUNCHER=ccache`, `cache_dir=$HOME/.cache/ccache`, `base_dir=$GITHUB_WORKSPACE`, caps at 1 GB |
| Restore ccache | always | `~/.cache/ccache` keyed on `<prefix>-<os>-<sha>`, restores from most recent prior run |
| Restore Conan packages | always | `~/.conan2/p` keyed on `<prefix>-<os>-<conan.lock hash>-<conan-profile>` |
| Save Conan packages | always | Saves immediately after `conan install` — packages are fully populated at this point regardless of whether downstream build/test steps fail |
| Configure Conan profile | always | `conan profile detect --force` - picks up clang-18 via `CC`/`CXX` |
| Register local recipes remote | always | Adds `conan/` as `orion-local` (priority 0, `local-recipes-index` type); root must be `conan/`, not `conan/recipes/` |
| Install dependencies | always | `conan install --profile=<conan-profile> --lockfile=conan.lock` |

## CI jobs

The job dependency graph is:

```
format ─┐
docs   ─┼─► build ─────────► sanitize
proto  ─┘        └──────────► tsan
                 └──────────► coverage
                 └──────────► fuzz
         ├──────► build-release
         └──────► build-arm64
```

`format`, `docs`, and `proto` run in parallel. `build` and `build-release` start once all
three pass. `sanitize`, `tsan`, `coverage`, and `fuzz` all `needs: [build]` - they start
after the debug build completes and restore its warm Conan cache, avoiding a cold dependency
rebuild on every run.

Every build job sets `CC=clang-18` and `CXX=clang++-18` so Conan and CMake use clang rather
than the runner's default GCC - required because Conan injects `-stdlib=libstdc++` and GCC
rejects that flag.

All x86_64 jobs (including format, proto, and docs) run inside the CI container image.
Only `build-arm64` runs on a bare `ubuntu-22.04-arm` runner with `install-toolchain: true`.

### Format

Checks all C++ and CMake files are correctly formatted. No build required - fastest failing job.
Runs in the CI container; clang-format-18 and gersemi are pre-installed.

| Check | Tool | Command |
|---|---|---|
| C++ formatting | clang-format-18 | `--dry-run --Werror` on `*.cpp`/`*.hpp` in `libs/`, `proto/`, `tests/` |
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
| Run Doxygen | `doxygen docs/Doxyfile` - emits XML to `docs/_build/doxygen/xml/` |
| Build Sphinx site | `sphinx-build -W -b html docs docs/_build/html` |

`-W` promotes any Sphinx warning to an error. The Doxyfile sets
`WARN_AS_ERROR = YES`, so an undocumented public symbol also fails the job.

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
3. Fails the job if total line coverage falls below **60%**

Raise the threshold in the **Enforce minimum coverage** step of the `coverage` job as the test
suite grows.

### Build and test (ARM64)

Runs on a native `ubuntu-22.04-arm` runner. Uses `conan/profiles/arm64/debug` (single-profile
install) and `cmake --preset debug`. Includes a full `ctest` step - native execution means tests
actually run on ARM64. ARM64 Conan packages and ccache are cached separately under keys prefixed
`conan-arm64-` and `ccache-conan-arm64-`.

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

See [versioning.md](versioning.md) for how to create a release tag.

## Node.js runtime

All jobs set `FORCE_JAVASCRIPT_ACTIONS_TO_NODE24: true` at the workflow level. This opts into
Node.js 24 ahead of the mandatory transition (June 2026). Remove this variable and pin actions to
their Node.js 24 releases once they ship those versions.

## Dependency caching

Two layers of caching are active on build jobs:

| Cache | Path | Key |
|---|---|---|
| Conan packages | `~/.conan2/p` | `conan-<os>-<conan.lock hash>-<conan-profile>` |
| ccache objects | `~/.cache/ccache` | `ccache-conan-<os>-<commit SHA>` |

The pip cache is only active on `build-arm64` (the only job that runs `pip install conan`).
The CI container image has Conan pre-installed, so pip is not invoked on x86_64 jobs.

When `conan.lock` changes the Conan cache misses and all packages rebuild from source. The
ccache always restores from the most recent prior entry and saves a new entry per commit, so
only changed translation units recompile. `sanitize`, `tsan`, `coverage`, and `fuzz` run after
`build` completes and restore its Conan cache - they never perform a cold dependency rebuild.
The Conan cache is saved inside `setup-builder` immediately after `conan install`, so packages
are always persisted regardless of whether the subsequent build or test steps fail. The ccache
save runs as the final step of each job (after the build), so compiled objects are captured even
if tests or lint fail — `actions/cache/save` always runs unless the job is cancelled.

The ccache `cache_dir` is explicitly set to `~/.cache/ccache` in `setup-builder` to override
the `CCACHE_DIR=/ccache` environment variable baked into the container image (which is a volume
mount path in the devcontainer, not available in CI).

## What blocks a merge

Every job must pass before a PR can be merged. In particular:

- Formatting error → **Format** fails
- Undocumented public symbol or Sphinx warning → **Docs coverage** fails
- Proto style violation → **Proto schema** (lint) fails
- Backward-incompatible schema change → **Proto schema** (breaking) fails
- Build or test failure → any of the build/test jobs fails
- clang-tidy finding → **Build and lint (debug)** fails
- Sanitizer crash or error → **Sanitize** or **Thread Sanitizer** fails
- Coverage below 60% → **Coverage** fails
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
