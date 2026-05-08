#!/usr/bin/env run-and-check
# SPDX-License-Identifier: MIT-0
# RUN: sh %s

# Verify that shutdown is still handled when SIGINT/SIGTERM arrives during the
# runner's artificial captured-output drain delay.

set -eu

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
. "$SCRIPT_DIR/signal-shutdown-common.sh"

# Send a signal while the runner is delaying after a passthrough chunk so the
# shutdown request remains pending until the next wait-loop poll.
run_runner_pending_signal_shutdown_test() {
    signal_name=$1
    expected_status=$2
    log_path=$3
    output_dir=$4
    state_dir=$5

    mkdir "$state_dir"

    IMGNEKO_RUNNER_SIGNAL_STATE_DIR="$state_dir" \
        "$RUNNER" --tests-dir "$RUNNER_SIGNAL_TEST_DIR" --output-dir "$output_dir" \
        --output-passthrough --debug-parent-output-chunk-delay 1 \
        -j 2 --filter 'e-output-pending-stop.sh|f-closed-fds-success.sh' \
        >"$log_path" 2>&1 &
    runner_pid=$!

    # Wait until the passthrough output appears. The runner prints the chunk to
    # its own stdout before sleeping in the debug output-drain delay, so a
    # signal sent here remains pending until the next shutdown poll instead of
    # being handled directly in pselect().
    # Poll tightly here so the signal lands while the runner is still inside
    # the debug output-drain delay triggered by the passthrough chunk.
    wait_for_file_contains "$log_path" "pending stop marker" 0.01
    kill "-$signal_name" "$runner_pid"

    set +e
    wait "$runner_pid"
    status=$?
    set -e

    [ "$status" -eq "$expected_status" ] ||
        fail "unexpected pending-signal runner exit status for $signal_name: $status"

    wait_for_path "$state_dir/e.term"
    e_pid=$(cat "$state_dir/e.pid")
    wait_for_process_gone "$e_pid"
}

require_runner_signal_shutdown_env

RUNNER_SIGNAL_TEST_DIR=$IMGNEKO_TEST_OUTPUT_DIR/runner-signal-pending-tests
PENDING_INTERRUPT_OUTPUT_DIR=$IMGNEKO_TEST_OUTPUT_DIR/runner-pending-interrupt-output
PENDING_TERMINATE_OUTPUT_DIR=$IMGNEKO_TEST_OUTPUT_DIR/runner-pending-terminate-output
PENDING_INTERRUPT_LOG=$IMGNEKO_TEST_OUTPUT_DIR/runner-pending-interrupt.log
PENDING_TERMINATE_LOG=$IMGNEKO_TEST_OUTPUT_DIR/runner-pending-terminate.log
PENDING_INTERRUPT_STATE_DIR=$IMGNEKO_TEST_OUTPUT_DIR/runner-pending-interrupt-state
PENDING_TERMINATE_STATE_DIR=$IMGNEKO_TEST_OUTPUT_DIR/runner-pending-terminate-state

mkdir -p "$RUNNER_SIGNAL_TEST_DIR"

# Emits one passthrough chunk, then waits so the stop signal stays pending
# until the runner leaves its artificial output-drain delay.
cat >"$RUNNER_SIGNAL_TEST_DIR/e-output-pending-stop.sh" <<'EOF'
#!/bin/sh
set -eu
state_dir=${IMGNEKO_RUNNER_SIGNAL_STATE_DIR:?}
printf '%s\n' "$$" >"$state_dir/e.pid"
trap ': >"$state_dir/e.term"; exit 0' TERM INT
printf '%s\n' 'pending stop marker'
while :; do
    sleep 0.1
done
EOF
chmod +x "$RUNNER_SIGNAL_TEST_DIR/e-output-pending-stop.sh"

# Long-running child with stdout/stderr closed. Keeping it alive until TERM/INT
# avoids the earlier race where it could exit before the runner observed the
# pending shutdown signal.
cat >"$RUNNER_SIGNAL_TEST_DIR/f-closed-fds-success.sh" <<'EOF'
#!/bin/sh
trap 'exit 0' TERM INT
exec 1>&- 2>&-
while :; do
    sleep 0.1
done
EOF
chmod +x "$RUNNER_SIGNAL_TEST_DIR/f-closed-fds-success.sh"

echo '== runner pending interrupt =='
run_runner_pending_signal_shutdown_test INT 130 "$PENDING_INTERRUPT_LOG" \
    "$PENDING_INTERRUPT_OUTPUT_DIR" "$PENDING_INTERRUPT_STATE_DIR"
cat "$PENDING_INTERRUPT_LOG"
# CHECK: == runner pending interrupt ==
# CHECK: RUN: e-output-pending-stop.sh
# CHECK: RUN: f-closed-fds-success.sh
# CHECK: pending stop marker
# CHECK: Summary:
# CHECK: discovered: 2
# CHECK: interrupted: 2
# CHECK: Interrupted by SIGINT.
# CHECK: Result: INTERRUPTED
# CHECK-NOT: PASS: e-output-pending-stop.sh
# CHECK-NOT: PASS: f-closed-fds-success.sh

echo '== runner pending terminate =='
run_runner_pending_signal_shutdown_test TERM 143 "$PENDING_TERMINATE_LOG" \
    "$PENDING_TERMINATE_OUTPUT_DIR" "$PENDING_TERMINATE_STATE_DIR"
cat "$PENDING_TERMINATE_LOG"
# CHECK: == runner pending terminate ==
# CHECK: RUN: e-output-pending-stop.sh
# CHECK: RUN: f-closed-fds-success.sh
# CHECK: pending stop marker
# CHECK: Summary:
# CHECK: discovered: 2
# CHECK: interrupted: 2
# CHECK: Terminated by SIGTERM.
# CHECK: Result: INTERRUPTED
# CHECK-NOT: PASS: e-output-pending-stop.sh
# CHECK-NOT: PASS: f-closed-fds-success.sh
