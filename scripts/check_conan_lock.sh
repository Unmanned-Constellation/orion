#!/usr/bin/env bash
# Fails if conanfile.py dependency lines changed but conan.lock was not re-staged.

set -euo pipefail

if ! git diff --cached --name-only | grep -q "^conanfile\.py$"; then
    exit 0
fi

dep_diff=$(git diff --cached conanfile.py \
    | grep -E '^[+-].*self\.(requires|test_requires)\(' \
    | grep -v '^---\|^+++' \
    || true)

if [ -z "$dep_diff" ]; then
    exit 0
fi

if git diff --cached --name-only | grep -q "^conan\.lock$"; then
    exit 0
fi

echo "conanfile.py: dependency change detected but conan.lock is not staged."
echo ""
echo "Changed lines:"
echo "$dep_diff" | sed 's/^/  /'
echo ""
echo "Regenerate and stage the lockfile:"
echo "  conan lock create . --profile=conan/profiles/x86_64/debug"
echo "  git add conan.lock"
exit 1
