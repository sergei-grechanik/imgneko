#!/bin/sh

# Emit deterministic stdout/stderr so the test runner can verify per-test output
# capture, custom output roots, and failure tail reporting.

set -eu

fail() {
    printf '%s\n' "$1" >&2
    exit 1
}

if [ -z "${IMGNEKO_TEST_OUTPUT_DIR:-}" ] ||
   [ ! -d "$IMGNEKO_TEST_OUTPUT_DIR" ]; then
    fail "IMGNEKO_TEST_OUTPUT_DIR is not set to an existing directory"
fi

case $IMGNEKO_TEST_OUTPUT_DIR in
    /*) ;;
    *) fail "IMGNEKO_TEST_OUTPUT_DIR is not absolute" ;;
esac

test "$(pwd)" = "$IMGNEKO_TEST_OUTPUT_DIR" ||
    fail "current directory does not match IMGNEKO_TEST_OUTPUT_DIR"
test -f output || fail "output file is missing"

if [ "${IMGNEKO_TEST_PASSTHROUGH_DELAY:-0}" = "1" ]; then
    # Keep the process alive after the first line so nested runner tests can
    # verify that passthrough reaches the user before the test completes.
    printf '%s\n' 'output script stdout marker'
    sleep 4
    printf '%s\n' 'output script stderr marker' >&2
    exit 0
fi

if [ "${IMGNEKO_TEST_SHOULD_FAIL:-0}" = "1" ]; then
    i=1
    while [ "$i" -le 25 ]; do
        printf 'failure line %d\n' "$i"
        i=$((i + 1))
    done
    exit 1
fi

printf '%s\n' 'output script stdout marker'
printf '%s\n' 'output script stderr marker' >&2
