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

echo '== conflicting dimensions =='
# --place supplies both rows and columns, so mixing it with either explicit
# dimension option would make the requested size ambiguous.
check_exit_code 2 "$IMGNEKO" placeholder --id 1 --place 1x1 --rows 1
# CHECK-NEXT: {{^}}== conflicting dimensions =={{$}}
# CHECK-NEXT: {{^}}error: --place cannot be used with --rows or --cols{{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 1 --place 1x1 --cols 1
# CHECK-NEXT: {{^}}error: --place cannot be used with --rows or --cols{{$}}

echo '== invalid placement id =='
check_exit_code 2 "$IMGNEKO" placeholder --id 1 --placement-id 0x1000000 \
    --rows 1 --cols 1
# CHECK-NEXT: {{^}}== invalid placement id =={{$}}
# CHECK-NEXT: {{^}}error: invalid value for --placement-id: '0x1000000' (expected a value up to 16777215){{$}}

echo '== invalid numeric options =='
# Cover the custom unsigned-integer parser errors exposed by the placeholder
# command.
check_exit_code 2 "$IMGNEKO" placeholder --id= --rows 1 --cols 1
# CHECK-NEXT: {{^}}== invalid numeric options =={{$}}
# CHECK-NEXT: {{^}}error: invalid value for --id: '' (expected an unsigned decimal or hexadecimal integer){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id -1 --rows 1 --cols 1
# CHECK-NEXT: {{^}}error: invalid value for --id: '-1' (expected an unsigned decimal or hexadecimal integer){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id /1 --rows 1 --cols 1
# CHECK-NEXT: {{^}}error: invalid value for --id: '/1' (expected an unsigned decimal or hexadecimal integer){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id x --rows 1 --cols 1
# CHECK-NEXT: {{^}}error: invalid value for --id: 'x' (expected an unsigned decimal or hexadecimal integer){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 0x --rows 1 --cols 1
# CHECK-NEXT: {{^}}error: invalid value for --id: '0x' (expected an unsigned decimal or hexadecimal integer){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 4294967296 --rows 1 \
    --cols 1
# CHECK-NEXT: {{^}}error: invalid value for --id: '4294967296' (expected a 32-bit unsigned integer){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 0x100000000 --rows 1 \
    --cols 1
# CHECK-NEXT: {{^}}error: invalid value for --id: '0x100000000' (expected a 32-bit unsigned integer){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 0 --rows 1 --cols 1
# CHECK-NEXT: {{^}}error: invalid value for --id: '0' (expected a positive integer){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 1 --rows x --cols 1
# CHECK-NEXT: {{^}}error: invalid value for --rows: 'x' (expected a base-10 unsigned integer){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 1 --rows -1 --cols 1
# CHECK-NEXT: {{^}}error: invalid value for --rows: '-1' (expected a base-10 unsigned integer){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 1 --rows 4294967296 --cols 1
# CHECK-NEXT: {{^}}error: invalid value for --rows: '4294967296' (expected a 32-bit unsigned integer){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 1 --rows 1 --cols 1 \
    --diacritics ""
# CHECK-NEXT: {{^}}error: invalid value for --diacritics: '' (expected one of minimal, default, or complete){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 1 --rows 1 --cols 1 \
    --diacritics minimum
# CHECK-NEXT: {{^}}error: invalid value for --diacritics: 'minimum' (expected one of minimal, default, or complete){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 1 --rows 1 --cols 1 \
    --diacritics computed
# CHECK-NEXT: {{^}}error: invalid value for --diacritics: 'computed' (expected one of minimal, default, or complete){{$}}

echo '== invalid background options =='
check_exit_code 2 "$IMGNEKO" placeholder --id 1 --rows 1 --cols 1 --bg ""
# CHECK-NEXT: {{^}}== invalid background options =={{$}}
# CHECK-NEXT: {{^}}error: invalid value for --bg: '' (expected default, INDEX, #rrggbb, rgb(r, g, b), checkerboard(bg, bg), ch(bg, bg), hstripes(bg, bg), hs(bg, bg), vstripes(bg, bg), or vs(bg, bg)){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 1 --rows 1 --cols 1 --bg 256
# CHECK-NEXT: {{^}}error: invalid value for --bg: '256' (background color index must be a decimal integer from 0 to 255){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 1 --rows 1 --cols 1 \
    --bg 999999999999999999999
# CHECK-NEXT: {{^}}error: invalid value for --bg: '999999999999999999999' (background color index must be a decimal integer from 0 to 255){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 1 --rows 1 --cols 1 --bg 1x
# CHECK-NEXT: {{^}}error: invalid value for --bg: '1x' (unexpected text 'x' after expression at '1<here>x'){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 1 --rows 1 --cols 1 --bg '!'
# CHECK-NEXT: {{^}}error: invalid value for --bg: '!' (expected an expression, got '!' at '<here>!'){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 1 --rows 1 --cols 1 \
    --bg abcdefg
# CHECK-NEXT: {{^}}error: invalid value for --bg: 'abcdefg' (unexpected identifier 'abcdefg'; expected default, INDEX, #rrggbb, rgb(r, g, b), checkerboard(bg, bg), ch(bg, bg), hstripes(bg, bg), hs(bg, bg), vstripes(bg, bg), or vs(bg, bg)){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 1 --rows 1 --cols 1 \
    --bg 0x1
# CHECK-NEXT: {{^}}error: invalid value for --bg: '0x1' (unexpected hexadecimal integer '0x1'; expected default, INDEX, #rrggbb, rgb(r, g, b), checkerboard(bg, bg), ch(bg, bg), hstripes(bg, bg), hs(bg, bg), vstripes(bg, bg), or vs(bg, bg)){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 1 --rows 1 --cols 1 \
    --bg '"x"'
# CHECK-NEXT: {{^}}error: invalid value for --bg: '"x"' (unexpected string literal '"x"'; expected default, INDEX, #rrggbb, rgb(r, g, b), checkerboard(bg, bg), ch(bg, bg), hstripes(bg, bg), hs(bg, bg), vstripes(bg, bg), or vs(bg, bg)){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 1 --rows 1 --cols 1 \
    --bg '#12345'
# CHECK-NEXT: {{^}}error: invalid value for --bg: '#12345' (expected a hexadecimal color digit, got end of expression at '#12345<here>'){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 1 --rows 1 --cols 1 \
    --bg '#g00000'
# CHECK-NEXT: {{^}}error: invalid value for --bg: '#g00000' (expected a hexadecimal color digit, got 'g' at '#<here>g00000'){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 1 --rows 1 --cols 1 \
    --bg '#0g0000'
# CHECK-NEXT: {{^}}error: invalid value for --bg: '#0g0000' (expected a hexadecimal color digit, got 'g' at '#0<here>g0000'){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 1 --rows 1 --cols 1 \
    --bg 'rgbx(1,2,3)'
# CHECK-NEXT: {{^}}error: invalid value for --bg: 'rgbx(1,2,3)' (expected default, INDEX, #rrggbb, rgb(r, g, b), checkerboard(bg, bg), ch(bg, bg), hstripes(bg, bg), hs(bg, bg), vstripes(bg, bg), or vs(bg, bg)){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 1 --rows 1 --cols 1 \
    --bg 'rgb(1,2,3x'
# CHECK-NEXT: {{^}}error: invalid value for --bg: 'rgb(1,2,3x' (expected ',' or ')' in function argument list, got 'x' at 'rgb(1,2,3<here>x'){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 1 --rows 1 --cols 1 \
    --bg 'rgb(1,2,3'
# CHECK-NEXT: {{^}}error: invalid value for --bg: 'rgb(1,2,3' (expected ',' or ')' in function argument list, got end of expression at 'rgb(1,2,3<here>'){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 1 --rows 1 --cols 1 \
    --bg 'rgb(1 2,3)'
# CHECK-NEXT: {{^}}error: invalid value for --bg: 'rgb(1 2,3)' (expected ',' or ')' in function argument list, got '2' at 'rgb(1 <here>2,3)'){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 1 --rows 1 --cols 1 \
    --bg 'rgb(1 2,3)abcdefghijklmnop'
# CHECK-NEXT: {{^}}error: invalid value for --bg: 'rgb(1 2,3)abcdefghijklmnop' (expected ',' or ')' in function argument list, got '2' at 'rgb(1 <here>2,3)abcdef...'){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 1 --rows 1 --cols 1 \
    --bg 'rgb(1,2)'
# CHECK-NEXT: {{^}}error: invalid value for --bg: 'rgb(1,2)' (rgb() expects 3 arguments, got 2){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 1 --rows 1 --cols 1 \
    --bg 'rgb(1,,3)'
# CHECK-NEXT: {{^}}error: invalid value for --bg: 'rgb(1,,3)' (expected an expression, got ',' at 'rgb(1,<here>,3)'){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 1 --rows 1 --cols 1 \
    --bg 'rgb(1,2,)'
# CHECK-NEXT: {{^}}error: invalid value for --bg: 'rgb(1,2,)' (expected an expression, got ')' at 'rgb(1,2,<here>)'){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 1 --rows 1 --cols 1 \
    --bg 'rgb(1,2,3,4)'
# CHECK-NEXT: {{^}}error: invalid value for --bg: 'rgb(1,2,3,4)' (rgb() expects 3 arguments, got 4){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 1 --rows 1 --cols 1 \
    --bg 'rgb(x,2,3)'
# CHECK-NEXT: {{^}}error: invalid value for --bg: 'rgb(x,2,3)' (rgb() argument 1 must be a decimal integer from 0 to 255){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 1 --rows 1 --cols 1 \
    --bg 'rgb(999999999999999999999,2,3)'
# CHECK-NEXT: {{^}}error: invalid value for --bg: 'rgb(999999999999999999999,2,3)' (rgb() argument 1 must be a decimal integer from 0 to 255){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 1 --rows 1 --cols 1 \
    --bg 'rgb(1,x,3)'
# CHECK-NEXT: {{^}}error: invalid value for --bg: 'rgb(1,x,3)' (rgb() argument 2 must be a decimal integer from 0 to 255){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 1 --rows 1 --cols 1 \
    --bg 'rgb(1,2,x)'
# CHECK-NEXT: {{^}}error: invalid value for --bg: 'rgb(1,2,x)' (rgb() argument 3 must be a decimal integer from 0 to 255){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 1 --rows 1 --cols 1 \
    --bg 'checkerboard(1)'
# CHECK-NEXT: {{^}}error: invalid value for --bg: 'checkerboard(1)' (checkerboard() expects 2 arguments, got 1){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 1 --rows 1 --cols 1 \
    --bg 'checkerboard(1,'
# CHECK-NEXT: {{^}}error: invalid value for --bg: 'checkerboard(1,' (expected an expression, got end of expression at '...erboard(1,<here>'){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 1 --rows 1 --cols 1 \
    --bg 'checkerboard(1,2'
# CHECK-NEXT: {{^}}error: invalid value for --bg: 'checkerboard(1,2' (expected ',' or ')' in function argument list, got end of expression at '...rboard(1,2<here>'){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 1 --rows 1 --cols 1 \
    --bg 'checkerboard(1,,2)'
# CHECK-NEXT: {{^}}error: invalid value for --bg: 'checkerboard(1,,2)' (expected an expression, got ',' at '...erboard(1,<here>,2)'){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 1 --rows 1 --cols 1 \
    --bg 'checkerboard(rgb(1,2,3), 4))'
# CHECK-NEXT: {{^}}error: invalid value for --bg: 'checkerboard(rgb(1,2,3), 4))' (unexpected text ')' after expression at '...1,2,3), 4)<here>)'){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 1 --rows 1 --cols 1 \
    --bg 'checkerboard(rgb(1,2,3, 4)'
# CHECK-NEXT: {{^}}error: invalid value for --bg: 'checkerboard(rgb(1,2,3, 4)' (expected ',' or ')' in function argument list, got end of expression at '...(1,2,3, 4)<here>'){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 1 --rows 1 --cols 1 \
    --bg 'checkerboard(x,1)'
# CHECK-NEXT: {{^}}error: invalid value for --bg: 'checkerboard(x,1)' (unexpected identifier 'x'; expected default, INDEX, #rrggbb, rgb(r, g, b), checkerboard(bg, bg), ch(bg, bg), hstripes(bg, bg), hs(bg, bg), vstripes(bg, bg), or vs(bg, bg)){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 1 --rows 1 --cols 1 \
    --bg 'checkerboard(1,x)'
# CHECK-NEXT: {{^}}error: invalid value for --bg: 'checkerboard(1,x)' (unexpected identifier 'x'; expected default, INDEX, #rrggbb, rgb(r, g, b), checkerboard(bg, bg), ch(bg, bg), hstripes(bg, bg), hs(bg, bg), vstripes(bg, bg), or vs(bg, bg)){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 1 --rows 1 --cols 1 \
    --bg 'checkerboard(256,1)'
# CHECK-NEXT: {{^}}error: invalid value for --bg: 'checkerboard(256,1)' (background color index must be a decimal integer from 0 to 255){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 1 --place ""
# CHECK-NEXT: {{^}}error: invalid value for --place: '' (expected CxR with positive base-10 unsigned integers){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 1 --place 1
# CHECK-NEXT: {{^}}error: invalid value for --place: '1' (expected CxR with positive base-10 unsigned integers){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 1 --place 1xx1
# CHECK-NEXT: {{^}}error: invalid value for --place: '1xx1' (expected exactly one x separator in CxR value){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 1 --place x
# CHECK-NEXT: {{^}}error: invalid value for --place: 'x' (expected CxR with positive base-10 unsigned integers){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 1 --place 1x0
# CHECK-NEXT: {{^}}error: invalid value for --place: '1x0' (expected CxR with positive base-10 unsigned integers){{$}}

check_exit_code 2 "$IMGNEKO" placeholder --id 1 --place 4294967296x1
# CHECK-NEXT: {{^}}error: invalid value for --place: '4294967296x1' (expected CxR with positive base-10 unsigned integers){{$}}

echo '== broken stdout =='
broken_stdout_fifo=$IMGNEKO_TEST_OUTPUT_DIR/imgneko-broken-stdout-fifo
broken_stdout_ready=$IMGNEKO_TEST_OUTPUT_DIR/imgneko-broken-stdout-ready
broken_stdout_status=$IMGNEKO_TEST_OUTPUT_DIR/imgneko-broken-stdout-status
mkfifo "$broken_stdout_fifo"
set +e
(
    trap '' PIPE
    (
        # Open the FIFO before waiting so the parent can close the only reader
        # before imgneko writes to stdout.
        while [ ! -f "$broken_stdout_ready" ]; do :; done
        "$IMGNEKO" placeholder --id 1 --rows 1 --cols 1
        printf '%d\n' "$?" >"$broken_stdout_status"
    ) >"$broken_stdout_fifo"
) &
broken_stdout_pid=$!
# Unblock the writer's FIFO open, close the reader, and only then let imgneko
# write the placeholder. This avoids racing a small successful FIFO write.
exec 9<"$broken_stdout_fifo"
exec 9<&-
: >"$broken_stdout_ready"
wait "$broken_stdout_pid"
set -e
printf 'status=%d\n' "$(cat "$broken_stdout_status")"
# CHECK-NEXT: {{^}}== broken stdout =={{$}}
# CHECK-NEXT: {{^}}error: failed to write placeholder: write failed{{$}}
# CHECK-NEXT: {{^}}status=1{{$}}
