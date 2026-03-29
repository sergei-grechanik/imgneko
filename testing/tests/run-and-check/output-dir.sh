#!/usr/bin/env run-and-check
# RUN: sh %s

# Verify that the RUN command executes from the per-test output directory and
# sees IMGNEKO_TEST_OUTPUT_DIR as an absolute path.

test -n "${IMGNEKO_TEST_OUTPUT_DIR:-}"
case $IMGNEKO_TEST_OUTPUT_DIR in
    /*) ;;
    *) exit 1 ;;
esac
test -d "$IMGNEKO_TEST_OUTPUT_DIR"
test "$(pwd)" = "$IMGNEKO_TEST_OUTPUT_DIR"

printf '%s\n' "$IMGNEKO_TEST_OUTPUT_DIR"
# CHECK: /
