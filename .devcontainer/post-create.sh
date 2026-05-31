#!/usr/bin/env bash
set -euo pipefail

git config --global --add safe.directory /workspaces/orion
git config --global --add safe.directory /workspaces/orion/.git
pre-commit install
bash scripts/environment/initialize_conan.sh
