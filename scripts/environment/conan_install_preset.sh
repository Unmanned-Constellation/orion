#!/usr/bin/env bash
# Run `conan install` for a single CMake preset, using the matching Conan profile.
# Idempotent: if packages are already cached, Conan just regenerates the CMake files.
set -euo pipefail

PRESET="${1:?Usage: conan_install_preset.sh <preset>}"

case "$(uname -m)" in
    x86_64)          ARCH=x86_64 ;;
    aarch64 | arm64) ARCH=arm64  ;;
    *) echo "Unsupported architecture: $(uname -m)" >&2; exit 1 ;;
esac

case "${PRESET}" in
    debug | sanitize | tsan | coverage | fuzz) PROFILE="${ARCH}/debug"   ;;
    release | cross-arm64)                     PROFILE="${ARCH}/release" ;;
    *) echo "Unknown preset: ${PRESET}" >&2; exit 1 ;;
esac

LOCKFILE_ARG=""
if [ -f conan.lock ]; then
    LOCKFILE_ARG="--lockfile=conan.lock"
fi

conan install . --build=missing \
    --profile="conan/profiles/${PROFILE}" ${LOCKFILE_ARG}
