#!/usr/bin/env bash
# Run all CI checks in parallel. Shows a live spinner per check while running,
# then replaces each with ✓ or ✗. Failure logs are replayed at the end.
set -euo pipefail
export LC_ALL=C.UTF-8
export LANG=C.UTF-8

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

N=${#CHECKS[@]}
SPINNER='▁▂▃▄▅▆▇█▇▆▅▄▃▂▁'

# Initialize status files
for i in "${!CMDS[@]}"; do
    echo "running" > "$TMPDIR_CI/status_$i"
done

# Print initial placeholder lines so we have rows to overwrite
for check in "${CHECKS[@]}"; do
    printf '  %s\n' "$check"
done

# Launch all checks
pids=()
logs=()
for i in "${!CMDS[@]}"; do
    log="$TMPDIR_CI/check_$i.log"
    logs+=("$log")
    (bash -c "${CMDS[$i]}" >"$log" 2>&1 && printf 'pass' > "$TMPDIR_CI/status_$i" \
        || printf 'fail' > "$TMPDIR_CI/status_$i") &
    pids+=($!)
done

# Live display loop — redraws all N lines in place until every check finishes
spin_idx=0
while true; do
    printf '\033[%dA' "$N"   # move cursor up to the first check line
    all_done=true
    for i in "${!CHECKS[@]}"; do
        status=$(< "$TMPDIR_CI/status_$i")
        case "$status" in
            running)
                all_done=false
                char="${SPINNER:$(( spin_idx % ${#SPINNER} )):1}"
                printf '\033[33m%s\033[0m %s\033[K\n' "$char" "${CHECKS[$i]}"
                ;;
            pass)
                printf '\033[32m✓\033[0m %s\033[K\n' "${CHECKS[$i]}"
                ;;
            *)  # fail
                printf '\033[31m✗\033[0m %s\033[K\n' "${CHECKS[$i]}"
                ;;
        esac
    done
    $all_done && break
    spin_idx=$(( spin_idx + 1 ))
    sleep 0.1
done

for pid in "${pids[@]}"; do
    wait "$pid" || true
done

# Collect and replay failure logs
failed=()
for i in "${!CHECKS[@]}"; do
    [ "$(< "$TMPDIR_CI/status_$i")" = "fail" ] && failed+=("$i")
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
