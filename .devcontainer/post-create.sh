#!/usr/bin/env bash
set -euo pipefail

pip install -r docs/requirements.txt --quiet
pre-commit install
bash scripts/environment/initialize_conan.sh
