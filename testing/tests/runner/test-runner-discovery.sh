#!/usr/bin/env run-and-check
# RUN: sh %s

# Additional tests for test-runner discovery and reporting.

set -eu

fail() {
    echo "$1" >&2
    exit 1
}

RUNNER=$IMGNEKO_BUILD_DIR/bin/test-runner

TMP_TEST_DIR=$IMGNEKO_TEST_OUTPUT_DIR/tmp-runner-discovery
TMP_C_TEST_DIR=$IMGNEKO_TEST_OUTPUT_DIR/tmp-runner-c-tests
AMBIGUOUS_TEST_DIR=$IMGNEKO_TEST_OUTPUT_DIR/ambiguous-runner-tests
MULTIPLE_MARKERS_TEST_DIR=$IMGNEKO_TEST_OUTPUT_DIR/multiple-marker-tests
FAKE_TEST_BIN_DIR=$IMGNEKO_TEST_OUTPUT_DIR/fake-test-bin
FAKE_NO_SUBTESTS_BIN=$FAKE_TEST_BIN_DIR/runner/no-subtests.c.bin
NO_SUBTESTS_OUTPUT_DIR=$IMGNEKO_TEST_OUTPUT_DIR/no-subtests-output
SPACED_OUTPUT_DIR=$IMGNEKO_TEST_OUTPUT_DIR/spaced-output
EMPTY_FAIL_OUTPUT_DIR=$IMGNEKO_TEST_OUTPUT_DIR/empty-fail-output
MISSING_FAIL_OUTPUT_DIR=$IMGNEKO_TEST_OUTPUT_DIR/missing-fail-output
SIGNALED_OUTPUT_DIR=$IMGNEKO_TEST_OUTPUT_DIR/signaled-output

[ -x "$RUNNER" ] || fail "missing test-runner binary: $RUNNER"
mkdir -p "$TMP_TEST_DIR" "$TMP_C_TEST_DIR/runner" "$FAKE_TEST_BIN_DIR/runner"
mkdir -p "$AMBIGUOUS_TEST_DIR" "$MULTIPLE_MARKERS_TEST_DIR"
mkfifo "$TMP_TEST_DIR/ignored-fifo"
echo 'ignored regular file' >"$TMP_TEST_DIR/ignored.txt"
cp "$IMGNEKO_ROOT_DIR/testing/tests/runner/no-subtests.c" \
    "$TMP_C_TEST_DIR/runner/no-subtests.c"

cat >"$TMP_TEST_DIR/empty-output-fail.sh" <<'EOF'
#!/bin/sh
exit 1
EOF
chmod +x "$TMP_TEST_DIR/empty-output-fail.sh"

cat >"$TMP_TEST_DIR/missing-output-fail.sh" <<'EOF'
#!/bin/sh
rm output
echo 'captured output path removed'
exit 1
EOF
chmod +x "$TMP_TEST_DIR/missing-output-fail.sh"

cat >"$TMP_TEST_DIR/signaled.sh" <<'EOF'
#!/bin/sh
kill -TERM $$
EOF
chmod +x "$TMP_TEST_DIR/signaled.sh"

echo '== list custom c tests =='
"$RUNNER" --jobs=1 --list --filter 'runner/no-subtests.c|runner/spaced-subtests.c' 2>&1
# CHECK: == list custom c tests ==
# CHECK: runner/no-subtests.c
# CHECK: runner/spaced-subtests.c/marked_disabled DISABLED
# CHECK: runner/spaced-subtests.c/marked_xfail XFAIL
# CHECK: runner/spaced-subtests.c/plain_spaced

echo '== run no subtests =='
"$RUNNER" --jobs=1 --output-dir "$NO_SUBTESTS_OUTPUT_DIR" \
    --filter runner/no-subtests.c 2>&1
# CHECK: == run no subtests ==
# CHECK: RUN: runner/no-subtests.c
# CHECK: PASS: runner/no-subtests.c
# CHECK: Summary:
# CHECK: discovered: 1
# CHECK: passed: 1
# CHECK: Result: SUCCESS

echo '== run spaced subtests =='
"$RUNNER" --jobs=1 --output-dir "$SPACED_OUTPUT_DIR" \
    --filter runner/spaced-subtests.c 2>&1
# CHECK: == run spaced subtests ==
# CHECK: DISABLED: runner/spaced-subtests.c/marked_disabled
# CHECK: RUN: runner/spaced-subtests.c/marked_xfail
# CHECK: XFAIL: runner/spaced-subtests.c/marked_xfail
# CHECK: RUN: runner/spaced-subtests.c/plain_spaced
# CHECK: PASS: runner/spaced-subtests.c/plain_spaced
# CHECK: Summary:
# CHECK: discovered: 3
# CHECK: passed: 1
# CHECK: xfailed: 1
# CHECK: disabled: 1
# CHECK: Result: SUCCESS

echo '== empty output tail =='
"$RUNNER" --jobs=1 --tests-dir "$TMP_TEST_DIR" --output-dir "$EMPTY_FAIL_OUTPUT_DIR" \
    --filter empty-output-fail.sh 2>&1 || true
# CHECK: == empty output tail ==
# CHECK: RUN: empty-output-fail.sh
# CHECK: FAIL: empty-output-fail.sh
# CHECK: output is empty: {{.*empty-output-fail\.sh/output}}
# CHECK: failed tests:
# CHECK: empty-output-fail.sh
# CHECK: Result: FAILURE

echo '== missing output tail =='
"$RUNNER" --jobs=1 --tests-dir "$TMP_TEST_DIR" --output-dir "$MISSING_FAIL_OUTPUT_DIR" \
    --filter missing-output-fail.sh 2>&1 || true
# CHECK: == missing output tail ==
# CHECK: RUN: missing-output-fail.sh
# CHECK: FAIL: missing-output-fail.sh
# CHECK: error: failed to read captured output {{.*missing-output-fail\.sh/output}}: No such file or directory
# CHECK: failed tests:
# CHECK: missing-output-fail.sh
# CHECK: Result: FAILURE

echo '== signaled executable =='
"$RUNNER" --jobs=1 --tests-dir "$TMP_TEST_DIR" --output-dir "$SIGNALED_OUTPUT_DIR" \
    --filter signaled.sh 2>&1 || true
# CHECK: == signaled executable ==
# CHECK: RUN: signaled.sh
# CHECK: FAIL: signaled.sh
# CHECK: output is empty: {{.*signaled\.sh/output}}
# CHECK: failed tests:
# CHECK: signaled.sh
# CHECK: Result: FAILURE

cat >"$AMBIGUOUS_TEST_DIR/ambiguous-marker.sh" <<'EOF'
#!/bin/sh
# XFAIL DISABLED
exit 0
EOF
chmod +x "$AMBIGUOUS_TEST_DIR/ambiguous-marker.sh"

echo '== ambiguous markers =='
"$RUNNER" --jobs=1 --list --tests-dir "$AMBIGUOUS_TEST_DIR" \
    --filter ambiguous-marker.sh 2>&1 || true
# CHECK: == ambiguous markers ==
# CHECK: error: ambiguous test markers in line: # XFAIL DISABLED

cat >"$MULTIPLE_MARKERS_TEST_DIR/multiple-markers.sh" <<'EOF'
#!/bin/sh
# XFAIL
# DISABLED
exit 0
EOF
chmod +x "$MULTIPLE_MARKERS_TEST_DIR/multiple-markers.sh"

echo '== multiple markers =='
"$RUNNER" --jobs=1 --list --tests-dir "$MULTIPLE_MARKERS_TEST_DIR" \
    --filter multiple-markers.sh 2>&1 || true
# CHECK: == multiple markers ==
# CHECK: error: multiple test markers found in {{.*multiple-markers\.sh}}

echo '== missing c binary =='
"$RUNNER" --jobs=1 --list --tests-dir "$TMP_C_TEST_DIR" \
    --test-bin-dir "$FAKE_TEST_BIN_DIR" \
    --filter runner/no-subtests.c 2>&1 || true
# CHECK: == missing c binary ==
# CHECK: error: missing built C test binary for runner/no-subtests.c at {{.*runner/no-subtests\.c\.bin}}
# CHECK: build C tests first with `make -C {{.*}} test-programs` or `make -C {{.*}} test`

cat >"$FAKE_NO_SUBTESTS_BIN" <<'EOF'
#!/bin/sh
kill -TERM $$
EOF
chmod +x "$FAKE_NO_SUBTESTS_BIN"
echo '== signaled c list =='
"$RUNNER" --jobs=1 --list --tests-dir "$TMP_C_TEST_DIR" \
    --test-bin-dir "$FAKE_TEST_BIN_DIR" \
    --filter runner/no-subtests.c 2>&1 || true
# CHECK: == signaled c list ==
# CHECK: error: runner/no-subtests.c --list failed with status 143
