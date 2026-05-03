#!/usr/bin/env run-and-check
# SPDX-License-Identifier: MIT-0
# RUN: sh %s

# Exercise the small PTY runner used by CLI tests that need a specific tty
# width without relying on external `script` or `stty` helpers.

set -eu

fail() {
    echo "$1" >&2
    exit 1
}

if [ -z "${IMGNEKO_TEST_OUTPUT_DIR:-}" ] ||
   [ ! -d "$IMGNEKO_TEST_OUTPUT_DIR" ]; then
    fail "IMGNEKO_TEST_OUTPUT_DIR is not set to an existing directory"
fi

RUN_IN_PTY=$IMGNEKO_BUILD_DIR/bin/run-in-pty

[ -x "$RUN_IN_PTY" ] || fail "missing run-in-pty binary: $RUN_IN_PTY"

echo '== help =='
"$RUN_IN_PTY" --help 2>&1
# CHECK:      {{^}}== help =={{$}}
# CHECK-NEXT: {{^}}Run a child command under a PTY with a requested window size.{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Usage: run-in-pty [options] -- COMMAND [ARG...]{{$}}
# CHECK:      {{^}}  --rows ROWS               PTY row count. (default: 24){{$}}
# CHECK:      {{^}}  --cols COLS               PTY column count. (default: 80){{$}}
# CHECK:      {{^}}  --opost / --no-opost      Enable PTY output post-processing. (default: false){{$}}
# Only match the first line for this option. The default is derived from
# PIPE_BUF, which is smaller on macOS and changes the wrapped help text.
# CHECK:      {{^}}  --write-chunk-size BYTES  Maximum stdout forwarding write size.

echo '== default size =='
"$RUN_IN_PTY" --no-opost -- sh -c 'stty size' 2>&1
# CHECK:      {{^}}== default size =={{$}}
# CHECK-NEXT: {{^}}24 80{{$}}

echo '== default opost off =='
# BSD od on macOS and GNU od pad hex output differently. Drop blank padding
# rows and let the check match either spacing style.
"$RUN_IN_PTY" -- sh -c 'printf "x\ny\n"' | od -An -tx1 | sed '/^[[:space:]]*$/d'
# CHECK-NEXT: {{^}}== default opost off =={{$}}
# CHECK-NEXT: {{^ *78 +0a +79 +0a *$}}

echo '== explicit opost on =='
"$RUN_IN_PTY" --opost -- sh -c 'printf "x\ny\n"' | od -An -tx1 | sed '/^[[:space:]]*$/d'
# CHECK-NEXT: {{^}}== explicit opost on =={{$}}
# CHECK-NEXT: {{^ *78 +0d +0a +79 +0d +0a *$}}

echo '== stty size =='
"$RUN_IN_PTY" --rows 13 --cols 72 -- sh -c 'stty size' 2>&1
# CHECK-NEXT: {{^}}== stty size =={{$}}
# CHECK-NEXT: {{^}}13 72{{$}}

echo '== unknown helper option =='
set +e
"$RUN_IN_PTY" --rows 5 --cols 10 --bogus 2>&1
status=$?
set -e
printf 'status=%d\n' "$status"
# CHECK-NEXT: {{^}}== unknown helper option =={{$}}
# CHECK-NEXT: {{^}}error: unknown option: --bogus{{$}}
# CHECK-NEXT: {{^}}status=2{{$}}

echo '== missing delimiter =='
set +e
"$RUN_IN_PTY" sh -c 'printf "oops\n"' 2>&1
status=$?
set -e
printf 'status=%d\n' "$status"
# CHECK-NEXT: {{^}}== missing delimiter =={{$}}
# CHECK-NEXT: {{^}}error: positional argument requires the -- delimiter here: sh{{$}}
# CHECK-NEXT: {{^}}status=2{{$}}

echo '== missing command =='
set +e
"$RUN_IN_PTY" --rows 5 --cols 10 -- 2>&1
status=$?
set -e
printf 'status=%d\n' "$status"
# CHECK-NEXT: {{^}}== missing command =={{$}}
# CHECK-NEXT: {{^}}error: missing required argument: COMMAND{{$}}
# CHECK-NEXT: {{^}}status=2{{$}}

echo '== forwarded output =='
"$RUN_IN_PTY" --rows 5 --cols 10 -- sh -c 'printf "alpha\nbeta\n"' 2>&1
# CHECK-NEXT: {{^}}== forwarded output =={{$}}
# CHECK-NEXT: {{^}}alpha{{$}}
# CHECK-NEXT: {{^}}beta{{$}}

echo '== large output =='
# wc pads counts differently across BSD and GNU implementations, so the
# expectation accepts the portable count with optional leading spaces.
"$RUN_IN_PTY" -- sh -c 'i=0; while [ "$i" -lt 100000 ]; do printf x; i=$((i + 1)); done' | wc -c
# CHECK-NEXT: {{^}}== large output =={{$}}
# CHECK-NEXT: {{^ *100000$}}

echo '== large output, small write chunk =='
"$RUN_IN_PTY" --write-chunk-size 16 -- sh -c 'i=0; while [ "$i" -lt 100000 ]; do printf x; i=$((i + 1)); done' | wc -c
# CHECK-NEXT: {{^}}== large output, small write chunk =={{$}}
# CHECK-NEXT: {{^ *100000$}}

echo '== broken stdout =='
broken_stdout_fifo=$IMGNEKO_TEST_OUTPUT_DIR/broken-stdout-fifo
broken_stdout_ready=$IMGNEKO_TEST_OUTPUT_DIR/broken-stdout-ready
mkfifo "$broken_stdout_fifo"
set +e
(
    # Ignore SIGPIPE so run-in-pty can report the write error instead of being
    # terminated by the signal.
    trap '' PIPE
    BROKEN_STDOUT_READY=$broken_stdout_ready \
    "$RUN_IN_PTY" --write-chunk-size 1 -- \
        sh -c 'while [ ! -f "$BROKEN_STDOUT_READY" ]; do :; done; printf x' \
        >"$broken_stdout_fifo" 2>"$IMGNEKO_TEST_OUTPUT_DIR/err"
    printf '%d\n' "$?" >"$IMGNEKO_TEST_OUTPUT_DIR/status"
) &
broken_stdout_pid=$!
# Unblock the writer's FIFO open, close the reader, and only then let the PTY
# child produce output. That makes the stdout write fail deterministically.
exec 9<"$broken_stdout_fifo"
exec 9<&-
: >"$broken_stdout_ready"
wait "$broken_stdout_pid"
set -e
cat "$IMGNEKO_TEST_OUTPUT_DIR/err"
printf 'status=%d\n' "$(cat "$IMGNEKO_TEST_OUTPUT_DIR/status")"
# CHECK-NEXT: {{^}}== broken stdout =={{$}}
# CHECK-NEXT: {{^}}error: failed to write PTY output to stdout: {{.*}}{{$}}
# CHECK-NEXT: {{^}}status=1{{$}}

echo '== exit status =='
set +e
"$RUN_IN_PTY" --rows 5 --cols 10 -- sh -c 'exit 7' 2>&1
status=$?
set -e
printf 'status=%d\n' "$status"
# CHECK-NEXT: {{^}}== exit status =={{$}}
# CHECK-NEXT: {{^}}status=7{{$}}

echo '== signaled exit =='
set +e
"$RUN_IN_PTY" -- sh -c 'kill -TERM $$' 2>&1
status=$?
set -e
printf 'status=%d\n' "$status"
# CHECK-NEXT: {{^}}== signaled exit =={{$}}
# CHECK-NEXT: {{^}}status=143{{$}}
