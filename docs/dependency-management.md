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
`conan/recipes/`:

**`zenoh-c`** (`conan/recipes/zenoh-c/conanfile.py`)
Downloads a pre-built shared library from the zenoh-c GitHub Releases page for the target
architecture. The recipe maps Conan's `armv8` arch identifier to the `aarch64-unknown-linux-gnu`
release asset name.

**`zenoh-cpp`** (`conan/recipes/zenoh-cpp/conanfile.py`)
Header-only; downloads the source tarball from GitHub. Declares a dependency on `zenoh-c`.

Both recipes are exported into the local Conan cache by `initialize_conan.sh` before the install
step runs.

## `conanfile.py`

```python
self.requires("protobuf/5.29.3")
self.requires("zenoh-c/1.9.0")
self.requires("zenoh-cpp/1.9.0")
self.requires("abseil/20240722.0", force=True)
```

The `force=True` on abseil pins the version to avoid a runtime symbol conflict with the abseil
version bundled in the Triton runtime inside the DeepStream L4T image. See
[ADR-0003](adr/0003-multi-platform-build-and-deploy.md).

## The lockfile

`conan.lock` is version-controlled and pins the exact content hash of every resolved package.
`initialize_conan.sh` passes `--lockfile=conan.lock` to `conan install` when the file exists,
ensuring reproducible installs across machines.

**Regenerate the lockfile whenever you change `conanfile.py`.**

## Adding a dependency

1. Add `self.requires("lib/version")` to `conanfile.py`
2. Run the **Conan: Create Lockfile** task — this regenerates `conan.lock` using the release
   profile for the current architecture
3. Run the **Conan: Install** task — installs all dependencies and reconfigures both CMake presets
4. Commit `conanfile.py` and `conan.lock` together

## Lockfile recovery

If `conan.lock` is stale or causes a conflict during install, the recovery sequence is:

```bash
rm conan.lock
```

Then run steps 2–4 above. The lockfile must be regenerated before `conan install` will succeed —
running install first against a missing or inconsistent lockfile will fail.

## `initialize_conan.sh` walkthrough

`scripts/environment/initialize_conan.sh` is the single entry point for environment setup:

1. Detects architecture via `uname -m` → sets `ARCH` to `x86_64` or `arm64`
2. Runs `conan profile detect --force` to (re)generate the default Conan profile for the host
3. Exports both custom recipes: `conan export conan/recipes/zenoh-c` and `zenoh-cpp`
4. Wipes `build/` entirely to ensure a clean slate
5. Installs the **Release** profile: `conan install . --output-folder=build/Release --profile=conan/profiles/${ARCH}/release`
6. Installs the **Debug** profile: `conan install . --output-folder=build/Debug --profile=conan/profiles/${ARCH}/debug`
7. Configures both CMake presets: `cmake --preset release` and `cmake --preset debug`

Steps 5 and 6 pass `--lockfile=conan.lock` when the lockfile exists.

## Offline install

The **Conan: Install (Offline)** task passes `--no-remote` to `initialize_conan.sh`, which
forwards it to both `conan install` calls. Use this when all required packages are already in the
local cache (`~/.cache/orion-deps`) and no network access is available.
