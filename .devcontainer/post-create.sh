#!/usr/bin/env bash
set -euo pipefail

pre-commit install
bash scripts/environment/initialize_conan.sh
