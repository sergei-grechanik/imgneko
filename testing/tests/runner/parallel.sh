#!/usr/bin/env run-and-check
# RUN: sh %s

# Verify that -j schedules more than one test child at a time. The `a-*` test
# waits for the `b-*` test to announce that it started, so `-j 1` must fail and
# `-j 2` must pass.

set -eu

fail() {
    printf '%s\n' "$1" >&2
    exit 1
}

RUNNER=$IMGNEKO_BUILD_DIR/bin/test-runner
PARALLEL_TEST_DIR=$IMGNEKO_TEST_OUTPUT_DIR/parallel-tests
SERIAL_OUTPUT_DIR=$IMGNEKO_TEST_OUTPUT_DIR/parallel-serial-output
PARALLEL_OUTPUT_DIR=$IMGNEKO_TEST_OUTPUT_DIR/parallel-output
START_MARKER=$PARALLEL_TEST_DIR/b-started

[ -x "$RUNNER" ] || fail "missing test-runner binary: $RUNNER"
mkdir -p "$PARALLEL_TEST_DIR"

cat >"$PARALLEL_TEST_DIR/a-waits-for-b.sh" <<EOF
#!/bin/sh
set -eu

i=0
while [ "\$i" -lt 40 ]; do
    if [ -f "$START_MARKER" ]; then
        exit 0
    fi
    sleep 0.05
    i=\$((i + 1))
done

printf '%s\n' 'b did not start in time' >&2
exit 1
EOF
chmod +x "$PARALLEL_TEST_DIR/a-waits-for-b.sh"

cat >"$PARALLEL_TEST_DIR/b-starts.sh" <<EOF
#!/bin/sh
set -eu

: >"$START_MARKER"
sleep 0.2
EOF
chmod +x "$PARALLEL_TEST_DIR/b-starts.sh"

rm -f "$START_MARKER"
echo '== serial jobs =='
"$RUNNER" --tests-dir "$PARALLEL_TEST_DIR" --output-dir "$SERIAL_OUTPUT_DIR" \
    -j 1 2>&1 || true
# CHECK: == serial jobs ==
# CHECK: Starting test run: 1 job, 2 discovered tests
# CHECK: RUN: a-waits-for-b.sh
# CHECK: FAIL: a-waits-for-b.sh
# CHECK: RUN: b-starts.sh
# CHECK: PASS: b-starts.sh
# CHECK: failed tests:
# CHECK: a-waits-for-b.sh
# CHECK: Result: FAILURE

rm -f "$START_MARKER"
echo '== parallel jobs =='
"$RUNNER" --tests-dir "$PARALLEL_TEST_DIR" --output-dir "$PARALLEL_OUTPUT_DIR" \
    -j 2 2>&1
# CHECK: == parallel jobs ==
# CHECK: Starting test run: 2 jobs, 2 discovered tests
# CHECK: RUN: a-waits-for-b.sh
# CHECK: RUN: b-starts.sh
# CHECK: PASS: a-waits-for-b.sh
# CHECK: PASS: b-starts.sh
# CHECK: Summary:
# CHECK: discovered: 2
# CHECK: passed: 2
# CHECK: Result: SUCCESS
