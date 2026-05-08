#!/usr/bin/env run-and-check
# SPDX-License-Identifier: MIT-0
# RUN: sh %s

# Verify that when test-runner itself is interrupted or terminated, it stops
# scheduling new tests, gracefully winds down running children, and prints an
# interrupted partial summary.

set -eu

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
. "$SCRIPT_DIR/signal-shutdown-common.sh"

# Run the nested runner until both long-running children have started, then
# stop the runner and verify that queued work never starts.
run_runner_signal_shutdown_test() {
    signal_name=$1
    expected_status=$2
    log_path=$3
    output_dir=$4
    state_dir=$5

    mkdir "$state_dir"

    IMGNEKO_RUNNER_SIGNAL_STATE_DIR="$state_dir" \
        "$RUNNER" --tests-dir "$RUNNER_SIGNAL_TEST_DIR" --output-dir "$output_dir" \
        -j 2 --filter 'a-fast-success.sh|b-slow-abort.sh|c-slow-abort.sh|d-never-started.sh' \
        >"$log_path" 2>&1 &
    runner_pid=$!

    # Wait until the fast test has passed and the replacement slow test has
    # started before interrupting the runner. That makes the partial summary
    # deterministic and also proves the remaining children get terminated.
    wait_for_path "$state_dir/c.started"
    kill "-$signal_name" "$runner_pid"

    set +e
    wait "$runner_pid"
    status=$?
    set -e

    [ "$status" -eq "$expected_status" ] ||
        fail "unexpected runner exit status for $signal_name: $status"

    wait_for_path "$state_dir/b.term"
    wait_for_path "$state_dir/c.term"
    b_pid=$(cat "$state_dir/b.pid")
    c_pid=$(cat "$state_dir/c.pid")
    wait_for_process_gone "$b_pid"
    wait_for_process_gone "$c_pid"
    [ ! -e "$state_dir/d.started" ] || fail "d-never-started.sh unexpectedly ran"
}

require_runner_signal_shutdown_env

RUNNER_SIGNAL_TEST_DIR=$IMGNEKO_TEST_OUTPUT_DIR/runner-signal-basic-tests
INTERRUPT_OUTPUT_DIR=$IMGNEKO_TEST_OUTPUT_DIR/runner-interrupt-output
TERMINATE_OUTPUT_DIR=$IMGNEKO_TEST_OUTPUT_DIR/runner-terminate-output
INTERRUPT_LOG=$IMGNEKO_TEST_OUTPUT_DIR/runner-interrupt.log
TERMINATE_LOG=$IMGNEKO_TEST_OUTPUT_DIR/runner-terminate.log
INTERRUPT_STATE_DIR=$IMGNEKO_TEST_OUTPUT_DIR/runner-interrupt-state
TERMINATE_STATE_DIR=$IMGNEKO_TEST_OUTPUT_DIR/runner-terminate-state

mkdir -p "$RUNNER_SIGNAL_TEST_DIR"

cat >"$RUNNER_SIGNAL_TEST_DIR/a-fast-success.sh" <<'EOF'
#!/bin/sh
exit 0
EOF
chmod +x "$RUNNER_SIGNAL_TEST_DIR/a-fast-success.sh"

# Long-running child that records its pid/start and acknowledges TERM/INT with
# a marker file before exiting. The test uses that marker to confirm the runner
# propagated shutdown to the child process group.
cat >"$RUNNER_SIGNAL_TEST_DIR/b-slow-abort.sh" <<'EOF'
#!/bin/sh
set -eu
state_dir=${IMGNEKO_RUNNER_SIGNAL_STATE_DIR:?}
printf '%s\n' "$$" >"$state_dir/b.pid"
: >"$state_dir/b.started"
trap ': >"$state_dir/b.term"; exit 0' TERM INT
while :; do
    sleep 0.1
done
EOF
chmod +x "$RUNNER_SIGNAL_TEST_DIR/b-slow-abort.sh"

# Second long-running child used to prove the runner stops queued work too.
cat >"$RUNNER_SIGNAL_TEST_DIR/c-slow-abort.sh" <<'EOF'
#!/bin/sh
set -eu
state_dir=${IMGNEKO_RUNNER_SIGNAL_STATE_DIR:?}
printf '%s\n' "$$" >"$state_dir/c.pid"
: >"$state_dir/c.started"
trap ': >"$state_dir/c.term"; exit 0' TERM INT
while :; do
    sleep 0.1
done
EOF
chmod +x "$RUNNER_SIGNAL_TEST_DIR/c-slow-abort.sh"

# This test should never start, it will write a marker if it does.
cat >"$RUNNER_SIGNAL_TEST_DIR/d-never-started.sh" <<'EOF'
#!/bin/sh
set -eu
state_dir=${IMGNEKO_RUNNER_SIGNAL_STATE_DIR:?}
: >"$state_dir/d.started"
exit 0
EOF
chmod +x "$RUNNER_SIGNAL_TEST_DIR/d-never-started.sh"

echo '== runner interrupted =='
run_runner_signal_shutdown_test INT 130 "$INTERRUPT_LOG" \
    "$INTERRUPT_OUTPUT_DIR" "$INTERRUPT_STATE_DIR"
cat "$INTERRUPT_LOG"
# CHECK: == runner interrupted ==
# CHECK: RUN: a-fast-success.sh
# CHECK: RUN: b-slow-abort.sh
# CHECK: PASS: a-fast-success.sh
# CHECK: RUN: c-slow-abort.sh
# CHECK: Summary:
# CHECK: discovered: 4
# CHECK: passed: 1
# CHECK: interrupted: 3
# CHECK: Interrupted by SIGINT.
# CHECK: Result: INTERRUPTED
# CHECK-NOT: PASS: b-slow-abort.sh
# CHECK-NOT: PASS: c-slow-abort.sh
# CHECK-NOT: RUN: d-never-started.sh
# CHECK-NOT: FAIL: b-slow-abort.sh
# CHECK-NOT: FAIL: c-slow-abort.sh
# CHECK-NOT: TIMEOUT: b-slow-abort.sh
# CHECK-NOT: TIMEOUT: c-slow-abort.sh

echo '== runner terminated =='
run_runner_signal_shutdown_test TERM 143 "$TERMINATE_LOG" \
    "$TERMINATE_OUTPUT_DIR" "$TERMINATE_STATE_DIR"
cat "$TERMINATE_LOG"
# CHECK: == runner terminated ==
# CHECK: RUN: a-fast-success.sh
# CHECK: RUN: b-slow-abort.sh
# CHECK: PASS: a-fast-success.sh
# CHECK: RUN: c-slow-abort.sh
# CHECK: Summary:
# CHECK: discovered: 4
# CHECK: passed: 1
# CHECK: interrupted: 3
# CHECK: Terminated by SIGTERM.
# CHECK: Result: INTERRUPTED
# CHECK-NOT: PASS: b-slow-abort.sh
# CHECK-NOT: PASS: c-slow-abort.sh
# CHECK-NOT: RUN: d-never-started.sh
# CHECK-NOT: FAIL: b-slow-abort.sh
# CHECK-NOT: FAIL: c-slow-abort.sh
# CHECK-NOT: TIMEOUT: b-slow-abort.sh
# CHECK-NOT: TIMEOUT: c-slow-abort.sh
