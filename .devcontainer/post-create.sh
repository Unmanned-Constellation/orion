#!/usr/bin/env bash
set -euo pipefail

pre-commit install
conan profile detect --force
conan export conan/recipes/zenoh-c
conan export conan/recipes/zenoh-cpp

# Wipe any stale Conan-generated cmake files before reinstalling.
# build/ lives on the host bind mount and can carry over cmake data files
# from previous container instances that pointed to a different CONAN_HOME.
rm -rf build/

conan install . --output-folder=build --build=missing -s build_type=Debug
conan install . --output-folder=build --build=missing -s build_type=Release
/usr/bin/cmake --preset conan-debug --no-warn-unused-cli





