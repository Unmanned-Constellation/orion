# CI/CD

Continuous integration runs on GitHub Actions. Three workflows exist:

| Workflow file | Triggers | Purpose |
|---|---|---|
| `.github/workflows/ci.yml` | Push to `main`, all PRs | Build, test, lint, sanitize, coverage, fuzz, docs, proto schema |
| `.github/workflows/changelog.yml` | Push of `v*` tag | Generate CHANGELOG and create GitHub Release |
| `.github/workflows/docs.yml` | Push to `main`, manual dispatch | Build Sphinx/Doxygen site and deploy to GitHub Pages |

## Custom actions

### `.github/actions/setup-builder`

Composite action used by every build job. It abstracts the shared setup steps so
each job only needs to pass a Conan profile and an optional list of extra apt packages.

**Inputs**

| Input | Required | Default | Description |
|---|---|---|---|
| `conan-profile` | yes | - | Path to the Conan host profile (e.g. `conan/profiles/x86_64/debug`) |
| `extra-packages` | no | `""` | Space-separated extra apt packages installed alongside the base toolchain |
| `cache-key-prefix` | no | `conan` | Prefix used for Conan package and ccache cache keys |

**Steps (in order)**

| Step | What it does |
|---|---|
| Install toolchain | Installs clang-18, cmake, ninja, ccache - plus any `extra-packages` - from the LLVM apt repository |
| Configure ccache | Sets `CMAKE_C_COMPILER_LAUNCHER=ccache` / `CMAKE_CXX_COMPILER_LAUNCHER=ccache`; caps cache at 1 GB |
| Cache ccache | `~/.cache/ccache` keyed on `<prefix>-<os>-<sha>`, restores from most recent prior run |
| Cache pip | `~/.cache/pip` keyed on OS - avoids re-downloading the Conan wheel |
| Install Conan | `pip install conan` |
| Cache Conan packages | `~/.conan2/p` keyed on `<prefix>-<os>-<conan.lock hash>` |
| Configure Conan profile | `conan profile detect --force` - picks up clang-18 via `CC`/`CXX` |
| Register local recipes remote | Adds `conan/recipes/` as `orion-local` (priority 0, `local-recipes-index` type) |
| Install dependencies | `conan install --profile=<conan-profile> --lockfile=conan.lock` |

## CI jobs

The fast-check jobs (format, proto, docs) run in parallel with each other. All
build, test, sanitizer, coverage, and fuzz jobs depend on those three via `needs:` and only
start after they all pass. Every job sets `CC=clang-18` and `CXX=clang++-18` so Conan and
CMake use clang rather than the runner's default GCC - required because Conan injects
`-stdlib=libstdc++` and GCC rejects that flag.

### Format

Checks all C++ and CMake files are correctly formatted. No build required - fastest failing job.

| Check | Tool | Command |
|---|---|---|
| C++ formatting | clang-format-18 | `--dry-run --Werror` on `*.cpp`/`*.hpp` in `libs/`, `proto/`, `tests/` |
| CMake formatting | gersemi | `--check .` |

If this job fails, run **Format: C++** and **Format: CMake** locally, then push again.

### Proto schema

Runs two `buf` checks against the `proto/` directory:

| Step | Command | What it catches |
|---|---|---|
| Lint | `buf lint` | Style violations - package naming, field conventions, etc. |
| Breaking changes | `buf breaking --against '.git#branch=main'` | Backward-incompatible schema changes (removed fields, renamed messages, etc.) |

`buf` is downloaded directly from GitHub releases (v1.69.0) - no additional setup required.

### Docs coverage

Validates that all public C++ symbols are documented and that the Sphinx site
builds without warnings. Runs in parallel with **Format** and **Proto schema**
- all downstream jobs gate on these via `needs: [format, docs, proto]`.

| Step | Command |
|---|---|
| Install Doxygen | `apt-get install doxygen` |
| Install Sphinx deps | `pip install -r docs/requirements.txt` |
| Run Doxygen | `doxygen docs/Doxyfile` - emits XML to `docs/_build/doxygen/xml/` |
| Build Sphinx site | `sphinx-build -W -b html docs docs/_build/html` |

`-W` promotes any Sphinx warning to an error. The Doxyfile sets
`WARN_AS_ERROR = YES`, so an undocumented public symbol also fails the job.

See [documentation.md](documentation.md) for what must be documented and how to
write Doxygen comments.

### Build and lint (debug)

Full debug build plus clang-tidy static analysis. Uses `.github/actions/setup-builder` with
`conan-profile: conan/profiles/x86_64/debug` and `extra-packages: clang-tidy-18`.

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

Builds and tests with `-fsanitize=address,undefined -fno-sanitize-recover=all`. Catches
heap/stack overflows, use-after-free, and undefined behaviour that clang-tidy cannot see
statically. Uses the debug Conan install (same packages, separate build directory).

### Thread Sanitizer

Builds and tests with `-fsanitize=thread`. Finds data races in concurrent code. Incompatible
with ASan - runs as a separate job. Uses the debug Conan install.

### Coverage

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

Three layers of caching are active on every job:

| Cache | Path | Key |
|---|---|---|
| Conan packages | `~/.conan2/p` | `conan-<os>-<conan.lock hash>` |
| ccache objects | `~/.cache/ccache` | `ccache-conan-<os>-<commit SHA>` |
| pip wheel | `~/.cache/pip` | `pip-<os>-conan` |

When `conan.lock` changes the Conan cache misses and all packages rebuild. The ccache always
restores from the most recent prior entry and saves a new entry per commit, so only changed
translation units recompile. The `sanitize`, `tsan`, `coverage`, and `fuzz` jobs share the same
x86_64 Conan cache bucket as `build` since they install identical packages from the same debug
profile.

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
