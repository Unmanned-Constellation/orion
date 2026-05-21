#!/usr/bin/env bash
set -euo pipefail

case "$(uname -m)" in
    x86_64)           ARCH=x86_64 ;;
    aarch64 | arm64)  ARCH=arm64  ;;
    *) echo "Unsupported architecture: $(uname -m)" >&2; exit 1 ;;
esac

conan profile detect --force

LOCKFILE_ARG=""
if [ -f conan.lock ]; then
    LOCKFILE_ARG="--lockfile=conan.lock"
fi

conan export conan/recipes/zenoh-c
conan export conan/recipes/zenoh-cpp

rm -rf build/

conan install . --output-folder=build/Release --build=missing \
    --profile=conan/profiles/${ARCH}/release ${LOCKFILE_ARG} "$@"

conan install . --output-folder=build/Debug --build=missing \
    --profile=conan/profiles/${ARCH}/debug ${LOCKFILE_ARG} "$@"

/usr/bin/cmake --preset release --no-warn-unused-cli
/usr/bin/cmake --preset debug --no-warn-unused-cli
