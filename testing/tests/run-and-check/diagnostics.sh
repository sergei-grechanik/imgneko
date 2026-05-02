#!/bin/sh
# SPDX-License-Identifier: MIT-0

# Verify direct run-and-check diagnostics for representative failure modes.

set -eu

fail() {
    printf '%s\n' "$1" >&2
    exit 1
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

RUN_AND_CHECK=run-and-check
TEST_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
NOT_LOG=$(mktemp /tmp/imgneko-run-and-check-not.XXXXXX)
VAR_LOG=$(mktemp /tmp/imgneko-run-and-check-var.XXXXXX)
NOT_VAR_DEF_LOG=$(mktemp /tmp/imgneko-run-and-check-not-var-def.XXXXXX)
NOT_VAR_USE_LOG=$(mktemp /tmp/imgneko-run-and-check-not-var-use.XXXXXX)
NEXT_NO_NEXT_LINE_LOG=$(mktemp /tmp/imgneko-run-and-check-next-no-next-line.XXXXXX)
ANCHOR_LOG=$(mktemp /tmp/imgneko-run-and-check-anchor.XXXXXX)
SAME_LINE_VAR_LOG=$(mktemp /tmp/imgneko-run-and-check-same-line-var.XXXXXX)
SAME_LINE_WITHOUT_PREVIOUS_LOG=$(mktemp /tmp/imgneko-run-and-check-same-line-without-previous.XXXXXX)
OUTPUT_DIR_LOG=$(mktemp /tmp/imgneko-run-and-check-output-dir.XXXXXX)
WHOLE_LINE_NOT_LOG=$(mktemp /tmp/imgneko-run-and-check-whole-line-not.XXXXXX)

cleanup() {
    rm -f "$NOT_LOG" "$VAR_LOG" "$NOT_VAR_DEF_LOG" "$NOT_VAR_USE_LOG" "$NEXT_NO_NEXT_LINE_LOG" "$ANCHOR_LOG" "$SAME_LINE_VAR_LOG" "$OUTPUT_DIR_LOG" "$WHOLE_LINE_NOT_LOG"
    rm -f "$SAME_LINE_WITHOUT_PREVIOUS_LOG"
}

trap cleanup EXIT

set +e
"$RUN_AND_CHECK" "$TEST_DIR/same-line-not-fail.sh" >"$NOT_LOG" 2>&1
status=$?
set -e

[ "$status" -ne 0 ] || fail "same-line CHECK-NOT failure unexpectedly passed"
assert_file_contains "$NOT_LOG" "same-line-not-fail.sh: note: RUN: sh '$TEST_DIR/same-line-not-fail.sh'"
assert_file_contains "$NOT_LOG" "same-line-not-fail.sh: note: stdout file:"
assert_file_contains "$NOT_LOG" "same-line-not-fail.sh: note: stderr file:"
assert_file_contains "$NOT_LOG" "same-line-not-fail.sh: note: RUN exit code: 0"
assert_file_contains "$NOT_LOG" "same-line-not-fail.sh:10: error: CHECK-NOT matched forbidden output"
assert_file_contains "$NOT_LOG" "same-line-not-fail.sh:10: note: pattern: forbidden"
assert_file_contains "$NOT_LOG" "same-line-not-fail.sh:10: note: output line 1: before forbidden after"
assert_file_contains "$NOT_LOG" "same-line-not-fail.sh: note: last 1 lines of stdout"
assert_file_contains "$NOT_LOG" "before forbidden after"

set +e
"$RUN_AND_CHECK" "$TEST_DIR/undefined-variable-fail.sh" >"$VAR_LOG" 2>&1
status=$?
set -e

[ "$status" -ne 0 ] || fail "undefined variable failure unexpectedly passed"
assert_file_contains "$VAR_LOG" "undefined-variable-fail.sh: note: RUN exit code: 0"
assert_file_contains "$VAR_LOG" "undefined-variable-fail.sh:9: error: undefined variable [[missing]] in CHECK"

set +e
"$RUN_AND_CHECK" "$TEST_DIR/check-not-variable-def-fail.sh" >"$NOT_VAR_DEF_LOG" 2>&1
status=$?
set -e

[ "$status" -ne 0 ] || fail "CHECK-NOT variable definition failure unexpectedly passed"
assert_file_contains "$NOT_VAR_DEF_LOG" "check-not-variable-def-fail.sh: note: RUN exit code: 0"
assert_file_contains "$NOT_VAR_DEF_LOG" "check-not-variable-def-fail.sh:10: error: variable definitions are not allowed in CHECK-NOT: [[value]]"

set +e
"$RUN_AND_CHECK" "$TEST_DIR/check-not-variable-use-fail.sh" >"$NOT_VAR_USE_LOG" 2>&1
status=$?
set -e

[ "$status" -ne 0 ] || fail "CHECK-NOT reused-variable failure unexpectedly passed"
assert_file_contains "$NOT_VAR_USE_LOG" "check-not-variable-use-fail.sh: note: RUN exit code: 0"
assert_file_contains "$NOT_VAR_USE_LOG" "check-not-variable-use-fail.sh:13: error: CHECK-NOT matched forbidden output"
assert_file_contains "$NOT_VAR_USE_LOG" "check-not-variable-use-fail.sh:13: note: pattern: id [[number]] middle [[word]]"
assert_file_contains "$NOT_VAR_USE_LOG" "check-not-variable-use-fail.sh:13: note: output line 2: id 123 middle abc"

set +e
"$RUN_AND_CHECK" "$TEST_DIR/check-next-no-next-line-fail.sh" >"$NEXT_NO_NEXT_LINE_LOG" 2>&1
status=$?
set -e

[ "$status" -ne 0 ] || fail "CHECK-NEXT no-next-line failure unexpectedly passed"
assert_file_contains "$NEXT_NO_NEXT_LINE_LOG" "check-next-no-next-line-fail.sh: note: RUN exit code: 0"
assert_file_contains "$NEXT_NO_NEXT_LINE_LOG" "check-next-no-next-line-fail.sh:11: error: CHECK-NEXT did not match"
assert_file_contains "$NEXT_NO_NEXT_LINE_LOG" "check-next-no-next-line-fail.sh:11: note: pattern: beta"
assert_file_contains "$NEXT_NO_NEXT_LINE_LOG" "check-next-no-next-line-fail.sh:11: note: there is no next output line after line 1"

set +e
"$RUN_AND_CHECK" "$TEST_DIR/anchor-both-fail.sh" >"$ANCHOR_LOG" 2>&1
status=$?
set -e

[ "$status" -ne 0 ] || fail "anchored whole-line failure unexpectedly passed"
assert_file_contains "$ANCHOR_LOG" "anchor-both-fail.sh: note: RUN exit code: 0"
assert_file_contains "$ANCHOR_LOG" "anchor-both-fail.sh:9: error: CHECK did not match"
assert_file_contains "$ANCHOR_LOG" "anchor-both-fail.sh:9: note: pattern: {{^value$}}"
assert_file_contains "$ANCHOR_LOG" "anchor-both-fail.sh:9: note: output line 1: prefix value suffix"

set +e
"$RUN_AND_CHECK" "$TEST_DIR/same-line-variable-use-fail.sh" >"$SAME_LINE_VAR_LOG" 2>&1
status=$?
set -e

[ "$status" -ne 0 ] ||
    fail "same-line variable reuse failure unexpectedly passed"
assert_file_contains "$SAME_LINE_VAR_LOG" "same-line-variable-use-fail.sh: note: RUN exit code: 0"
assert_file_contains "$SAME_LINE_VAR_LOG" "same-line-variable-use-fail.sh:10: error: undefined variable [[value]] in CHECK"

set +e
"$RUN_AND_CHECK" "$TEST_DIR/same-line-without-previous-fail.sh" >"$SAME_LINE_WITHOUT_PREVIOUS_LOG" 2>&1
status=$?
set -e

[ "$status" -ne 0 ] ||
    fail "CHECK-SAME without a previous positive match unexpectedly passed"
assert_file_contains "$SAME_LINE_WITHOUT_PREVIOUS_LOG" "same-line-without-previous-fail.sh: note: RUN exit code: 0"
assert_file_contains "$SAME_LINE_WITHOUT_PREVIOUS_LOG" "same-line-without-previous-fail.sh:10: error: CHECK-SAME requires a previous positive match"

set +e
"$RUN_AND_CHECK" "$TEST_DIR/not-whole-line-anchor-fail.sh" >"$WHOLE_LINE_NOT_LOG" 2>&1
status=$?
set -e

[ "$status" -ne 0 ] || fail "whole-line anchored CHECK-NOT failure unexpectedly passed"
assert_file_contains "$WHOLE_LINE_NOT_LOG" "not-whole-line-anchor-fail.sh: note: RUN exit code: 0"
assert_file_contains "$WHOLE_LINE_NOT_LOG" "not-whole-line-anchor-fail.sh:13: error: CHECK-NOT matched forbidden output"
assert_file_contains "$WHOLE_LINE_NOT_LOG" "not-whole-line-anchor-fail.sh:13: note: pattern: {{^b$}}"
assert_file_contains "$WHOLE_LINE_NOT_LOG" "not-whole-line-anchor-fail.sh:13: note: output line 2: b"

set +e
IMGNEKO_TEST_OUTPUT_DIR= "$RUN_AND_CHECK" "$TEST_DIR/output-dir.sh" >"$OUTPUT_DIR_LOG" 2>&1
status=$?
set -e

[ "$status" -eq 0 ] || fail "temp output dir run unexpectedly failed"
assert_file_contains "$OUTPUT_DIR_LOG" "output-dir.sh: note: RUN: sh '$TEST_DIR/output-dir.sh'"
assert_file_contains "$OUTPUT_DIR_LOG" "output-dir.sh: note: stdout file: /tmp/imgneko-run-and-check."
assert_file_contains "$OUTPUT_DIR_LOG" "output-dir.sh: note: stderr file: /tmp/imgneko-run-and-check."
assert_file_contains "$OUTPUT_DIR_LOG" "output-dir.sh: note: RUN exit code: 0"
