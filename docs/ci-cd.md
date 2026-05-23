# CI/CD

Continuous integration runs on GitHub Actions. The workflow is defined in
`.github/workflows/ci.yml` and triggers on every push to `main` and every pull request.

## Jobs

Two jobs run in parallel:

### Format

Checks that all C++ and CMake files are correctly formatted. No build or Conan install required —
this job is fast and fails early.

| Check | Tool | Command |
|---|---|---|
| C++ formatting | clang-format-18 | `--dry-run --Werror` on all `*.cpp` and `*.hpp` in `libs/` and `proto/` |
| CMake formatting | gersemi | `--check .` on all `CMakeLists.txt` and `*.cmake` files |

Neither check auto-fixes. If this job fails, run the **Format: C++** and **Format: CMake** VS Code
tasks locally, then push again.

### Build and lint

Installs the full toolchain and dependencies, builds the debug preset, then runs static analysis.

The job sets `CC=clang-18` and `CXX=clang++-18` at the job level so every step — including
`conan profile detect` and package builds from source — uses clang rather than the runner's default
`/usr/bin/c++` (GCC). GCC does not accept the `-stdlib=libstdc++` flag that Conan's toolchain
injects for `compiler.libcxx=libstdc++11`, so without this the abseil build fails immediately.

| Step | What happens |
|---|---|
| Install toolchain | clang-18, clang-tidy-18, cmake, ninja via LLVM apt |
| Install Conan | `pip install conan` |
| Restore cache | Restores `~/.conan2/p` from cache keyed on `conan.lock` hash |
| Configure Conan profile | `conan profile detect --force` — detects clang-18 via `CC`/`CXX` |
| Export recipes | Exports custom `zenoh-c` and `zenoh-cpp` recipes into the local cache |
| Install dependencies | `conan install` with the `x86_64/debug` profile, `--lockfile=conan.lock`, and `-c tools.system.package_manager:mode=check` |
| Configure | `cmake --preset debug` |
| Build | `cmake --build --preset debug` |
| Lint | `run-clang-tidy-18` scoped to `libs/` and `proto/` |

## Node.js runtime

Both jobs set `FORCE_JAVASCRIPT_ACTIONS_TO_NODE24: true` at the workflow level. This opts into the
Node.js 24 runtime for GitHub Actions ahead of the mandatory transition (forced June 2026, Node.js
20 removal September 2026). Once `actions/checkout` and `actions/cache` ship native Node.js 24
versions, this variable can be removed and the actions pinned to those versions instead.

## Dependency caching

The Conan package binaries (`~/.conan2/p`) are cached in Actions using `conan.lock` as the cache key.
When the lockfile changes (i.e. when `conanfile.py` is updated and the lockfile is regenerated),
the cache misses and all packages are reinstalled from scratch. On a warm cache hit, the install
step is near-instant.

## What blocks a merge

Both jobs must pass before a PR can be merged:

- Any C++ or CMake file that needs reformatting fails **Format**
- A build error fails **Build and lint**
- Any clang-tidy finding fails **Build and lint**

## Relationship to pre-commit hooks

The CI format checks mirror the pre-commit hooks exactly. Pre-commit catches formatting issues
locally on `git commit`; CI catches anything that slipped through (e.g. `--no-verify`). The same
tools and flags are used in both places.

## Adding a new check

To add a new CI check:

1. Add a step to the appropriate job in `.github/workflows/ci.yml`
2. If the check is also appropriate as a pre-commit hook, add it to `.pre-commit-config.yaml`
3. If it warrants a VS Code task, add it to `.vscode/tasks.json`

Keep format checks in the **Format** job (no build dependency) and anything that requires a built
binary in the **Build and lint** job.
