#!/usr/bin/env run-and-check
# SPDX-License-Identifier: MIT-0
# RUN: sh %s

# Verify shutdown when a timed-out child has already exited but is still tracked
# by the runner alongside another running child.

set -eu

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
. "$SCRIPT_DIR/signal-shutdown-common.sh"

# Interrupt the runner after one child has completed timeout cleanup and a later
# child is still keeping the event loop asleep.
run_runner_completed_timeout_shutdown_test() {
    signal_name=$1
    expected_status=$2
    log_path=$3
    output_dir=$4
    state_dir=$5

    mkdir "$state_dir"

    IMGNEKO_RUNNER_SIGNAL_STATE_DIR="$state_dir" \
        "$RUNNER" --tests-dir "$RUNNER_SIGNAL_TEST_DIR" --output-dir "$output_dir" \
        --timeout 5.0 -j 2 \
        --filter 'j-timeout-exit-on-term.sh|k-delay-slot.sh|l-signal-window.sh' \
        >"$log_path" 2>&1 &
    runner_pid=$!

    # k exits first and frees the second job slot. That lets l start later than
    # j, so by the time we interrupt the runner:
    # - j has already timed out, exited on that timeout TERM, and aged past its
    #   output-drain deadline, and
    # - l is still running and keeping the event loop blocked in pselect().
    wait_for_path "$state_dir/l.started"
    # Keep enough wall-clock space for the timeout path to settle under macOS
    # ASan load before delivering the interrupt.
    sleep 2.5
    kill "-$signal_name" "$runner_pid"

    set +e
    wait "$runner_pid"
    status=$?
    set -e

    [ "$status" -eq "$expected_status" ] ||
        fail "unexpected completed-timeout runner exit status for $signal_name: $status"

    wait_for_path "$state_dir/l.term"
    l_pid=$(cat "$state_dir/l.pid")
    wait_for_process_gone "$l_pid"
}

require_runner_signal_shutdown_env

RUNNER_SIGNAL_TEST_DIR=$IMGNEKO_TEST_OUTPUT_DIR/runner-signal-completed-timeout-tests
COMPLETED_TIMEOUT_INTERRUPT_OUTPUT_DIR=$IMGNEKO_TEST_OUTPUT_DIR/runner-completed-timeout-interrupt-output
COMPLETED_TIMEOUT_INTERRUPT_LOG=$IMGNEKO_TEST_OUTPUT_DIR/runner-completed-timeout-interrupt.log
COMPLETED_TIMEOUT_INTERRUPT_STATE_DIR=$IMGNEKO_TEST_OUTPUT_DIR/runner-completed-timeout-interrupt-state

mkdir -p "$RUNNER_SIGNAL_TEST_DIR"

# Exits promptly once the ordinary timeout path sends TERM. The shutdown test
# uses it to cover the branch that sees an already-complete timed-out child in
# running_tests when shutdown begins.
cat >"$RUNNER_SIGNAL_TEST_DIR/j-timeout-exit-on-term.sh" <<'EOF'
#!/bin/sh
trap 'exit 0' TERM
while :; do
    sleep 0.1
done
EOF
chmod +x "$RUNNER_SIGNAL_TEST_DIR/j-timeout-exit-on-term.sh"

# Delayed child that exits before j times out, freeing a job slot so the
# signal-window child can start later and therefore have a later timeout
# deadline than j. The delay is intentionally wide for macOS ASan scheduling.
cat >"$RUNNER_SIGNAL_TEST_DIR/k-delay-slot.sh" <<'EOF'
#!/bin/sh
sleep 3
EOF
chmod +x "$RUNNER_SIGNAL_TEST_DIR/k-delay-slot.sh"

# Long-running child that simply keeps the event loop in pselect() until the
# outer test interrupts the runner after j has fully completed timeout cleanup.
cat >"$RUNNER_SIGNAL_TEST_DIR/l-signal-window.sh" <<'EOF'
#!/bin/sh
set -eu
state_dir=${IMGNEKO_RUNNER_SIGNAL_STATE_DIR:?}
printf '%s\n' "$$" >"$state_dir/l.pid"
: >"$state_dir/l.started"
trap ': >"$state_dir/l.term"; exit 0' TERM INT
while :; do
    sleep 0.1
done
EOF
chmod +x "$RUNNER_SIGNAL_TEST_DIR/l-signal-window.sh"

echo '== runner completed timeout interrupt =='
run_runner_completed_timeout_shutdown_test \
    INT 130 "$COMPLETED_TIMEOUT_INTERRUPT_LOG" \
    "$COMPLETED_TIMEOUT_INTERRUPT_OUTPUT_DIR" \
    "$COMPLETED_TIMEOUT_INTERRUPT_STATE_DIR"
cat "$COMPLETED_TIMEOUT_INTERRUPT_LOG"
# CHECK: == runner completed timeout interrupt ==
# CHECK: RUN: j-timeout-exit-on-term.sh
# CHECK: RUN: k-delay-slot.sh
# k's PASS, l's RUN, and j's TIMEOUT all happen near the same scheduling window,
# so accept any ordering among those three lines.
# CHECK-DAG: PASS: k-delay-slot.sh
# CHECK-DAG: RUN: l-signal-window.sh
# CHECK-DAG: TIMEOUT: j-timeout-exit-on-term.sh
# CHECK: Summary:
# CHECK: discovered: 3
# CHECK: passed: 1
# CHECK: timeout: 1
# CHECK: interrupted: 1
# CHECK: Interrupted by SIGINT.
# CHECK: Result: INTERRUPTED
# CHECK-NOT: PASS: j-timeout-exit-on-term.sh
# CHECK-NOT: PASS: l-signal-window.sh
