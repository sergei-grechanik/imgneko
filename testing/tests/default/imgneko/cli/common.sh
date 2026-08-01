# SPDX-License-Identifier: MIT-0

# Shared helpers for imgneko CLI run-and-check tests.

fail() {
    echo "$1" >&2
    exit 1
}

setup_imgneko_cli() {
    IMGNEKO=$IMGNEKO_BUILD_DIR/bin/imgneko
    [ -x "$IMGNEKO" ] || fail "missing imgneko binary: $IMGNEKO"
}

# Run a command that is expected to fail, while preserving its output for CHECK.
check_exit_code() {
    expected_status=$1
    shift

    set +e
    "$@"
    status=$?
    set -e

    if [ "$status" -ne "$expected_status" ]; then
        echo "expected exit code $expected_status, got $status" >&2
        return 1
    fi
}
