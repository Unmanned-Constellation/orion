# Dependency Management

Dependencies are managed with Conan 2. The lockfile is version-controlled so every developer and
CI run resolves identical package versions.

## Profile structure

Four profiles live in `conan/profiles/`, one per architecture/build-type combination:

```
conan/profiles/
  x86_64/
    debug
    release
  arm64/
    debug
    release
```

All four profiles use Clang 18, C++20, and `libstdc++11`. The only differences are `arch`
(`x86_64` vs `armv8`) and `build_type` (`Debug` vs `Release`). `initialize_conan.sh` selects the
correct arch directory automatically based on `uname -m`.

## Custom recipes

Two dependencies are not available in ConanCenter and are maintained as local recipes under
`conan/recipes/`, structured as a Conan [Local Recipes Index](https://docs.conan.io/2/devops/devops_local_recipes_index.html):

```
conan/recipes/
  zenoh-c/
    config.yml        # maps version "1.9.0" to the all/ folder
    all/
      conanfile.py
  zenoh-cpp/
    config.yml
    all/
      conanfile.py
```

**`zenoh-c`** (`conan/recipes/zenoh-c/all/conanfile.py`)
Downloads a pre-built shared library from the zenoh-c GitHub Releases page for the target
architecture. The recipe maps Conan's `armv8` arch identifier to the `aarch64-unknown-linux-gnu`
release asset name.

**`zenoh-cpp`** (`conan/recipes/zenoh-cpp/all/conanfile.py`)
Header-only; downloads the source tarball from GitHub. Declares a dependency on `zenoh-c`.

`initialize_conan.sh` registers `conan/` as a `local-recipes-index` remote (`orion-local`)
at priority 0 so Conan reads recipes directly from the filesystem. No `conan export` step is
needed - this avoids the timestamp churn that `conan export` causes in the lockfile.

## `conanfile.py`

```python
def requirements(self):
    self.requires("protobuf/5.29.3")
    self.requires("zenoh-c/1.9.0")
    self.requires("zenoh-cpp/1.9.0")
    self.requires("abseil/20240722.0", force=True)
    self.requires("backward-cpp/1.6", options={"stack_details": "dw"})

def build_requirements(self):
    self.tool_requires("protobuf/5.29.3")
    self.test_requires("gtest/1.17.0")
```

`backward-cpp` uses the `dw` backend for full DWARF symbolization — function
names, file names, line numbers, and inlined frames. This requires `libdw-dev`
to be installed in the devcontainer (via `apt`) and on the Jetson deployment
sysroot. The `elfutils` and `xz_utils` transitive dependencies are resolved from
ConanCenter. The `orion-local` remote must be registered with the correct path
(`/workspaces/orion/conan`) for ConanCenter fallthrough to work — `initialize_conan.sh`
handles this automatically.

`tool_requires("protobuf/5.29.3")` causes Conan's `VirtualBuildEnv` generator to add protobuf's
`bin/` directory to `PATH` via `conanbuild.sh`. CMake uses this during the build via the
`protobuf::protoc` import target - `protoc` never needs to be on the system PATH. The compiler
version is guaranteed to match the runtime library through the shared lockfile entry.

## buf

`buf` is installed directly in the Docker image (`docker/Dockerfile`) rather than managed by
Conan. It is a standalone schema toolchain with no dependency on the C++ build graph:

| Use | Command |
|---|---|
| Lint proto style | `buf lint` |
| Format `.proto` files | `buf format -w` |
| Check for breaking schema changes | `buf breaking --against '.git#branch=main'` |

Configuration lives in `buf.yaml` at the repo root. The proto module root is `proto/`, and
packages follow the versioned convention `orion.v1` (files under `proto/orion/v1/`).

`buf` is also used by the `bufbuild.vscode-buf` VS Code extension for in-editor proto linting and
go-to-definition. Because it is in the Docker image it is always on `PATH` without any env
sourcing or symlinking.

The `force=True` on abseil pins the version to avoid a runtime symbol conflict with the abseil
version bundled in the Triton runtime inside the DeepStream L4T image. See
[ADR-0003](adr/0003-multi-platform-build-and-deploy.md).

## The lockfile

`conan.lock` is version-controlled and pins the exact content hash of every resolved package.
`initialize_conan.sh` passes `--lockfile=conan.lock` to `conan install` when the file exists,
ensuring reproducible installs across machines.

**Regenerate the lockfile whenever you change `conanfile.py`.**

When `conan.lock` already exists the **Conan: Create Lockfile** task passes it as
`--lockfile=conan.lock` (input) in addition to `--lockfile-out=conan.lock` (output). Conan
reuses existing lockfile entries whose RREV still resolves correctly rather than re-querying
the remote, which keeps the timestamps for the local zenoh-c and zenoh-cpp recipes stable
between regenerations. Only entries that actually changed (new dependency, version bump) will
differ in the resulting file.

## Adding a dependency

1. Add `self.requires("lib/version")` to `conanfile.py`
2. Run the **Conan: Create Lockfile** task - this regenerates `conan.lock` using the release
   profile for the current architecture
3. Run the **Conan: Install** task - installs all dependencies and reconfigures both CMake presets
4. Commit `conanfile.py` and `conan.lock` together

## Lockfile recovery

If `conan.lock` is stale or causes a conflict during install, the recovery sequence is:

```bash
rm conan.lock
```

Then run steps 2–4 above. The lockfile must be regenerated before `conan install` will succeed -
running install first against a missing or inconsistent lockfile will fail.

## `initialize_conan.sh` walkthrough

`scripts/environment/initialize_conan.sh` is the single entry point for environment setup:

1. Detects architecture via `uname -m` → sets `ARCH` to `x86_64` or `arm64`
2. Runs `conan profile detect --force` to (re)generate the default Conan profile for the host
3. Registers `conan/` as a `local-recipes-index` remote named `orion-local` at priority 0
   (checked before ConanCenter). The remote root must be `conan/`, not `conan/recipes/` - the
   `local-recipes-index` type expects the parent directory that contains `recipes/`.
4. Wipes `build/` entirely to ensure a clean slate - pass `--no-clean` to skip this
5. Installs the **Release** profile: `conan install . --profile=conan/profiles/${ARCH}/release`
6. Installs the **Debug** profile: `conan install . --profile=conan/profiles/${ARCH}/debug`
7. Configures both CMake presets: `cmake --preset release` and `cmake --preset debug`

Steps 5 and 6 pass `--lockfile=conan.lock` when the lockfile exists.

Pass `--no-clean` (e.g. via the **Conan: Sync Dependencies** VS Code task) to skip step 4
when you only need to pull in a dependency change without discarding the Ninja build cache.

## Offline install

The **Conan: Install (Offline)** task passes `--no-remote` to `initialize_conan.sh`, which
forwards it to both `conan install` calls. Use this when all required packages are already in the
local cache (`~/.cache/orion-deps`) and no network access is available.
