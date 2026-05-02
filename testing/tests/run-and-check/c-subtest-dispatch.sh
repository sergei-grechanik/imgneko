#!/usr/bin/env run-and-check
# SPDX-License-Identifier: MIT-0
# RUN: sh %s

# Verify the standalone C subtest dispatcher by invoking a compiled C test
# binary directly through its `main()` helper in testing/support/test_main.c.

set -eu

fail() {
    printf '%s\n' "$1" >&2
    exit 1
}

MARKERS_BIN=$IMGNEKO_BUILD_DIR/obj/test-bin/runner/markers.c.bin

[ -x "$MARKERS_BIN" ] || fail "missing compiled C test binary: $MARKERS_BIN"

echo '== list =='
"$MARKERS_BIN" --list
# CHECK: == list ==
# CHECK-NEXT: marked_xfail XFAIL
# CHECK-NEXT: marked_disabled DISABLED

echo '== default =='
set +e
"$MARKERS_BIN" 2>&1
status=$?
set -e
printf 'status=%d\n' "$status"
# CHECK: == default ==
# CHECK-NEXT: marked_xfail: intentional expected failure
# CHECK-NEXT: marked_disabled: disabled subtest ran unexpectedly
# CHECK-NEXT: status=1

echo '== all =='
set +e
"$MARKERS_BIN" --all 2>&1
status=$?
set -e
printf 'status=%d\n' "$status"
# CHECK: == all ==
# CHECK-NEXT: marked_xfail: intentional expected failure
# CHECK-NEXT: marked_disabled: disabled subtest ran unexpectedly
# CHECK-NEXT: status=1

echo '== named =='
set +e
"$MARKERS_BIN" marked_disabled marked_xfail 2>&1
status=$?
set -e
printf 'status=%d\n' "$status"
# CHECK: == named ==
# CHECK-NEXT: marked_disabled: disabled subtest ran unexpectedly
# CHECK-NEXT: marked_xfail: intentional expected failure
# CHECK-NEXT: status=1

echo '== unknown =='
set +e
"$MARKERS_BIN" missing 2>&1
status=$?
set -e
printf 'status=%d\n' "$status"
# CHECK: == unknown ==
# CHECK-NEXT: unknown subtest: missing
# CHECK-NEXT: status=1
# CHECK-NOT: marked_xfail:
# CHECK-NOT: marked_disabled:
