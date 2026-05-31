#!/usr/bin/env bash
# Run all CI checks in parallel. Each check logs to a temp file; output is
# replayed in order after all jobs finish so the terminal stays readable.
set -euo pipefail

CHECKS=(
    "Format C++"
    "Format CMake"
    "Proto lint"
    "Proto breaking"
    "Tests"
    "Lint C++"
    "Docs"
)

CMDS=(
    "find libs proto tests -type f \\( -name '*.cpp' -o -name '*.hpp' \\) | xargs -r clang-format-18 --dry-run --Werror"
    "gersemi --check ."
    "buf lint"
    "if git ls-remote --exit-code origin main 2>/dev/null && git ls-tree -r origin/main --name-only | grep -q '\\.proto\$'; then buf breaking --against '.git#branch=origin/main'; else echo 'No proto files on origin/main — skipping.'; fi"
    "/usr/bin/cmake --build --preset debug && /usr/bin/ctest --preset debug --output-on-failure"
    "/usr/bin/cmake --build --preset debug --target tidy"
    "rm -rf docs/api && mkdir -p docs/_build/doxygen && doxygen docs/Doxyfile && LC_ALL=C.UTF-8 LANG=C.UTF-8 sphinx-build -W -b html docs docs/_build/html"
)

TMPDIR_CI=$(mktemp -d)
trap 'rm -rf "$TMPDIR_CI"' EXIT

pids=()
logs=()
for i in "${!CMDS[@]}"; do
    log="$TMPDIR_CI/check_$i.log"
    logs+=("$log")
    bash -c "${CMDS[$i]}" >"$log" 2>&1 &
    pids+=($!)
done

failed=()
for i in "${!pids[@]}"; do
    if wait "${pids[$i]}"; then
        printf '\033[32m✓\033[0m %s\n' "${CHECKS[$i]}"
    else
        printf '\033[31m✗\033[0m %s\n' "${CHECKS[$i]}"
        failed+=("$i")
    fi
done

if [ "${#failed[@]}" -gt 0 ]; then
    echo ""
    for i in "${failed[@]}"; do
        printf '\n\033[31m── %s ──\033[0m\n' "${CHECKS[$i]}"
        cat "${logs[$i]}"
    done
    echo ""
    echo "$(( ${#failed[@]} )) check(s) failed."
    exit 1
fi

echo ""
echo "All checks passed."
