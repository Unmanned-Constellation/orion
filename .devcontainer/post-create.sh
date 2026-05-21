#!/usr/bin/env bash
set -euo pipefail

case "$(uname -m)" in
    x86_64)           ARCH=x86_64 ;;
    aarch64 | arm64)  ARCH=arm64  ;;
    *) echo "Unsupported architecture: $(uname -m)" >&2; exit 1 ;;
esac

pre-commit install
conan export conan/recipes/zenoh-c
conan export conan/recipes/zenoh-cpp

# Wipe stale Conan-generated cmake files before reinstalling.
rm -rf build/

conan install . --output-folder=build/Release --build=missing \
    --profile=conan/profiles/${ARCH}/release

conan install . --output-folder=build/Debug --build=missing \
    --profile=conan/profiles/${ARCH}/debug

/usr/bin/cmake --preset debug --no-warn-unused-cli
