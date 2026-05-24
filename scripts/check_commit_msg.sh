#!/usr/bin/env bash
# Enforces commit message rules from commitlint.config.mjs:
#   - header max 72 characters
#   - type must be one of the allowed values

set -euo pipefail

msg_file="$1"
header=$(head -1 "$msg_file")

# Strip comments (lines starting with #)
header=$(echo "$header" | sed 's/^#.*//')

if [ -z "$header" ]; then
    exit 0
fi

fail=0

if [ ${#header} -gt 72 ]; then
    echo "commit-msg: header too long (${#header} chars, max 72)"
    echo "  $header"
    fail=1
fi

allowed_types="feat|fix|chore|docs|refactor|test|ci|perf|style|build|revert"
if ! echo "$header" | grep -qE "^($allowed_types)(\(.+\))?!?: .+"; then
    echo "commit-msg: header must match '<type>(<scope>): <desc>'"
    echo "  allowed types: ${allowed_types//|/, }"
    echo "  $header"
    fail=1
fi

exit $fail
