#!/usr/bin/env run-and-check
# SPDX-License-Identifier: MIT-0
# RUN: sh %s

# Verify pending shutdown when the runner has a mix of completed output,
# closed-output running children, and children already inside timeout cleanup.

set -eu

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
. "$SCRIPT_DIR/signal-shutdown-common.sh"

# Send a shutdown signal while output-drain delay leaves the runner with several
# different running-test states that all must become interrupted.
run_runner_pending_signal_mixed_state_test() {
    signal_name=$1
    expected_status=$2
    log_path=$3
    output_dir=$4
    state_dir=$5

    mkdir "$state_dir"

    # These windows are longer than the logical race needs because macOS ASan
    # runs can be slow enough for shorter values to collapse the intended mixed
    # state before the interrupt is sent.
    IMGNEKO_RUNNER_SIGNAL_STATE_DIR="$state_dir" \
        "$RUNNER" --tests-dir "$RUNNER_SIGNAL_TEST_DIR" --output-dir "$output_dir" \
        --output-passthrough --debug-parent-output-chunk-delay 6 --timeout 5.0 \
        -j 3 --filter 'g-output-then-exit.sh|h-closed-fds-fast-success.sh|i-ignore-term-timeout.sh' \
        >"$log_path" 2>&1 &
    runner_pid=$!

    # Wait until the timeout candidate has started and the passthrough output
    # appears. The runner prints the chunk before sleeping in the debug delay,
    # which leaves the stop signal pending until shutdown polling after:
    # - h has already closed its output pipe while its process keeps running,
    # - i has already crossed its timeout deadline.
    wait_for_path "$state_dir/i.started"
    # The mixed-state assertions depend on interrupting the runner before it
    # finalizes the already-completed children, so use a shorter poll interval
    # than the generic helper default to reduce scheduling drift.
    wait_for_file_contains "$log_path" "mixed state marker" 0.01
    kill "-$signal_name" "$runner_pid"

    set +e
    wait "$runner_pid"
    status=$?
    set -e

    [ "$status" -eq "$expected_status" ] ||
        fail "unexpected mixed-state runner exit status for $signal_name: $status"
}

require_runner_signal_shutdown_env

RUNNER_SIGNAL_TEST_DIR=$IMGNEKO_TEST_OUTPUT_DIR/runner-signal-mixed-state-tests
MIXED_INTERRUPT_OUTPUT_DIR=$IMGNEKO_TEST_OUTPUT_DIR/runner-mixed-interrupt-output
MIXED_TERMINATE_OUTPUT_DIR=$IMGNEKO_TEST_OUTPUT_DIR/runner-mixed-terminate-output
MIXED_INTERRUPT_LOG=$IMGNEKO_TEST_OUTPUT_DIR/runner-mixed-interrupt.log
MIXED_TERMINATE_LOG=$IMGNEKO_TEST_OUTPUT_DIR/runner-mixed-terminate.log
MIXED_INTERRUPT_STATE_DIR=$IMGNEKO_TEST_OUTPUT_DIR/runner-mixed-interrupt-state
MIXED_TERMINATE_STATE_DIR=$IMGNEKO_TEST_OUTPUT_DIR/runner-mixed-terminate-state

mkdir -p "$RUNNER_SIGNAL_TEST_DIR"

# Emits one passthrough chunk and exits immediately. The pending-signal tests
# use it to keep one running-test entry in the "reaped but pipe not finalized"
# state until shutdown processing.
cat >"$RUNNER_SIGNAL_TEST_DIR/g-output-then-exit.sh" <<'EOF'
#!/bin/sh
printf '%s\n' 'mixed state marker'
exit 0
EOF
chmod +x "$RUNNER_SIGNAL_TEST_DIR/g-output-then-exit.sh"

# Closes stdout/stderr while keeping the process alive so the runner observes a
# child with no remaining output pipe that still needs shutdown cleanup.
cat >"$RUNNER_SIGNAL_TEST_DIR/h-closed-fds-fast-success.sh" <<'EOF'
#!/bin/sh
set -eu
state_dir=${IMGNEKO_RUNNER_SIGNAL_STATE_DIR:?}
: >"$state_dir/h.started"
trap 'exit 0' TERM INT
exec 1>&- 2>&-
while :; do
    sleep 0.1
done
EOF
chmod +x "$RUNNER_SIGNAL_TEST_DIR/h-closed-fds-fast-success.sh"

# Ignores TERM so the ordinary timeout path marks it timed out before the
# pending shutdown signal is processed. That covers the shutdown branch that
# sees a child already in timeout cleanup.
cat >"$RUNNER_SIGNAL_TEST_DIR/i-ignore-term-timeout.sh" <<'EOF'
#!/bin/sh
set -eu
state_dir=${IMGNEKO_RUNNER_SIGNAL_STATE_DIR:?}
: >"$state_dir/i.started"
trap '' TERM
while :; do
    sleep 0.1
done
EOF
chmod +x "$RUNNER_SIGNAL_TEST_DIR/i-ignore-term-timeout.sh"

echo '== runner mixed pending interrupt =='
run_runner_pending_signal_mixed_state_test INT 130 "$MIXED_INTERRUPT_LOG" \
    "$MIXED_INTERRUPT_OUTPUT_DIR" "$MIXED_INTERRUPT_STATE_DIR"
cat "$MIXED_INTERRUPT_LOG"
# CHECK: == runner mixed pending interrupt ==
# CHECK: RUN: g-output-then-exit.sh
# CHECK: RUN: h-closed-fds-fast-success.sh
# CHECK: RUN: i-ignore-term-timeout.sh
# CHECK: mixed state marker
# CHECK: Summary:
# CHECK: discovered: 3
# CHECK: interrupted: 3
# CHECK: Interrupted by SIGINT.
# CHECK: Result: INTERRUPTED
# CHECK-NOT: PASS: g-output-then-exit.sh
# CHECK-NOT: PASS: h-closed-fds-fast-success.sh
# CHECK-NOT: PASS: i-ignore-term-timeout.sh
# CHECK-NOT: TIMEOUT: i-ignore-term-timeout.sh

echo '== runner mixed pending terminate =='
run_runner_pending_signal_mixed_state_test TERM 143 "$MIXED_TERMINATE_LOG" \
    "$MIXED_TERMINATE_OUTPUT_DIR" "$MIXED_TERMINATE_STATE_DIR"
cat "$MIXED_TERMINATE_LOG"
# CHECK: == runner mixed pending terminate ==
# CHECK: RUN: g-output-then-exit.sh
# CHECK: RUN: h-closed-fds-fast-success.sh
# CHECK: RUN: i-ignore-term-timeout.sh
# CHECK: mixed state marker
# CHECK: Summary:
# CHECK: discovered: 3
# CHECK: interrupted: 3
# CHECK: Terminated by SIGTERM.
# CHECK: Result: INTERRUPTED
# CHECK-NOT: PASS: g-output-then-exit.sh
# CHECK-NOT: PASS: h-closed-fds-fast-success.sh
# CHECK-NOT: PASS: i-ignore-term-timeout.sh
# CHECK-NOT: TIMEOUT: i-ignore-term-timeout.sh
