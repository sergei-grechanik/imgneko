#!/bin/sh
# SPDX-License-Identifier: MIT-0

# Exercise nested test-runner invocations so output capture, custom output roots,
# and failure summaries are verified end to end.

set -eu

fail() {
    printf '%s\n' "$1" >&2
    exit 1
}

if [ -z "${IMGNEKO_TEST_OUTPUT_DIR:-}" ] ||
   [ ! -d "$IMGNEKO_TEST_OUTPUT_DIR" ]; then
    fail "IMGNEKO_TEST_OUTPUT_DIR is not set to an existing directory"
fi

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

# Poll instead of sleeping for live-output assertions. macOS ASan runs can start
# the nested child slowly enough that a fixed sleep checks the log too early.
wait_for_file_contains() {
    path=$1
    needle=$2
    i=0

    while [ "$i" -lt 200 ]; do
        if [ -f "$path" ] && grep -F -- "$needle" "$path" >/dev/null 2>&1; then
            return 0
        fi
        sleep 0.05
        i=$((i + 1))
    done

    printf '%s\n' "Timed out waiting for file to contain: $needle" >&2
    printf '%s\n' "Actual file: $path" >&2
    sed -n '1,200p' "$path" >&2
    exit 1
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
SUCCESS_OUTPUT_ROOT=$IMGNEKO_TEST_OUTPUT_DIR/nested-success
PASSTHROUGH_OUTPUT_ROOT=$IMGNEKO_TEST_OUTPUT_DIR/nested-passthrough
RUNNER_C_OUTPUT_ROOT=$IMGNEKO_TEST_OUTPUT_DIR/nested-runner-c
UNIT_C_OUTPUT_ROOT=$IMGNEKO_TEST_OUTPUT_DIR/nested-unit-c
FAILURE_OUTPUT_ROOT=$IMGNEKO_TEST_OUTPUT_DIR/nested-failure
PASSTHROUGH_FAILURE_OUTPUT_ROOT=$IMGNEKO_TEST_OUTPUT_DIR/nested-passthrough-failure
EXEC_OUTPUT_DIR=$SUCCESS_OUTPUT_ROOT/runner/output.sh
EXEC_OUTPUT=$EXEC_OUTPUT_DIR/output
PASSTHROUGH_EXEC_OUTPUT_DIR=$PASSTHROUGH_OUTPUT_ROOT/runner/output.sh
PASSTHROUGH_EXEC_OUTPUT=$PASSTHROUGH_EXEC_OUTPUT_DIR/output
RUNNER_C_OUTPUT_DIR=$RUNNER_C_OUTPUT_ROOT/runner/output.c/emit_output
RUNNER_C_OUTPUT=$RUNNER_C_OUTPUT_DIR/output
UNIT_C_OUTPUT_DIR=$UNIT_C_OUTPUT_ROOT/unit/util/string.c/empty_and_reserve
UNIT_C_OUTPUT=$UNIT_C_OUTPUT_DIR/output
FAILURE_EXEC_OUTPUT=$FAILURE_OUTPUT_ROOT/runner/output.sh/output
PASSTHROUGH_FAILURE_EXEC_OUTPUT=$PASSTHROUGH_FAILURE_OUTPUT_ROOT/runner/output.sh/output
SUCCESS_LOG=$(mktemp /tmp/imgneko-runner-success.XXXXXX)
FAILURE_LOG=$(mktemp /tmp/imgneko-runner-failure.XXXXXX)
C_LOG=$(mktemp /tmp/imgneko-runner-c.XXXXXX)
PASSTHROUGH_LOG=$(mktemp /tmp/imgneko-runner-passthrough.XXXXXX)
PASSTHROUGH_FAILURE_LOG=$(mktemp /tmp/imgneko-runner-passthrough-failure.XXXXXX)

cleanup() {
    rm -f "$SUCCESS_LOG" "$FAILURE_LOG" "$C_LOG" "$PASSTHROUGH_LOG" \
        "$PASSTHROUGH_FAILURE_LOG"
}

trap cleanup EXIT

assert_file_exists "$RUNNER"

"$RUNNER" --jobs=1 --output-dir "$SUCCESS_OUTPUT_ROOT" --filter runner/output.sh >"$SUCCESS_LOG" 2>&1 ||
    fail "nested success run failed"

assert_file_exists "$EXEC_OUTPUT"
assert_file_contains "$EXEC_OUTPUT" "output script stdout marker"
assert_file_contains "$EXEC_OUTPUT" "output script stderr marker"
assert_file_contains "$SUCCESS_LOG" "discovered: 1"
assert_file_contains "$SUCCESS_LOG" "passed: 1"
assert_file_contains "$SUCCESS_LOG" "Time:"
assert_file_contains "$SUCCESS_LOG" "Result: SUCCESS"

# Output passthrough should surface child output before the nested run exits,
# while still writing the combined output file.
IMGNEKO_TEST_PASSTHROUGH_DELAY=1 "$RUNNER" --jobs=1 --output-dir "$PASSTHROUGH_OUTPUT_ROOT" \
    -p --filter runner/output.sh \
    >"$PASSTHROUGH_LOG" 2>&1 &
passthrough_pid=$!
wait_for_file_contains "$PASSTHROUGH_LOG" "output script stdout marker"
kill -0 "$passthrough_pid" >/dev/null 2>&1 ||
    fail "passthrough run exited before live-output check"
wait "$passthrough_pid" || fail "nested passthrough run failed"

assert_file_exists "$PASSTHROUGH_EXEC_OUTPUT"
assert_file_contains "$PASSTHROUGH_EXEC_OUTPUT" "output script stdout marker"
assert_file_contains "$PASSTHROUGH_EXEC_OUTPUT" "output script stderr marker"
assert_file_contains "$PASSTHROUGH_LOG" "output script stderr marker"
assert_file_contains "$PASSTHROUGH_LOG" "PASS: runner/output.sh"
assert_file_contains "$PASSTHROUGH_LOG" "Result: SUCCESS"

# C tests should get their own nested output files too, including tests under
# testing/tests/runner/.
"$RUNNER" --jobs=1 --output-dir "$RUNNER_C_OUTPUT_ROOT" --filter runner/output.c/emit_output >"$C_LOG" 2>&1 ||
    fail "nested runner C test run failed"

assert_file_exists "$RUNNER_C_OUTPUT"
assert_file_contains "$RUNNER_C_OUTPUT" "output C test stdout marker"
assert_file_contains "$RUNNER_C_OUTPUT" "output C test stderr marker"

"$RUNNER" --jobs=1 --output-dir "$UNIT_C_OUTPUT_ROOT" --filter unit/util/string.c/empty_and_reserve >"$C_LOG" 2>&1 ||
    fail "nested C subtest run failed"

assert_file_exists "$UNIT_C_OUTPUT"

# A failing test should report the captured output path plus only the last
# twenty output lines.
set +e
IMGNEKO_TEST_SHOULD_FAIL=1 "$RUNNER" --jobs=1 --output-dir "$FAILURE_OUTPUT_ROOT" --filter runner/output.sh >"$FAILURE_LOG" 2>&1
status=$?
set -e

[ "$status" -ne 0 ] || fail "nested failure run unexpectedly succeeded"
assert_file_contains "$FAILURE_LOG" "===== LAST 20 LINES OF TEST OUTPUT $FAILURE_EXEC_OUTPUT {{{ ====="
assert_file_contains "$FAILURE_LOG" "===== }}} END TEST OUTPUT ====="
assert_file_contains "$FAILURE_LOG" "failure line 6"
assert_file_contains "$FAILURE_LOG" "failure line 25"
assert_file_not_contains "$FAILURE_LOG" "failure line 5"
assert_file_contains "$FAILURE_LOG" "failed tests:"
assert_file_contains "$FAILURE_LOG" "  runner/output.sh"
assert_file_contains "$FAILURE_LOG" "Summary:"
assert_file_contains "$FAILURE_LOG" "discovered: 1"
assert_file_contains "$FAILURE_LOG" "failed: 1"
assert_file_contains "$FAILURE_LOG" "Time:"
assert_file_contains "$FAILURE_LOG" "Result: FAILURE"

# In passthrough mode, the live test output is already visible so the runner
# should not print a duplicate tail after failure.
set +e
IMGNEKO_TEST_SHOULD_FAIL=1 "$RUNNER" --jobs=1 --output-dir "$PASSTHROUGH_FAILURE_OUTPUT_ROOT" \
    -p --filter runner/output.sh \
    >"$PASSTHROUGH_FAILURE_LOG" 2>&1
status=$?
set -e

[ "$status" -ne 0 ] || fail "nested passthrough failure run unexpectedly succeeded"
assert_file_exists "$PASSTHROUGH_FAILURE_EXEC_OUTPUT"
assert_file_contains "$PASSTHROUGH_FAILURE_EXEC_OUTPUT" "failure line 25"
assert_file_contains "$PASSTHROUGH_FAILURE_LOG" "failure line 1"
assert_file_contains "$PASSTHROUGH_FAILURE_LOG" "failure line 25"
assert_file_contains "$PASSTHROUGH_FAILURE_LOG" "FAIL: runner/output.sh"
assert_file_contains "$PASSTHROUGH_FAILURE_LOG" "failed tests:"
assert_file_contains "$PASSTHROUGH_FAILURE_LOG" "Result: FAILURE"
assert_file_not_contains "$PASSTHROUGH_FAILURE_LOG" "===== LAST"
