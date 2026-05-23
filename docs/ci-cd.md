# CI/CD

Continuous integration runs on GitHub Actions. Two workflows exist:

| Workflow file | Triggers | Purpose |
|---|---|---|
| `.github/workflows/ci.yml` | Push to `main`, all PRs | Build, test, lint, sanitize, coverage, fuzz |
| `.github/workflows/changelog.yml` | Push of `v*` tag | Generate CHANGELOG and create GitHub Release |

## CI jobs

All jobs run in parallel. Every job sets `CC=clang-18` and `CXX=clang++-18` so Conan and CMake
use clang rather than the runner's default GCC — required because Conan injects `-stdlib=libstdc++`
and GCC rejects that flag.

### Format

Checks all C++ and CMake files are correctly formatted. No build required — fastest failing job.

| Check | Tool | Command |
|---|---|---|
| C++ formatting | clang-format-18 | `--dry-run --Werror` on `*.cpp`/`*.hpp` in `libs/`, `proto/`, `tests/` |
| CMake formatting | gersemi | `--check .` |

If this job fails, run **Format: C++** and **Format: CMake** locally, then push again.

### Commit messages

Runs `wagoid/commitlint-github-action` against every commit in the push or PR using the rules in
`commitlint.config.mjs`. Enforces [Conventional Commits](https://www.conventionalcommits.org):
`feat:`, `fix:`, `chore:`, `docs:`, `refactor:`, `test:`, `ci:`, `perf:`, `build:`, `revert:`.

Header max length: 72 characters.

### Build and lint (debug)

Full debug build plus clang-tidy static analysis.

| Step | What happens |
|---|---|
| Install toolchain | clang-18, clang-tidy-18, cmake, ninja via LLVM apt |
| Install Conan | `pip install conan` |
| Restore cache | `~/.conan2/p` keyed on `conan.lock` hash |
| Configure Conan profile | `conan profile detect --force` — detects clang-18 via `CC`/`CXX` |
| Export recipes | Exports custom `zenoh-c` and `zenoh-cpp` recipes |
| Install dependencies | `conan install --profile=x86_64/debug --lockfile=conan.lock` |
| Configure | `cmake --preset debug` |
| Build | `cmake --build --preset debug` |
| Test | `ctest --preset debug` |
| Lint | `run-clang-tidy-18` scoped to `libs/`, `proto/`, `tests/` |

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
with ASan — runs as a separate job. Uses the debug Conan install.

### Coverage

Builds and tests with `-fprofile-instr-generate -fcoverage-mapping`. After the test run:

1. Merges raw profile data with `llvm-profdata-18`
2. Generates a line-coverage report with `llvm-cov-18` and appends it to the GitHub Step Summary
3. Fails the job if total line coverage falls below **60%**

Raise the threshold in the **Enforce minimum coverage** step of the `coverage` job as the test
suite grows.

### Cross-compile (ARM64)

Cross-compiles for `aarch64-linux-gnu` using clang-18 and the sysroot from `gcc-aarch64-linux-gnu`.
Uses a two-profile Conan install (`--profile:build=x86_64/debug --profile:host=arm64/debug`) with
`--output-folder=build/CrossArm64`. No `ctest` step — the output binaries cannot run on the x86
runner.

ARM64 packages are cached separately under the key `conan-arm64-<os>-<lockfile-hash>`.

### Fuzz (smoke test)

Builds the fuzz preset (`ORION_FUZZING=ON`, ASan enabled), then runs each fuzz target for 10 000
iterations. Catches immediate crashes and memory errors in the fuzz targets. To run a longer
campaign locally:

```bash
cmake --preset fuzz && cmake --build --preset fuzz
./build/Fuzz/tests/fuzz/fuzz_topic -max_len=4096 -timeout=60 corpus/
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

The Conan package binaries (`~/.conan2/p`) are cached in Actions using `conan.lock` as the key.
When the lockfile changes, the cache misses and all packages reinstall from scratch.

The `sanitize`, `tsan`, `coverage`, and `fuzz` jobs share the same x86_64 cache bucket as the
`build` job since they install identical packages from the same debug profile.

## What blocks a merge

Every job must pass before a PR can be merged. In particular:

- Formatting error → **Format** fails
- Non-conventional commit message → **Commit messages** fails
- Build or test failure → any of the build/test jobs fails
- clang-tidy finding → **Build and lint (debug)** fails
- Sanitizer crash or error → **Sanitize** or **Thread Sanitizer** fails
- Coverage below 60% → **Coverage** fails
- Cross-compile error → **Cross-compile (ARM64)** fails
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
