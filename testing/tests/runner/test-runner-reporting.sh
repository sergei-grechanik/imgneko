#!/usr/bin/env run-and-check
# SPDX-License-Identifier: MIT-0
# RUN: sh %s

# Verify that test-runner writes a root-level timing file, reports its path
# before the final result line, and records `time outcome test_name` entries
# for representative outcomes.

set -eu

fail() {
    printf '%s\n' "$1" >&2
    exit 1
}

if [ -z "${IMGNEKO_TEST_OUTPUT_DIR:-}" ] ||
   [ ! -d "$IMGNEKO_TEST_OUTPUT_DIR" ]; then
    fail "IMGNEKO_TEST_OUTPUT_DIR is not set to an existing directory"
fi

RUNNER=$IMGNEKO_BUILD_DIR/bin/test-runner
[ -x "$RUNNER" ] || fail "missing test-runner binary: $RUNNER"

PASS_OUTPUT_DIR=$IMGNEKO_TEST_OUTPUT_DIR/reporting-pass
MARKERS_OUTPUT_DIR=$IMGNEKO_TEST_OUTPUT_DIR/reporting-markers
TIMEOUT_OUTPUT_DIR=$IMGNEKO_TEST_OUTPUT_DIR/reporting-timeout
PASS_TIMING_FILE=$PASS_OUTPUT_DIR/test-times.txt
MARKERS_TIMING_FILE=$MARKERS_OUTPUT_DIR/test-times.txt
TIMEOUT_TIMING_FILE=$TIMEOUT_OUTPUT_DIR/test-times.txt

echo '== pass timing file path =='
printf 'expected timing file: %s\n' "$PASS_TIMING_FILE"
# CHECK: == pass timing file path ==
# CHECK-NEXT: expected timing file: [[pass_timing_file:.*/reporting-pass/test-times\.txt]]

"$RUNNER" --jobs=1 --output-dir "$PASS_OUTPUT_DIR" \
    runner/output.sh 2>&1
# CHECK: Starting test run: 1 job, 1 discovered test
# CHECK: RUN: runner/output.sh
# CHECK: PASS: runner/output.sh
# CHECK: Summary:
# CHECK: discovered: 1
# CHECK: passed: 1
# CHECK: Time: {{[0-9]+[.][0-9]+}} s
# CHECK-NEXT: Output dir: [[pass_output_dir:[a-zA-Z0-9_/-]+]]
# CHECK-NEXT: Timing file: [[pass_timing_file]]
# CHECK-NEXT: Result: SUCCESS

echo '== pass timing file =='
cat "$PASS_TIMING_FILE"
echo '== pass timing file end =='
# CHECK: == pass timing file ==
# CHECK-NEXT: {{^[0-9]+[.][0-9][0-9] pass runner/output\.sh$}}
# CHECK-NEXT: == pass timing file end ==

echo '== mixed timing file path =='
printf 'expected timing file: %s\n' "$MARKERS_TIMING_FILE"
# CHECK: == mixed timing file path ==
# CHECK-NEXT: expected timing file: [[markers_timing_file:.*/reporting-markers/test-times\.txt]]

"$RUNNER" --jobs=1 --output-dir "$MARKERS_OUTPUT_DIR" \
    'runner/markers.c|runner/xfail.sh|runner/disabled.sh' 2>&1
# CHECK: Starting test run: 1 job, 4 discovered tests
# CHECK: DISABLED: runner/disabled.sh
# CHECK: DISABLED: runner/markers.c/marked_disabled
# CHECK: XFAIL: runner/markers.c/marked_xfail
# CHECK: XFAIL: runner/xfail.sh
# CHECK: Summary:
# CHECK: discovered: 4
# CHECK: xfailed: 2
# CHECK: disabled: 2
# CHECK: Time: {{[0-9]+[.][0-9]+}} s
# CHECK-NEXT: Output dir: [[markers_output_dir:[a-zA-Z0-9_/-]+]]
# CHECK-NEXT: Timing file: [[markers_timing_file]]
# CHECK-NEXT: Result: SUCCESS

echo '== mixed timing file =='
cat "$MARKERS_TIMING_FILE"
echo '== mixed timing file end =='
# CHECK: == mixed timing file ==
# CHECK-NEXT: {{^[0-9]+[.][0-9][0-9] disabled runner/disabled\.sh$}}
# CHECK-NEXT: {{^[0-9]+[.][0-9][0-9] disabled runner/markers\.c/marked_disabled$}}
# CHECK-NEXT: {{^[0-9]+[.][0-9][0-9] xfail runner/markers\.c/marked_xfail$}}
# CHECK-NEXT: {{^[0-9]+[.][0-9][0-9] xfail runner/xfail\.sh$}}
# CHECK-NEXT: == mixed timing file end ==

echo '== timeout timing file path =='
printf 'expected timing file: %s\n' "$TIMEOUT_TIMING_FILE"
# CHECK: == timeout timing file path ==
# CHECK-NEXT: expected timing file: [[timeout_timing_file:.*/reporting-timeout/test-times\.txt]]

"$RUNNER" --jobs=1 --output-dir "$TIMEOUT_OUTPUT_DIR" --timeout 1 \
    runner/timeout.sh 2>&1 || true
# CHECK: Starting test run: 1 job, 1 discovered test
# CHECK: RUN: runner/timeout.sh
# CHECK: TIMEOUT: runner/timeout.sh
# CHECK: timed out tests:
# CHECK-NEXT:   runner/timeout.sh
# CHECK: Summary:
# CHECK: discovered: 1
# CHECK: timeout: 1
# CHECK: Time: {{[0-9]+[.][0-9]+}} s
# CHECK-NEXT: Output dir: [[timeout_output_dir:[a-zA-Z0-9_/-]+]]
# CHECK-NEXT: Timing file: [[timeout_timing_file]]
# CHECK-NEXT: Result: FAILURE

echo '== timeout timing file =='
cat "$TIMEOUT_TIMING_FILE"
echo '== timeout timing file end =='
# CHECK: == timeout timing file ==
# CHECK-NEXT: {{^[0-9]+[.][0-9][0-9] timeout runner/timeout\.sh$}}
# CHECK-NEXT: == timeout timing file end ==
