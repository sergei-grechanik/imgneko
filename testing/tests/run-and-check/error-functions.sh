#!/usr/bin/env run-and-check
# RUN: sh %s

# Verify the shared fatal helpers emit the expected stderr text and exit status.

set -eu

fail() {
    printf '%s\n' "$1" >&2
    exit 1
}

ERROR_BIN=$IMGNEKO_BUILD_DIR/obj/test-bin/runner/error.c.bin

[ -x "$ERROR_BIN" ] || fail "missing compiled error helper binary: $ERROR_BIN"

echo '== die =='
set +e
"$ERROR_BIN" die 2>&1
status=$?
set -e
printf 'status=%d\n' "$status"
# CHECK: == die ==
# CHECK-NEXT: error: plain failure
# CHECK-NEXT: status=1

echo '== die placeholder =='
set +e
"$ERROR_BIN" die_placeholder 2>&1
status=$?
set -e
printf 'status=%d\n' "$status"
# CHECK: == die placeholder ==
# CHECK-NEXT: error: placeholder failure: No such file or directory
# CHECK-NEXT: status=1

echo '== die errno =='
set +e
"$ERROR_BIN" die_errno 2>&1
status=$?
set -e
printf 'status=%d\n' "$status"
# CHECK: == die errno ==
# CHECK-NEXT: error: errno failure: No such file or directory
# CHECK-NEXT: status=1

echo '== require pass =='
"$ERROR_BIN" require_pass
# CHECK: == require pass ==
# CHECK-NEXT: require passed

echo '== require fail =='
set +e
"$ERROR_BIN" require_fail 2>&1
status=$?
set -e
printf 'status=%d\n' "$status"
# CHECK: == require fail ==
# CHECK-NEXT: error: require failure: Permission denied
# CHECK-NEXT: status=1
