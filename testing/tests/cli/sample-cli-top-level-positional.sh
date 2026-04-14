#!/usr/bin/env run-and-check
# RUN: sh %s

# Exercise the validation that rejects command-based parsers with top-level
# positional arguments.

set -eu

fail() {
    echo "$1" >&2
    exit 1
}

SAMPLE_CLI=$IMGNEKO_BUILD_DIR/bin/sample-cli-top-level-positional

[ -x "$SAMPLE_CLI" ] || fail "missing sample-cli-top-level-positional binary: $SAMPLE_CLI"

echo '== help =='
set +e
env COLUMNS=80 "$SAMPLE_CLI" --help 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK:      {{^}}== help =={{$}}
# CHECK-NEXT: {{^}}error: program parsers with commands do not support top-level positional arguments{{$}}
# CHECK-NEXT: {{^}}status=1{{$}}

echo '== parse =='
set +e
"$SAMPLE_CLI" prefix 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK-NEXT: {{^}}== parse =={{$}}
# CHECK-NEXT: {{^}}error: program parsers with commands do not support top-level positional arguments{{$}}
# CHECK-NEXT: {{^}}status=1{{$}}
