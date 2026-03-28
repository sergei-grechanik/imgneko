#!/bin/sh

# Exercise nested test-runner invocations so output capture, custom output roots,
# and failure summaries are verified end to end.

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

RUNNER=$IMGNEKO_BUILD_DIR/bin/test-runner
OUTPUT_ROOT=$IMGNEKO_TEST_OUTPUT_DIR/nested
EXEC_OUTPUT_DIR=$OUTPUT_ROOT/runner/output.sh
EXEC_OUTPUT=$EXEC_OUTPUT_DIR/output
RUNNER_C_OUTPUT_DIR=$OUTPUT_ROOT/runner/output.c/emit_output
RUNNER_C_OUTPUT=$RUNNER_C_OUTPUT_DIR/output
UNIT_C_OUTPUT_DIR=$OUTPUT_ROOT/unit/util/string.c/empty_and_reserve
UNIT_C_OUTPUT=$UNIT_C_OUTPUT_DIR/output
SUCCESS_LOG=$(mktemp /tmp/imgneko-runner-success.XXXXXX)
FAILURE_LOG=$(mktemp /tmp/imgneko-runner-failure.XXXXXX)
C_LOG=$(mktemp /tmp/imgneko-runner-c.XXXXXX)

cleanup() {
    rm -f "$SUCCESS_LOG" "$FAILURE_LOG" "$C_LOG"
}

trap cleanup EXIT

assert_file_exists "$RUNNER"

rm -rf "$OUTPUT_ROOT"
"$RUNNER" --output-dir "$OUTPUT_ROOT" --filter runner/output.sh >"$SUCCESS_LOG" 2>&1 ||
    fail "nested success run failed"

assert_file_exists "$EXEC_OUTPUT"
assert_file_contains "$EXEC_OUTPUT" "output script stdout marker"
assert_file_contains "$EXEC_OUTPUT" "output script stderr marker"
assert_file_contains "$SUCCESS_LOG" "discovered: 1"
assert_file_contains "$SUCCESS_LOG" "passed: 1"
assert_file_contains "$SUCCESS_LOG" "Result: SUCCESS"

# C tests should get their own nested output files too, including tests under
# testing/tests/runner/.
"$RUNNER" --output-dir "$OUTPUT_ROOT" --filter runner/output.c/emit_output >"$C_LOG" 2>&1 ||
    fail "nested runner C test run failed"

assert_file_exists "$RUNNER_C_OUTPUT"
assert_file_contains "$RUNNER_C_OUTPUT" "output C test stdout marker"
assert_file_contains "$RUNNER_C_OUTPUT" "output C test stderr marker"

"$RUNNER" --output-dir "$OUTPUT_ROOT" --filter unit/util/string.c/empty_and_reserve >"$C_LOG" 2>&1 ||
    fail "nested C subtest run failed"

assert_file_exists "$UNIT_C_OUTPUT"

# A failing test should report the captured output path plus only the last
# twenty output lines.
set +e
IMGNEKO_TEST_SHOULD_FAIL=1 "$RUNNER" --output-dir "$OUTPUT_ROOT" --filter runner/output.sh >"$FAILURE_LOG" 2>&1
status=$?
set -e

[ "$status" -ne 0 ] || fail "nested failure run unexpectedly succeeded"
assert_file_contains "$FAILURE_LOG" "===== LAST 20 LINES OF TEST OUTPUT $EXEC_OUTPUT {{{ ====="
assert_file_contains "$FAILURE_LOG" "===== }}} END TEST OUTPUT ====="
assert_file_contains "$FAILURE_LOG" "failure line 6"
assert_file_contains "$FAILURE_LOG" "failure line 25"
assert_file_not_contains "$FAILURE_LOG" "failure line 5"
assert_file_contains "$FAILURE_LOG" "failed tests:"
assert_file_contains "$FAILURE_LOG" "  runner/output.sh"
assert_file_contains "$FAILURE_LOG" "Summary:"
assert_file_contains "$FAILURE_LOG" "discovered: 1"
assert_file_contains "$FAILURE_LOG" "failed: 1"
assert_file_contains "$FAILURE_LOG" "Result: FAILURE"
