#!/usr/bin/env bash
set -euo pipefail

# In an isolated DevContainer, it is safe to trust all directories.
# This prevents "unsafe repository" errors regardless of the clone directory name.
git config --global --add safe.directory '*'

echo "==> Installing pre-commit hooks..."
pre-commit install

echo "==> Initializing Conan environment..."
bash scripts/environment/initialize_conan.sh

echo "==> Dev environment successfully created!"