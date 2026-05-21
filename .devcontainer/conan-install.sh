#!/usr/bin/env bash
set -euo pipefail

rm -rf build/
conan export conan/recipes/zenoh-c
conan export conan/recipes/zenoh-cpp
conan install . --output-folder=build --build=missing -s build_type=Release
conan install . --output-folder=build --build=missing -s build_type=Debug
