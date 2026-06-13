#!/usr/bin/env run-and-check
# SPDX-License-Identifier: MIT-0
# RUN: run-in-pty -- sh %s

# Exercise imgneko placeholder CLI error handling.

set -eu

. "$IMGNEKO_ROOT_DIR/testing/tests/imgneko/cli/common.sh"
setup_imgneko_cli

echo '== missing option =='
# Exercise each required-option branch before --cols, because the command
# validates required arguments with short-circuiting checks.
check_exit_code 2 "$IMGNEKO" placeholder --rows 1 --cols 1
# CHECK:      {{^}}== missing option =={{$}}
# CHECK-NEXT: {{^}}error: missing required option: --id{{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 1 --cols 1
# CHECK-NEXT: {{^}}error: missing required option: --rows{{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 1 --rows 1
# CHECK-NEXT: {{^}}error: missing required option: --cols{{$}}

echo '== invalid placement id =='
check_exit_code 2 "$IMGNEKO" placeholder --id 1 --placement-id 0x1000000 \
    --rows 1 --cols 1
# CHECK-NEXT: {{^}}== invalid placement id =={{$}}
# CHECK-NEXT: {{^}}error: invalid value for --placement-id: 0x1000000 (expected a value up to 16777215){{$}}

echo '== invalid rectangle =='
# Cover placeholder validation failures that occur after CLI option parsing.
check_exit_code 2 "$IMGNEKO" placeholder --id 1 --rows 298 --cols 1
# CHECK-NEXT: {{^}}== invalid rectangle =={{$}}
# CHECK-NEXT: {{^}}error: invalid placeholder: unrepresentable row{{$}}

echo '== invalid numeric options =='
# Cover the custom unsigned-integer parser errors exposed by the placeholder
# command.
check_exit_code 2 "$IMGNEKO" placeholder --id= --rows 1 --cols 1
# CHECK-NEXT: {{^}}== invalid numeric options =={{$}}
# CHECK-NEXT: {{^}}error: invalid value for --id:  (expected an unsigned decimal or hexadecimal integer){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id -1 --rows 1 --cols 1
# CHECK-NEXT: {{^}}error: invalid value for --id: -1 (expected an unsigned decimal or hexadecimal integer){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id /1 --rows 1 --cols 1
# CHECK-NEXT: {{^}}error: invalid value for --id: /1 (expected an unsigned decimal or hexadecimal integer){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id x --rows 1 --cols 1
# CHECK-NEXT: {{^}}error: invalid value for --id: x (expected an unsigned decimal or hexadecimal integer){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 0x --rows 1 --cols 1
# CHECK-NEXT: {{^}}error: invalid value for --id: 0x (expected an unsigned decimal or hexadecimal integer){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 4294967296 --rows 1 \
    --cols 1
# CHECK-NEXT: {{^}}error: invalid value for --id: 4294967296 (expected a 32-bit unsigned integer){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 0x100000000 --rows 1 \
    --cols 1
# CHECK-NEXT: {{^}}error: invalid value for --id: 0x100000000 (expected a 32-bit unsigned integer){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 0 --rows 1 --cols 1
# CHECK-NEXT: {{^}}error: invalid value for --id: 0 (expected a positive integer){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 1 --rows x --cols 1
# CHECK-NEXT: {{^}}error: invalid value for --rows: x (expected a base-10 unsigned integer){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 1 --rows -1 --cols 1
# CHECK-NEXT: {{^}}error: invalid value for --rows: -1 (expected a base-10 unsigned integer){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 1 --rows 4294967296 --cols 1
# CHECK-NEXT: {{^}}error: invalid value for --rows: 4294967296 (expected a 32-bit unsigned integer){{$}}

echo '== broken stdout =='
# Close the only FIFO reader before the child writes so imgneko reports a write
# failure instead of silently succeeding.
broken_stdout_fifo=$IMGNEKO_TEST_OUTPUT_DIR/imgneko-broken-stdout-fifo
mkfifo "$broken_stdout_fifo"
set +e
(
    trap '' PIPE
    "$IMGNEKO" placeholder --id 1 --rows 1 --cols 1 \
        >"$broken_stdout_fifo"
    printf 'status=%d\n' "$?"
) &
broken_stdout_pid=$!
exec 9<"$broken_stdout_fifo"
exec 9<&-
wait "$broken_stdout_pid"
set -e
# CHECK-NEXT: {{^}}== broken stdout =={{$}}
# CHECK-NEXT: {{^}}error: failed to write placeholder: write failed{{$}}
# CHECK-NEXT: {{^}}status=1{{$}}
