#!/bin/sh
# SPDX-License-Identifier: MIT-0

# Keep review placeholders out of checked-in code. The only allowed occurrence
# is the instruction documenting this policy.

set -eu

ROOT_DIR=$(CDPATH= cd "$(dirname "$0")/../.." && pwd)
EXPECTED_PATH=AGENTS.md
MARKER=$(printf '%s:' 'REVIEW')

cd "$ROOT_DIR"

matches=$(git grep -n -o -F "$MARKER" -- . || true)
paths=$(git grep -l -F "$MARKER" -- . || true)
count=$(printf '%s\n' "$matches" | sed '/^$/d' | wc -l | tr -d ' ')

if [ "$count" = 1 ] && [ "$paths" = "$EXPECTED_PATH" ]; then
    exit 0
fi

printf '%s\n' "Unexpected review placeholder locations:" >&2
if [ -n "$matches" ]; then
    printf '%s\n' "$matches" >&2
else
    printf '%s\n' "(none)" >&2
fi
printf '%s\n' "Expected exactly one occurrence in $EXPECTED_PATH." >&2
exit 1
