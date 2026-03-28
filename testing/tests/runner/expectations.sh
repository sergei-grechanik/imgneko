#!/bin/sh

# Verify test markers in list output, skip/xfail accounting, and unexpected
# success reporting via nested test-runner invocations.

set -eu

fail() {
    printf '%s\n' "$1" >&2
    exit 1
}

assert_file_exists() {
    path=$1

    [ -f "$path" ] || fail "expected file to exist: $path"
}

assert_file_contains() {
    path=$1
    needle=$2

    if ! grep -F -- "$needle" "$path" >/dev/null 2>&1; then
        printf '%s\n' "Expected file to contain: $needle" >&2
        printf '%s\n' "Actual file: $path" >&2
        sed -n '1,200p' "$path" >&2
        exit 1
    fi
}

assert_file_not_contains() {
    path=$1
    needle=$2

    if grep -F -- "$needle" "$path" >/dev/null 2>&1; then
        printf '%s\n' "Expected file not to contain: $needle" >&2
        printf '%s\n' "Actual file: $path" >&2
        sed -n '1,200p' "$path" >&2
        exit 1
    fi
}

assert_path_absent() {
    path=$1

    [ ! -e "$path" ] || fail "expected path to be absent: $path"
}

RUNNER=$IMGNEKO_BUILD_DIR/bin/test-runner
OUTPUT_ROOT=$IMGNEKO_TEST_OUTPUT_DIR/markers
LIST_LOG=$(mktemp /tmp/imgneko-runner-list.XXXXXX)
SUMMARY_LOG=$(mktemp /tmp/imgneko-runner-summary.XXXXXX)
XPASS_LOG=$(mktemp /tmp/imgneko-runner-xpass.XXXXXX)
FILTER=runner/markers.c\|runner/xfail.sh\|runner/disabled.sh

cleanup() {
    rm -f "$LIST_LOG" "$SUMMARY_LOG" "$XPASS_LOG"
}

trap cleanup EXIT

assert_file_exists "$RUNNER"

"$RUNNER" --list --filter "$FILTER" >"$LIST_LOG" 2>&1 ||
    fail "nested list run failed"

assert_file_contains "$LIST_LOG" "runner/markers.c/marked_disabled DISABLED"
assert_file_contains "$LIST_LOG" "runner/markers.c/marked_xfail XFAIL"
assert_file_contains "$LIST_LOG" "runner/xfail.sh XFAIL"
assert_file_contains "$LIST_LOG" "runner/disabled.sh DISABLED"

rm -rf "$OUTPUT_ROOT"
"$RUNNER" --output-dir "$OUTPUT_ROOT" --filter "$FILTER" >"$SUMMARY_LOG" 2>&1 ||
    fail "nested marker run failed"

assert_file_contains "$SUMMARY_LOG" "XFAIL: runner/markers.c/marked_xfail"
assert_file_contains "$SUMMARY_LOG" "XFAIL: runner/xfail.sh"
assert_file_contains "$SUMMARY_LOG" "DISABLED: runner/markers.c/marked_disabled"
assert_file_contains "$SUMMARY_LOG" "DISABLED: runner/disabled.sh"
assert_file_contains "$SUMMARY_LOG" "discovered: 4"
assert_file_contains "$SUMMARY_LOG" "xfailed: 2"
assert_file_contains "$SUMMARY_LOG" "disabled: 2"
assert_path_absent "$OUTPUT_ROOT/runner/markers.c/marked_disabled"
assert_path_absent "$OUTPUT_ROOT/runner/disabled.sh"

set +e
IMGNEKO_TEST_FORCE_SUCCESS=1 "$RUNNER" --output-dir "$OUTPUT_ROOT" \
    --filter "runner/markers.c/marked_xfail|runner/xfail.sh" \
    >"$XPASS_LOG" 2>&1
status=$?
set -e

[ "$status" -ne 0 ] || fail "unexpected-success run unexpectedly passed"

assert_file_contains "$XPASS_LOG" "XPASS: runner/markers.c/marked_xfail"
assert_file_contains "$XPASS_LOG" "XPASS: runner/xfail.sh"
assert_file_contains "$XPASS_LOG" "unexpectedly succeeded tests:"
assert_file_contains "$XPASS_LOG" "  runner/markers.c/marked_xfail"
assert_file_contains "$XPASS_LOG" "  runner/xfail.sh"
assert_file_contains "$XPASS_LOG" "discovered: 2"
assert_file_contains "$XPASS_LOG" "unexpectedly succeeded: 2"
assert_file_not_contains "$XPASS_LOG" "failed:"
