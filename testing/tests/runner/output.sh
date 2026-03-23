#!/bin/sh

# Emit deterministic stdout/stderr so the test runner can verify per-test output
# capture, custom output roots, and failure tail reporting.

set -eu

fail() {
    printf '%s\n' "$1" >&2
    exit 1
}

test -n "${IMGNEKO_TEST_OUTPUT_DIR:-}" || fail "IMGNEKO_TEST_OUTPUT_DIR is not set"
test -n "${IMGNEKO_TEST_OUTPUT_FILE:-}" || fail "IMGNEKO_TEST_OUTPUT_FILE is not set"

case $IMGNEKO_TEST_OUTPUT_DIR in
    /*) ;;
    *) fail "IMGNEKO_TEST_OUTPUT_DIR is not absolute" ;;
esac

case $IMGNEKO_TEST_OUTPUT_FILE in
    "$IMGNEKO_TEST_OUTPUT_DIR"/*) ;;
    *) fail "IMGNEKO_TEST_OUTPUT_FILE is not under IMGNEKO_TEST_OUTPUT_DIR" ;;
esac

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
