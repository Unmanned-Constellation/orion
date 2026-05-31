#!/usr/bin/env bash
set -euo pipefail

case "$(uname -m)" in
    x86_64)           ARCH=x86_64 ;;
    aarch64 | arm64)  ARCH=arm64  ;;
    *) echo "Unsupported architecture: $(uname -m)" >&2; exit 1 ;;
esac

PROFILES_DIR="$(cd "$(dirname "$0")/../../conan/profiles" && pwd)"
cp "${PROFILES_DIR}/${ARCH}/release" "$(conan config home)/profiles/default"

# Register vendored recipes as a local-recipes-index remote so Conan reads them
# directly from the filesystem without a conan export step. Timestamp stability
# in conan.lock is achieved by passing --lockfile as input to conan lock create
# (see the Conan: Create Lockfile VS Code task).
RECIPES_PATH="$(cd "$(dirname "$0")/../../conan" && pwd)"
if ! conan remote list | grep -q "^orion-local:"; then
    conan remote add orion-local "$RECIPES_PATH" -t local-recipes-index --index 0
fi

LOCKFILE_ARG=""
if [ -f conan.lock ]; then
    LOCKFILE_ARG="--lockfile=conan.lock"
fi

# Wipe the build directory for a clean CMake and Conan graph. Pass --no-clean
# to skip this when you only need to sync dependency changes without rebuilding
# from scratch (e.g. updating conanfile.py without wanting a full rebuild).
if [[ "${1:-}" != "--no-clean" ]]; then
    rm -rf build/
else
    shift
fi

conan install . --build=missing \
    --profile=conan/profiles/${ARCH}/release ${LOCKFILE_ARG} "$@"

conan install . --build=missing \
    --profile=conan/profiles/${ARCH}/debug ${LOCKFILE_ARG} "$@"

cmake --preset release --no-warn-unused-cli
cmake --preset debug --no-warn-unused-cli
