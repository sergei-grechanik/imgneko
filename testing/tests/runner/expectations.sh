#!/bin/sh
# SPDX-License-Identifier: MIT-0

# Verify test markers in list output, skip/xfail accounting, and unexpected
# success reporting via nested test-runner invocations.

set -eu

fail() {
    printf '%s\n' "$1" >&2
    exit 1
}

if [ -z "${IMGNEKO_TEST_OUTPUT_DIR:-}" ] ||
   [ ! -d "$IMGNEKO_TEST_OUTPUT_DIR" ]; then
    fail "IMGNEKO_TEST_OUTPUT_DIR is not set to an existing directory"
fi

wait_for_process_gone() {
    pid=$1
    i=0

    while [ "$i" -lt 200 ]; do
        if ! kill -0 "$pid" >/dev/null 2>&1; then
            return 0
        fi
        sleep 0.05
        i=$((i + 1))
    done

    return 1
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
SUMMARY_OUTPUT_ROOT=$IMGNEKO_TEST_OUTPUT_DIR/markers-summary
XPASS_OUTPUT_ROOT=$IMGNEKO_TEST_OUTPUT_DIR/markers-xpass
FLIP_OUTPUT_ROOT=$IMGNEKO_TEST_OUTPUT_DIR/markers-flip
TIMEOUT_OUTPUT_ROOT=$IMGNEKO_TEST_OUTPUT_DIR/markers-timeout
TIMEOUT_CLOSED_FDS_OUTPUT_ROOT=$IMGNEKO_TEST_OUTPUT_DIR/markers-timeout-closed-fds
TIMEOUT_DETACHED_OUTPUT_ROOT=$IMGNEKO_TEST_OUTPUT_DIR/markers-timeout-detached
TIMEOUT_DISABLED_OUTPUT_ROOT=$IMGNEKO_TEST_OUTPUT_DIR/markers-timeout-disabled
DISABLED_ONLY_OUTPUT_ROOT=$IMGNEKO_TEST_OUTPUT_DIR/markers-disabled-only
LIST_LOG=$(mktemp /tmp/imgneko-runner-list.XXXXXX)
SUMMARY_LOG=$(mktemp /tmp/imgneko-runner-summary.XXXXXX)
XPASS_LOG=$(mktemp /tmp/imgneko-runner-xpass.XXXXXX)
FLIP_LOG=$(mktemp /tmp/imgneko-runner-flip.XXXXXX)
TIMEOUT_LOG=$(mktemp /tmp/imgneko-runner-timeout.XXXXXX)
TIMEOUT_CLOSED_FDS_LOG=$(mktemp /tmp/imgneko-runner-timeout-closed-fds.XXXXXX)
TIMEOUT_DETACHED_OUTPUT_LOG=$(mktemp /tmp/imgneko-runner-timeout-detached-output.XXXXXX)
TIMEOUT_DISABLED_LOG=$(mktemp /tmp/imgneko-runner-timeout-disabled.XXXXXX)
DISABLED_ONLY_LOG=$(mktemp /tmp/imgneko-runner-disabled-only.XXXXXX)
TIMEOUT_CLOSED_FDS_OUTPUT=$TIMEOUT_CLOSED_FDS_OUTPUT_ROOT/runner/timeout-closed-fds.sh/output
TIMEOUT_DETACHED_OUTPUT=$TIMEOUT_DETACHED_OUTPUT_ROOT/runner/timeout-detached-output.sh/output
FILTER=runner/markers.c\|runner/xfail.sh\|runner/disabled.sh

cleanup() {
    rm -f "$LIST_LOG" "$SUMMARY_LOG" "$XPASS_LOG" "$FLIP_LOG" \
        "$TIMEOUT_LOG" "$TIMEOUT_CLOSED_FDS_LOG" \
        "$TIMEOUT_DETACHED_OUTPUT_LOG" "$TIMEOUT_DISABLED_LOG" \
        "$DISABLED_ONLY_LOG"
}

trap cleanup EXIT

assert_file_exists "$RUNNER"

"$RUNNER" --jobs=1 --list "$FILTER" >"$LIST_LOG" 2>&1 ||
    fail "nested list run failed"

assert_file_contains "$LIST_LOG" "runner/markers.c/marked_disabled DISABLED"
assert_file_contains "$LIST_LOG" "runner/markers.c/marked_xfail XFAIL"
assert_file_contains "$LIST_LOG" "runner/xfail.sh XFAIL"
assert_file_contains "$LIST_LOG" "runner/disabled.sh DISABLED"

"$RUNNER" --jobs=1 --output-dir "$SUMMARY_OUTPUT_ROOT" "$FILTER" >"$SUMMARY_LOG" 2>&1 ||
    fail "nested marker run failed"

assert_file_contains "$SUMMARY_LOG" "XFAIL: runner/markers.c/marked_xfail"
assert_file_contains "$SUMMARY_LOG" "XFAIL: runner/xfail.sh"
assert_file_contains "$SUMMARY_LOG" "DISABLED: runner/markers.c/marked_disabled"
assert_file_contains "$SUMMARY_LOG" "DISABLED: runner/disabled.sh"
assert_file_contains "$SUMMARY_LOG" "discovered: 4"
assert_file_contains "$SUMMARY_LOG" "xfailed: 2"
assert_file_contains "$SUMMARY_LOG" "disabled: 2"
assert_file_contains "$SUMMARY_LOG" "Time:"
assert_file_contains "$SUMMARY_LOG" "Result: SUCCESS"
assert_path_absent "$SUMMARY_OUTPUT_ROOT/runner/markers.c/marked_disabled"
assert_path_absent "$SUMMARY_OUTPUT_ROOT/runner/disabled.sh"

# Disabled tests never enter running_tests, so a disabled-only run must still
# exit promptly instead of blocking in the event-loop wait path.
"$RUNNER" --jobs=1 --output-dir "$DISABLED_ONLY_OUTPUT_ROOT" \
    "runner/markers.c/marked_disabled|runner/disabled.sh" \
    >"$DISABLED_ONLY_LOG" 2>&1 &
disabled_only_pid=$!

wait_for_process_gone "$disabled_only_pid" || {
    kill "$disabled_only_pid" >/dev/null 2>&1 || true
    wait "$disabled_only_pid" || true
    fail "disabled-only run hung"
}

wait "$disabled_only_pid" ||
    fail "disabled-only run unexpectedly failed"

assert_file_contains "$DISABLED_ONLY_LOG" "DISABLED: runner/markers.c/marked_disabled"
assert_file_contains "$DISABLED_ONLY_LOG" "DISABLED: runner/disabled.sh"
assert_file_contains "$DISABLED_ONLY_LOG" "Summary:"
assert_file_contains "$DISABLED_ONLY_LOG" "discovered: 2"
assert_file_contains "$DISABLED_ONLY_LOG" "disabled: 2"
assert_file_contains "$DISABLED_ONLY_LOG" "Result: SUCCESS"

set +e
IMGNEKO_TEST_FORCE_SUCCESS=1 "$RUNNER" --jobs=1 --output-dir "$XPASS_OUTPUT_ROOT" \
    "runner/markers.c/marked_xfail|runner/xfail.sh" \
    >"$XPASS_LOG" 2>&1
status=$?
set -e

[ "$status" -ne 0 ] || fail "unexpected-success run unexpectedly passed"

assert_file_contains "$XPASS_LOG" "XPASS: runner/markers.c/marked_xfail"
assert_file_contains "$XPASS_LOG" "XPASS: runner/xfail.sh"
assert_file_contains "$XPASS_LOG" "xpassed tests:"
assert_file_contains "$XPASS_LOG" "  runner/markers.c/marked_xfail"
assert_file_contains "$XPASS_LOG" "  runner/xfail.sh"
assert_file_contains "$XPASS_LOG" "Summary:"
assert_file_contains "$XPASS_LOG" "discovered: 2"
assert_file_contains "$XPASS_LOG" "xpassed: 2"
assert_file_contains "$XPASS_LOG" "Time:"
assert_file_contains "$XPASS_LOG" "Result: FAILURE"
assert_file_not_contains "$XPASS_LOG" "failed:"

set +e
"$RUNNER" --jobs=1 --output-dir "$FLIP_OUTPUT_ROOT" --debug-flip-exit-probability=1 \
    runner/output.sh >"$FLIP_LOG" 2>&1
status=$?
set -e

[ "$status" -ne 0 ] || fail "flipped-exit run unexpectedly passed"

assert_file_contains "$FLIP_LOG" "DEBUG: flipped exit code for runner/output.sh (0 -> 1)"
assert_file_contains "$FLIP_LOG" "FAIL: runner/output.sh"
assert_file_contains "$FLIP_LOG" "===== LAST 20 LINES OF TEST OUTPUT $FLIP_OUTPUT_ROOT/runner/output.sh/output {{{ ====="
assert_file_contains "$FLIP_LOG" "===== }}} END TEST OUTPUT ====="
assert_file_contains "$FLIP_LOG" "failed tests:"
assert_file_contains "$FLIP_LOG" "  runner/output.sh"
assert_file_contains "$FLIP_LOG" "Summary:"
assert_file_contains "$FLIP_LOG" "discovered: 1"
assert_file_contains "$FLIP_LOG" "failed: 1"
assert_file_contains "$FLIP_LOG" "Time:"
assert_file_contains "$FLIP_LOG" "Result: FAILURE"

set +e
"$RUNNER" --jobs=1 --output-dir "$TIMEOUT_OUTPUT_ROOT" --timeout 1 \
    runner/timeout.sh >"$TIMEOUT_LOG" 2>&1
status=$?
set -e

[ "$status" -ne 0 ] || fail "timeout run unexpectedly passed"

assert_file_contains "$TIMEOUT_LOG" "TIMEOUT: runner/timeout.sh"
assert_file_contains "$TIMEOUT_LOG" "timed out tests:"
assert_file_contains "$TIMEOUT_LOG" "  runner/timeout.sh"
assert_file_contains "$TIMEOUT_LOG" "Summary:"
assert_file_contains "$TIMEOUT_LOG" "discovered: 1"
assert_file_contains "$TIMEOUT_LOG" "timeout: 1"
assert_file_contains "$TIMEOUT_LOG" "Time:"
assert_file_contains "$TIMEOUT_LOG" "Result: FAILURE"

set +e
IMGNEKO_TEST_TIMEOUT_CLOSED_FDS_SLEEP_SECONDS=30 \
    "$RUNNER" --jobs=1 --output-dir "$TIMEOUT_CLOSED_FDS_OUTPUT_ROOT" --timeout 2 \
    runner/timeout-closed-fds.sh >"$TIMEOUT_CLOSED_FDS_LOG" 2>&1
status=$?
set -e

[ "$status" -ne 0 ] || fail "closed-fds timeout run unexpectedly passed"

assert_file_contains "$TIMEOUT_CLOSED_FDS_LOG" "TIMEOUT: runner/timeout-closed-fds.sh"
assert_file_contains "$TIMEOUT_CLOSED_FDS_LOG" "timed out tests:"
assert_file_contains "$TIMEOUT_CLOSED_FDS_LOG" "  runner/timeout-closed-fds.sh"
assert_file_contains "$TIMEOUT_CLOSED_FDS_LOG" "Summary:"
assert_file_contains "$TIMEOUT_CLOSED_FDS_LOG" "discovered: 1"
assert_file_contains "$TIMEOUT_CLOSED_FDS_LOG" "timeout: 1"
assert_file_contains "$TIMEOUT_CLOSED_FDS_LOG" "Time:"
assert_file_contains "$TIMEOUT_CLOSED_FDS_LOG" "Result: FAILURE"
assert_file_exists "$TIMEOUT_CLOSED_FDS_OUTPUT"
[ ! -s "$TIMEOUT_CLOSED_FDS_OUTPUT" ] ||
    fail "expected closed-fds timeout output to be empty"

# Keep the detached child alive well past the allowed elapsed time, leaving room
# for coarse date +%s timing and macOS ASan scheduling.
detached_start=$(date +%s)
set +e
IMGNEKO_TEST_TIMEOUT_DETACHED_OUTPUT_SLEEP_SECONDS=10 \
    "$RUNNER" --jobs=1 --output-dir "$TIMEOUT_DETACHED_OUTPUT_ROOT" --timeout 1 \
    runner/timeout-detached-output.sh \
    >"$TIMEOUT_DETACHED_OUTPUT_LOG" 2>&1
status=$?
set -e
detached_elapsed=$(( $(date +%s) - detached_start ))

[ "$status" -ne 0 ] || fail "detached-output timeout run unexpectedly passed"
[ "$detached_elapsed" -lt 6 ] ||
    fail "detached-output timeout run took too long: ${detached_elapsed}s"

assert_file_contains "$TIMEOUT_DETACHED_OUTPUT_LOG" \
    "TIMEOUT: runner/timeout-detached-output.sh"
assert_file_contains "$TIMEOUT_DETACHED_OUTPUT_LOG" "timed out tests:"
assert_file_contains "$TIMEOUT_DETACHED_OUTPUT_LOG" \
    "  runner/timeout-detached-output.sh"
assert_file_contains "$TIMEOUT_DETACHED_OUTPUT_LOG" "Summary:"
assert_file_contains "$TIMEOUT_DETACHED_OUTPUT_LOG" "discovered: 1"
assert_file_contains "$TIMEOUT_DETACHED_OUTPUT_LOG" "timeout: 1"
assert_file_contains "$TIMEOUT_DETACHED_OUTPUT_LOG" "Result: FAILURE"
assert_file_exists "$TIMEOUT_DETACHED_OUTPUT"
assert_file_contains "$TIMEOUT_DETACHED_OUTPUT" \
    "timeout detached output script started"

"$RUNNER" --jobs=1 --output-dir "$TIMEOUT_DISABLED_OUTPUT_ROOT" --timeout 0 \
    runner/timeout.sh >"$TIMEOUT_DISABLED_LOG" 2>&1 ||
    fail "timeout-disabled run unexpectedly failed"

assert_file_contains "$TIMEOUT_DISABLED_LOG" "PASS: runner/timeout.sh"
assert_file_not_contains "$TIMEOUT_DISABLED_LOG" "TIMEOUT: runner/timeout.sh"
assert_file_contains "$TIMEOUT_DISABLED_LOG" "Summary:"
assert_file_contains "$TIMEOUT_DISABLED_LOG" "discovered: 1"
assert_file_contains "$TIMEOUT_DISABLED_LOG" "passed: 1"
assert_file_contains "$TIMEOUT_DISABLED_LOG" "Time:"
assert_file_contains "$TIMEOUT_DISABLED_LOG" "Result: SUCCESS"
