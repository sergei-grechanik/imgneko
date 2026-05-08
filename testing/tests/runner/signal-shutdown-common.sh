# SPDX-License-Identifier: MIT-0

# Shared helpers for the signal-shutdown run-and-check tests. This file is
# sourced by executable tests and intentionally is not executable itself.

fail() {
    printf '%s\n' "$1" >&2
    exit 1
}

# Verify the outer test-runner environment and expose the nested runner path.
require_runner_signal_shutdown_env() {
    if [ -z "${IMGNEKO_TEST_OUTPUT_DIR:-}" ] ||
       [ ! -d "$IMGNEKO_TEST_OUTPUT_DIR" ]; then
        fail "IMGNEKO_TEST_OUTPUT_DIR is not set to an existing directory"
    fi

    if [ -z "${IMGNEKO_BUILD_DIR:-}" ]; then
        fail "IMGNEKO_BUILD_DIR is not set"
    fi

    RUNNER=$IMGNEKO_BUILD_DIR/bin/test-runner
    [ -x "$RUNNER" ] || fail "missing test-runner binary: $RUNNER"
}

# Wait for a marker path written by a nested test child.
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

# Wait for nested runner output to include the line that opens the race window.
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

# Verify that a child process terminated after the runner propagated shutdown.
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
