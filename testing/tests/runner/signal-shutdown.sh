#!/usr/bin/env run-and-check
# SPDX-License-Identifier: MIT-0
# RUN: sh %s

# Verify that when test-runner itself is interrupted or terminated, it stops
# scheduling new tests, gracefully winds down running children, and prints an
# interrupted partial summary.

set -eu

fail() {
    echo "$1" >&2
    exit 1
}

if [ -z "${IMGNEKO_TEST_OUTPUT_DIR:-}" ] ||
   [ ! -d "$IMGNEKO_TEST_OUTPUT_DIR" ]; then
    fail "IMGNEKO_TEST_OUTPUT_DIR is not set to an existing directory"
fi

wait_for_path() {
    path=$1
    i=0

    while [ "$i" -lt 200 ]; do
        if [ -e "$path" ]; then
            return 0
        fi
        sleep 0.05
        i=$((i + 1))
    done

    fail "timed out waiting for path: $path"
}

wait_for_file_contains() {
    path=$1
    needle=$2
    poll_interval=${3:-0.05}
    i=0

    while [ "$i" -lt 200 ]; do
        if [ -f "$path" ] && grep -F -- "$needle" "$path" >/dev/null 2>&1; then
            return 0
        fi
        sleep "$poll_interval"
        i=$((i + 1))
    done

    fail "timed out waiting for '$needle' in $path"
}

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

    fail "expected process to be gone: $pid"
}

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

RUNNER=$IMGNEKO_BUILD_DIR/bin/test-runner
[ -x "$RUNNER" ] || fail "missing test-runner binary: $RUNNER"

RUNNER_SIGNAL_TEST_DIR=$IMGNEKO_TEST_OUTPUT_DIR/runner-signal-tests
INTERRUPT_OUTPUT_DIR=$IMGNEKO_TEST_OUTPUT_DIR/runner-interrupt-output
TERMINATE_OUTPUT_DIR=$IMGNEKO_TEST_OUTPUT_DIR/runner-terminate-output
INTERRUPT_LOG=$IMGNEKO_TEST_OUTPUT_DIR/runner-interrupt.log
TERMINATE_LOG=$IMGNEKO_TEST_OUTPUT_DIR/runner-terminate.log
INTERRUPT_STATE_DIR=$IMGNEKO_TEST_OUTPUT_DIR/runner-interrupt-state
TERMINATE_STATE_DIR=$IMGNEKO_TEST_OUTPUT_DIR/runner-terminate-state
PENDING_INTERRUPT_OUTPUT_DIR=$IMGNEKO_TEST_OUTPUT_DIR/runner-pending-interrupt-output
PENDING_TERMINATE_OUTPUT_DIR=$IMGNEKO_TEST_OUTPUT_DIR/runner-pending-terminate-output
PENDING_INTERRUPT_LOG=$IMGNEKO_TEST_OUTPUT_DIR/runner-pending-interrupt.log
PENDING_TERMINATE_LOG=$IMGNEKO_TEST_OUTPUT_DIR/runner-pending-terminate.log
PENDING_INTERRUPT_STATE_DIR=$IMGNEKO_TEST_OUTPUT_DIR/runner-pending-interrupt-state
PENDING_TERMINATE_STATE_DIR=$IMGNEKO_TEST_OUTPUT_DIR/runner-pending-terminate-state
MIXED_INTERRUPT_OUTPUT_DIR=$IMGNEKO_TEST_OUTPUT_DIR/runner-mixed-interrupt-output
MIXED_TERMINATE_OUTPUT_DIR=$IMGNEKO_TEST_OUTPUT_DIR/runner-mixed-terminate-output
MIXED_INTERRUPT_LOG=$IMGNEKO_TEST_OUTPUT_DIR/runner-mixed-interrupt.log
MIXED_TERMINATE_LOG=$IMGNEKO_TEST_OUTPUT_DIR/runner-mixed-terminate.log
MIXED_INTERRUPT_STATE_DIR=$IMGNEKO_TEST_OUTPUT_DIR/runner-mixed-interrupt-state
MIXED_TERMINATE_STATE_DIR=$IMGNEKO_TEST_OUTPUT_DIR/runner-mixed-terminate-state
COMPLETED_TIMEOUT_INTERRUPT_OUTPUT_DIR=$IMGNEKO_TEST_OUTPUT_DIR/runner-completed-timeout-interrupt-output
COMPLETED_TIMEOUT_INTERRUPT_LOG=$IMGNEKO_TEST_OUTPUT_DIR/runner-completed-timeout-interrupt.log
COMPLETED_TIMEOUT_INTERRUPT_STATE_DIR=$IMGNEKO_TEST_OUTPUT_DIR/runner-completed-timeout-interrupt-state

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

# Exits promptly once the ordinary timeout path sends TERM. The shutdown tests
# use it to cover the branch that sees an already-complete timed-out child in
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
# CHECK: {{PASS: k-delay-slot\.sh|RUN: l-signal-window\.sh|TIMEOUT: j-timeout-exit-on-term\.sh}}
# CHECK: {{PASS: k-delay-slot\.sh|RUN: l-signal-window\.sh|TIMEOUT: j-timeout-exit-on-term\.sh}}
# CHECK: {{PASS: k-delay-slot\.sh|RUN: l-signal-window\.sh|TIMEOUT: j-timeout-exit-on-term\.sh}}
# CHECK: Summary:
# CHECK: discovered: 3
# CHECK: passed: 1
# CHECK: timeout: 1
# CHECK: interrupted: 1
# CHECK: Interrupted by SIGINT.
# CHECK: Result: INTERRUPTED
# CHECK-NOT: PASS: j-timeout-exit-on-term.sh
# CHECK-NOT: PASS: l-signal-window.sh

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
