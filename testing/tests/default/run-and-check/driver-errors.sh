#!/usr/bin/env run-and-check
# SPDX-License-Identifier: MIT-0
# RUN: sh %s

# Exercise nested run-and-check parse and runtime failure paths through the
# public CLI so the support code gains coverage without shell-grep harnesses.
#
# The temporary fixture files use `@@ ` for nested directives. `write_case()`
# rewrites that back to `# ` so the outer run-and-check parser treats the
# nested fixtures as plain shell heredocs instead of its own directives.

set -eu

fail() {
    echo "$1" >&2
    exit 1
}

write_case() {
    path=$1
    sed 's/^@@ /# /' >"$path"
}

RUN_AND_CHECK=$IMGNEKO_BUILD_DIR/bin/run-and-check
TMP_DIR=$(mktemp -d /tmp/imgneko-run-and-check-driver-errors.XXXXXX)
OUTPUT_DIR=$TMP_DIR/output

cleanup() {
    rm -rf "$TMP_DIR"
}

trap cleanup EXIT INT TERM HUP

[ -x "$RUN_AND_CHECK" ] || fail "missing run-and-check binary: $RUN_AND_CHECK"
mkdir -p "$OUTPUT_DIR"

write_case "$TMP_DIR/missing-run.txt" <<'EOF'
@@ CHECK: hello
EOF
echo '== missing run =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/missing-run.txt" 2>&1 || true
# CHECK: == missing run ==
# CHECK: missing-run.txt:1: error: missing RUN directive
# CHECK: missing-run.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/missing-check.txt" <<'EOF'
@@ RUN: true
EOF
echo '== missing check =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/missing-check.txt" 2>&1 || true
# CHECK: == missing check ==
# CHECK: missing-check.txt:1: error: missing CHECK directives
# CHECK: missing-check.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/multiple-run.txt" <<'EOF'
@@ RUN: true
@@ RUN: true
@@ CHECK: hello
EOF
echo '== multiple run =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/multiple-run.txt" 2>&1 || true
# CHECK: == multiple run ==
# CHECK: multiple-run.txt:2: error: multiple RUN directives are not supported
# CHECK: multiple-run.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/empty-run.txt" <<'EOF'
@@ RUN:
@@ CHECK: hello
EOF
echo '== empty run =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/empty-run.txt" 2>&1 || true
# CHECK: == empty run ==
# CHECK: empty-run.txt:1: error: RUN directive requires a command
# CHECK: empty-run.txt: note: run-and-check result: FAIL

# Verify that CHECK-family directives require an explicit pattern. Use `{{}}`
# when a zero-width pattern is intended.
write_case "$TMP_DIR/empty-check.txt" <<'EOF'
@@ RUN: true
@@ CHECK:
EOF
echo '== empty check =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/empty-check.txt" 2>&1 || true
# CHECK: == empty check ==
# CHECK: empty-check.txt:2: error: CHECK directive requires a pattern
# CHECK: empty-check.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/unterminated-regex.txt" <<'EOF'
@@ RUN: true
@@ CHECK: {{unterminated
EOF
echo '== unterminated regex =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/unterminated-regex.txt" 2>&1 || true
# CHECK: == unterminated regex ==
# CHECK: unterminated-regex.txt:2: error: unterminated
# CHECK-SAME: {{[{][{]\.\.\.[}][}]}} regex fragment
# CHECK: unterminated-regex.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/unterminated-var.txt" <<'EOF'
@@ RUN: true
@@ CHECK: [[unterminated
EOF
echo '== unterminated var =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/unterminated-var.txt" 2>&1 || true
# CHECK: == unterminated var ==
# CHECK: unterminated-var.txt:2: error: unterminated
# CHECK-SAME: {{\[\[\.\.\.\]\]}} variable fragment
# CHECK: unterminated-var.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/invalid-empty-var.txt" <<'EOF'
@@ RUN: true
@@ CHECK: [[]]
EOF
echo '== invalid empty var =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/invalid-empty-var.txt" 2>&1 || true
# CHECK: == invalid empty var ==
# CHECK: invalid-empty-var.txt:2: error: invalid expression '' in CHECK: expected an expression, got end of expression
# CHECK: invalid-empty-var.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/invalid-first-char.txt" <<'EOF'
@@ RUN: true
@@ CHECK: [[1bad]]
EOF
echo '== invalid first char =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/invalid-first-char.txt" 2>&1 || true
# CHECK: == invalid first char ==
# CHECK: invalid-first-char.txt:2: error: invalid expression '1bad' in CHECK: unexpected text 'bad' after expression
# CHECK: invalid-first-char.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/invalid-later-char.txt" <<'EOF'
@@ RUN: true
@@ CHECK: [[bad-name]]
EOF
echo '== invalid later char =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/invalid-later-char.txt" 2>&1 || true
# CHECK: == invalid later char ==
# CHECK: invalid-later-char.txt:2: error: invalid expression 'bad-name' in CHECK: unexpected text '-name' after expression
# CHECK: invalid-later-char.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/invalid-empty-var-def.txt" <<'EOF'
@@ RUN: true
@@ CHECK: [[:[0-9]+]]
EOF
echo '== invalid empty var def =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/invalid-empty-var-def.txt" 2>&1 || true
# CHECK: == invalid empty var def ==
# CHECK: invalid-empty-var-def.txt:2: error: invalid expression
# CHECK-SAME: expected an expression, got ':'
# CHECK: invalid-empty-var-def.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/invalid-first-char-var-def.txt" <<'EOF'
@@ RUN: true
@@ CHECK: [[1bad:[0-9]+]]
EOF
echo '== invalid first char var def =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/invalid-first-char-var-def.txt" 2>&1 || true
# CHECK: == invalid first char var def ==
# CHECK: invalid-first-char-var-def.txt:2: error: invalid expression
# CHECK-SAME: unexpected text 'bad:[0-9]+' after expression
# CHECK: invalid-first-char-var-def.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/invalid-var-def.txt" <<'EOF'
@@ RUN: true
@@ CHECK: [[bad-name:[0-9]+]]
EOF
echo '== invalid var def =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/invalid-var-def.txt" 2>&1 || true
# CHECK: == invalid var def ==
# CHECK: invalid-var-def.txt:2: error: invalid expression
# CHECK-SAME: unexpected text '-name:[0-9]+' after expression
# CHECK: invalid-var-def.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/invalid-close-paren-var-def.txt" <<'EOF'
@@ RUN: true
@@ CHECK: [[bad):[0-9]+]]
EOF
echo '== invalid close paren var def =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/invalid-close-paren-var-def.txt" 2>&1 || true
# CHECK: == invalid close paren var def ==
# CHECK: invalid-close-paren-var-def.txt:2: error: invalid expression
# CHECK-SAME: unexpected text '):[0-9]+' after expression
# CHECK: invalid-close-paren-var-def.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/unknown-expression-function.txt" <<'EOF'
@@ RUN: true
@@ CHECK: [[bogus()]]
EOF
echo '== unknown expression function =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/unknown-expression-function.txt" 2>&1 || true
# CHECK: == unknown expression function ==
# CHECK: unknown-expression-function.txt:2: error: invalid expression
# CHECK-SAME: unknown function 'bogus'
# CHECK: unknown-expression-function.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/rgb-arity.txt" <<'EOF'
@@ RUN: true
@@ CHECK: [[rgb()]]
EOF
echo '== rgb arity =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/rgb-arity.txt" 2>&1 || true
# CHECK: == rgb arity ==
# CHECK: rgb-arity.txt:2: error: invalid expression
# CHECK-SAME: rgb() expects 1 argument, got 0
# CHECK: rgb-arity.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/call-undefined-argument.txt" <<'EOF'
@@ RUN: true
@@ CHECK: [[rgb(missing)]]
EOF
echo '== call undefined argument =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/call-undefined-argument.txt" 2>&1 || true
# CHECK: == call undefined argument ==
# CHECK: call-undefined-argument.txt:2: error: undefined variable {{\[\[missing\]\]}} in CHECK
# CHECK: call-undefined-argument.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/range-variable-argument.txt" <<'EOF'
@@ RUN: printf '1:2\n'
@@ CHECK: [[v:[0-9]+:[0-9]+]]
@@ CHECK: [[rgb(v)]]
EOF
echo '== range variable argument =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/range-variable-argument.txt" 2>&1 || true
# CHECK: == range variable argument ==
# CHECK: range-variable-argument.txt:3: error: invalid expression
# CHECK-SAME: rgb() argument must be an unsigned 32-bit decimal or hexadecimal integer
# CHECK: range-variable-argument.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/unquoted-variable-range.txt" <<'EOF'
@@ RUN: printf '1\n'
@@ CHECK: [[v:[0-9]+]]
@@ CHECK: [[rgb(v:1)]]
EOF
echo '== unquoted variable range =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/unquoted-variable-range.txt" 2>&1 || true
# CHECK: == unquoted variable range ==
# CHECK: unquoted-variable-range.txt:3: error: invalid expression
# CHECK-SAME: expected ',' or ')' in function argument list, got ':'
# CHECK: unquoted-variable-range.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/variable-before-comma.txt" <<'EOF'
@@ RUN: printf '1\n'
@@ CHECK: [[v:[0-9]+]]
@@ CHECK: [[ph(v, 0, "x")]]
EOF
echo '== variable before comma =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/variable-before-comma.txt" 2>&1 || true
# CHECK: == variable before comma ==
# CHECK: variable-before-comma.txt:3: error: invalid expression
# CHECK-SAME: ph() image id must be an unsigned decimal or hexadecimal integer
# CHECK: variable-before-comma.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/range-bad-start.txt" <<'EOF'
@@ RUN: true
@@ CHECK: [[ph(0, "x:1")]]
EOF
echo '== range bad start =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/range-bad-start.txt" 2>&1 || true
# CHECK: == range bad start ==
# CHECK: range-bad-start.txt:2: error: invalid expression
# CHECK-SAME: ph() column range start must be an unsigned 32-bit decimal integer
# CHECK: range-bad-start.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/range-missing-start.txt" <<'EOF'
@@ RUN: true
@@ CHECK: [[ph(0, ":1")]]
EOF
echo '== range missing start =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/range-missing-start.txt" 2>&1 || true
# CHECK: == range missing start ==
# CHECK: range-missing-start.txt:2: error: invalid expression
# CHECK-SAME: ph() column range start must be an unsigned 32-bit decimal integer
# CHECK: range-missing-start.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/range-bad-end.txt" <<'EOF'
@@ RUN: true
@@ CHECK: [[ph(0, "1:x")]]
EOF
echo '== range bad end =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/range-bad-end.txt" 2>&1 || true
# CHECK: == range bad end ==
# CHECK: range-bad-end.txt:2: error: invalid expression
# CHECK-SAME: ph() column range end must be an unsigned 32-bit decimal integer
# CHECK: range-bad-end.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/range-missing-end.txt" <<'EOF'
@@ RUN: true
@@ CHECK: [[ph(0, "1:")]]
EOF
echo '== range missing end =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/range-missing-end.txt" 2>&1 || true
# CHECK: == range missing end ==
# CHECK: range-missing-end.txt:2: error: invalid expression
# CHECK-SAME: ph() column range end must be an unsigned 32-bit decimal integer
# CHECK: range-missing-end.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/range-extra-colon.txt" <<'EOF'
@@ RUN: true
@@ CHECK: [[ph(0, "1:2:3")]]
EOF
echo '== range extra colon =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/range-extra-colon.txt" 2>&1 || true
# CHECK: == range extra colon ==
# CHECK: range-extra-colon.txt:2: error: invalid expression
# CHECK-SAME: ph() column range must contain exactly one ':'
# CHECK: range-extra-colon.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/range-as-number.txt" <<'EOF'
@@ RUN: true
@@ CHECK: [[rgb("1:2")]]
EOF
echo '== range as number =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/range-as-number.txt" 2>&1 || true
# CHECK: == range as number ==
# CHECK: range-as-number.txt:2: error: invalid expression
# CHECK-SAME: rgb() argument must be an unsigned 32-bit decimal or hexadecimal integer
# CHECK: range-as-number.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/empty-number.txt" <<'EOF'
@@ RUN: true
@@ CHECK: [[rgb("")]]
EOF
echo '== empty number =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/empty-number.txt" 2>&1 || true
# CHECK: == empty number ==
# CHECK: empty-number.txt:2: error: invalid expression
# CHECK-SAME: rgb() argument must be an unsigned 32-bit decimal or hexadecimal integer
# CHECK: empty-number.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/bad-number.txt" <<'EOF'
@@ RUN: true
@@ CHECK: [[rgb("12x")]]
EOF
echo '== bad number =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/bad-number.txt" 2>&1 || true
# CHECK: == bad number ==
# CHECK: bad-number.txt:2: error: invalid expression
# CHECK-SAME: rgb() argument must be an unsigned 32-bit decimal or hexadecimal integer
# CHECK: bad-number.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/negative-number.txt" <<'EOF'
@@ RUN: true
@@ CHECK: [[rgb("-1")]]
EOF
echo '== negative number =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/negative-number.txt" 2>&1 || true
# CHECK: == negative number ==
# CHECK: negative-number.txt:2: error: invalid expression
# CHECK-SAME: rgb() argument must be an unsigned 32-bit decimal or hexadecimal integer
# CHECK: negative-number.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/large-number.txt" <<'EOF'
@@ RUN: true
@@ CHECK: [[rgb(4294967296)]]
EOF
echo '== large number =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/large-number.txt" 2>&1 || true
# CHECK: == large number ==
# CHECK: large-number.txt:2: error: invalid expression
# CHECK-SAME: rgb() argument must be an unsigned 32-bit decimal or hexadecimal integer
# CHECK: large-number.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/overflow-number.txt" <<'EOF'
@@ RUN: true
@@ CHECK: [[rgb(99999999999999999999999999999999999999999999999999999999)]]
EOF
echo '== overflow number =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/overflow-number.txt" 2>&1 || true
# CHECK: == overflow number ==
# CHECK: overflow-number.txt:2: error: invalid expression
# CHECK-SAME: rgb() argument must be an unsigned 32-bit decimal or hexadecimal integer
# CHECK: overflow-number.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/missing-hex-digit.txt" <<'EOF'
@@ RUN: true
@@ CHECK: [[0x]]
EOF
echo '== missing hex digit =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/missing-hex-digit.txt" 2>&1 || true
# CHECK: == missing hex digit ==
# CHECK: missing-hex-digit.txt:2: error: invalid expression
# CHECK-SAME: expected a hexadecimal digit, got end of expression
# CHECK: missing-hex-digit.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/bad-expression-argument.txt" <<'EOF'
@@ RUN: true
@@ CHECK: [[rgb(,)]]
EOF
echo '== bad expression argument =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/bad-expression-argument.txt" 2>&1 || true
# CHECK: == bad expression argument ==
# CHECK: bad-expression-argument.txt:2: error: invalid expression
# CHECK-SAME: expected an expression, got ','
# CHECK: bad-expression-argument.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/bad-argument-separator.txt" <<'EOF'
@@ RUN: true
@@ CHECK: [[rgb(1 2)]]
EOF
echo '== bad argument separator =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/bad-argument-separator.txt" 2>&1 || true
# CHECK: == bad argument separator ==
# CHECK: bad-argument-separator.txt:2: error: invalid expression
# CHECK-SAME: expected ',' or ')' in function argument list, got '2'
# CHECK: bad-argument-separator.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/missing-call-close-paren.txt" <<'EOF'
@@ RUN: true
@@ CHECK: [[rgb(1]]
EOF
echo '== missing call close paren =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/missing-call-close-paren.txt" 2>&1 || true
# CHECK: == missing call close paren ==
# CHECK: missing-call-close-paren.txt:2: error: invalid expression
# CHECK-SAME: expected ',' or ')' in function argument list, got end of expression
# CHECK: missing-call-close-paren.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/bad-call-colon.txt" <<'EOF'
@@ RUN: true
@@ CHECK: [[rgb(1:2:3)]]
EOF
echo '== bad call colon =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/bad-call-colon.txt" 2>&1 || true
# CHECK: == bad call colon ==
# CHECK: bad-call-colon.txt:2: error: invalid expression
# CHECK-SAME: expected ',' or ')' in function argument list, got ':'
# CHECK: bad-call-colon.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/ph-arity.txt" <<'EOF'
@@ RUN: true
@@ CHECK: [[ph(0, 1, 2, 3)]]
EOF
echo '== ph arity =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/ph-arity.txt" 2>&1 || true
# CHECK: == ph arity ==
# CHECK: ph-arity.txt:2: error: invalid expression
# CHECK-SAME: ph() expects 0 to 3 arguments, got 4
# CHECK: ph-arity.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/ph-range-order.txt" <<'EOF'
@@ RUN: true
@@ CHECK: [[ph(0, "2:0")]]
EOF
echo '== ph range order =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/ph-range-order.txt" 2>&1 || true
# CHECK: == ph range order ==
# CHECK: ph-range-order.txt:2: error: invalid expression
# CHECK-SAME: ph() column range start must be less
# CHECK: ph-range-order.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/ph-bad-row.txt" <<'EOF'
@@ RUN: true
@@ CHECK: [[ph(297)]]
EOF
echo '== ph bad row =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/ph-bad-row.txt" 2>&1 || true
# CHECK: == ph bad row ==
# CHECK: ph-bad-row.txt:2: error: invalid expression
# CHECK-SAME: row value 297 is outside the supported placeholder diacritic range
# CHECK: ph-bad-row.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/ph-bad-column.txt" <<'EOF'
@@ RUN: true
@@ CHECK: [[ph(0, 297)]]
EOF
echo '== ph bad column =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/ph-bad-column.txt" 2>&1 || true
# CHECK: == ph bad column ==
# CHECK: ph-bad-column.txt:2: error: invalid expression
# CHECK-SAME: column value 297 is outside the supported placeholder diacritic range
# CHECK: ph-bad-column.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/ph-bad-row-number.txt" <<'EOF'
@@ RUN: true
@@ CHECK: [[ph("x")]]
EOF
echo '== ph bad row number =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/ph-bad-row-number.txt" 2>&1 || true
# CHECK: == ph bad row number ==
# CHECK: ph-bad-row-number.txt:2: error: invalid expression
# CHECK-SAME: ph() row must be an unsigned 32-bit decimal integer
# CHECK: ph-bad-row-number.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/ph-negative-row-number.txt" <<'EOF'
@@ RUN: true
@@ CHECK: [[ph("-1")]]
EOF
echo '== ph negative row number =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/ph-negative-row-number.txt" 2>&1 || true
# CHECK: == ph negative row number ==
# CHECK: ph-negative-row-number.txt:2: error: invalid expression
# CHECK-SAME: ph() row must be an unsigned 32-bit decimal integer
# CHECK: ph-negative-row-number.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/ph-large-row-number.txt" <<'EOF'
@@ RUN: true
@@ CHECK: [[ph(4294967296)]]
EOF
echo '== ph large row number =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/ph-large-row-number.txt" 2>&1 || true
# CHECK: == ph large row number ==
# CHECK: ph-large-row-number.txt:2: error: invalid expression
# CHECK-SAME: ph() row must be an unsigned 32-bit decimal integer
# CHECK: ph-large-row-number.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/ph-bad-column-number.txt" <<'EOF'
@@ RUN: true
@@ CHECK: [[ph(0, "x")]]
EOF
echo '== ph bad column number =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/ph-bad-column-number.txt" 2>&1 || true
# CHECK: == ph bad column number ==
# CHECK: ph-bad-column-number.txt:2: error: invalid expression
# CHECK-SAME: ph() column must be an unsigned 32-bit decimal integer
# CHECK: ph-bad-column-number.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/ph-bad-image-id-number.txt" <<'EOF'
@@ RUN: true
@@ CHECK: [[ph(0, 0, "x")]]
EOF
echo '== ph bad image id number =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/ph-bad-image-id-number.txt" 2>&1 || true
# CHECK: == ph bad image id number ==
# CHECK: ph-bad-image-id-number.txt:2: error: invalid expression
# CHECK-SAME: ph() image id must be an unsigned decimal or hexadecimal integer
# CHECK: ph-bad-image-id-number.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/ph-large-image-id.txt" <<'EOF'
@@ RUN: true
@@ CHECK: [[ph(0, 0, 0x100000000)]]
EOF
echo '== ph large image id =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/ph-large-image-id.txt" 2>&1 || true
# CHECK: == ph large image id ==
# CHECK: ph-large-image-id.txt:2: error: invalid expression
# CHECK-SAME: ph() image id must be a 32-bit unsigned integer
# CHECK: ph-large-image-id.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/bad-string-escape.txt" <<'EOF'
@@ RUN: true
@@ CHECK: [["\q"]]
EOF
echo '== bad string escape =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/bad-string-escape.txt" 2>&1 || true
# CHECK: == bad string escape ==
# CHECK: bad-string-escape.txt:2: error: invalid expression
# CHECK-SAME: unknown string escape
# CHECK: bad-string-escape.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/bad-string-hex.txt" <<'EOF'
@@ RUN: true
@@ CHECK: [["\xZZ"]]
EOF
echo '== bad string hex =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/bad-string-hex.txt" 2>&1 || true
# CHECK: == bad string hex ==
# CHECK: bad-string-hex.txt:2: error: invalid expression
# CHECK-SAME: string literal has an invalid \xHH escape
# CHECK: bad-string-hex.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/short-string-hex.txt" <<'EOF'
@@ RUN: true
@@ CHECK: [["\x]]
EOF
echo '== short string hex =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/short-string-hex.txt" 2>&1 || true
# CHECK: == short string hex ==
# CHECK: short-string-hex.txt:2: error: invalid expression
# CHECK-SAME: string literal has an invalid \xHH escape
# CHECK: short-string-hex.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/short-string-low-hex.txt" <<'EOF'
@@ RUN: true
@@ CHECK: [["\x0]]
EOF
echo '== short string low hex =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/short-string-low-hex.txt" 2>&1 || true
# CHECK: == short string low hex ==
# CHECK: short-string-low-hex.txt:2: error: invalid expression
# CHECK-SAME: string literal has an invalid \xHH escape
# CHECK: short-string-low-hex.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/bad-string-low-hex.txt" <<'EOF'
@@ RUN: true
@@ CHECK: [["\x0Z"]]
EOF
echo '== bad string low hex =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/bad-string-low-hex.txt" 2>&1 || true
# CHECK: == bad string low hex ==
# CHECK: bad-string-low-hex.txt:2: error: invalid expression
# CHECK-SAME: string literal has an invalid \xHH escape
# CHECK: bad-string-low-hex.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/nul-string-hex.txt" <<'EOF'
@@ RUN: true
@@ CHECK: [["\x00"]]
EOF
echo '== nul string hex =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/nul-string-hex.txt" 2>&1 || true
# CHECK: == nul string hex ==
# CHECK: nul-string-hex.txt:2: error: invalid expression
# CHECK-SAME: string literal escape \x00 is unsupported
# CHECK: nul-string-hex.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/unterminated-string.txt" <<'EOF'
@@ RUN: true
@@ CHECK: [["abc]]
EOF
echo '== unterminated string =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/unterminated-string.txt" 2>&1 || true
# CHECK: == unterminated string ==
# CHECK: unterminated-string.txt:2: error: invalid expression
# CHECK-SAME: unterminated string literal
# CHECK: unterminated-string.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/invalid-regex.txt" <<'EOF'
@@ RUN: echo "hello"
@@ CHECK: {{(}}
EOF
echo '== invalid regex =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/invalid-regex.txt" 2>&1 || true
# CHECK: == invalid regex ==
# CHECK: invalid-regex.txt:2: error: invalid regex in CHECK:
# CHECK: invalid-regex.txt:2: note: expanded regex: (
# CHECK: invalid-regex.txt: note: run-and-check result: FAIL

# Diagnostics that echo CHECK patterns should escape control bytes from the
# test file instead of writing those bytes directly to stderr.
{
    printf '# RUN: printf "plain\\n"\n'
    printf '# CHECK: '
    printf '\001'
    printf '\n'
} >"$TMP_DIR/control-pattern.txt"
echo '== control pattern diagnostic =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/control-pattern.txt" 2>&1 || true
# CHECK: == control pattern diagnostic ==
# CHECK: control-pattern.txt:2: note: pattern: <01>
# CHECK: control-pattern.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/trailing-regex-backslash.txt" <<'EOF'
@@ RUN: echo "hello"
@@ CHECK: {{\}}
EOF
echo '== trailing regex backslash =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/trailing-regex-backslash.txt" 2>&1 || true
# CHECK: == trailing regex backslash ==
# CHECK: trailing-regex-backslash.txt:2: error: invalid regex in CHECK:
# CHECK: trailing-regex-backslash.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/short-hex-regex.txt" <<'EOF'
@@ RUN: echo "hello"
@@ CHECK: {{\x}}
EOF
echo '== short hex regex =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/short-hex-regex.txt" 2>&1 || true
# CHECK: == short hex regex ==
# CHECK: short-hex-regex.txt:2: error: invalid \xHH regex escape
# CHECK: short-hex-regex.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/bad-high-hex-regex.txt" <<'EOF'
@@ RUN: echo "hello"
@@ CHECK: {{\xz1}}
EOF
echo '== bad high hex regex =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/bad-high-hex-regex.txt" 2>&1 || true
# CHECK: == bad high hex regex ==
# CHECK: bad-high-hex-regex.txt:2: error: invalid \xHH regex escape
# CHECK: bad-high-hex-regex.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/bad-punctuation-hex-regex.txt" <<'EOF'
@@ RUN: echo "hello"
@@ CHECK: {{\x/1}}
EOF
echo '== bad punctuation hex regex =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/bad-punctuation-hex-regex.txt" 2>&1 || true
# CHECK: == bad punctuation hex regex ==
# CHECK: bad-punctuation-hex-regex.txt:2: error: invalid \xHH regex escape
# CHECK: bad-punctuation-hex-regex.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/bad-low-hex-regex.txt" <<'EOF'
@@ RUN: echo "hello"
@@ CHECK: {{\x1z}}
EOF
echo '== bad low hex regex =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/bad-low-hex-regex.txt" 2>&1 || true
# CHECK: == bad low hex regex ==
# CHECK: bad-low-hex-regex.txt:2: error: invalid \xHH regex escape
# CHECK: bad-low-hex-regex.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/nul-hex-regex.txt" <<'EOF'
@@ RUN: echo "hello"
@@ CHECK: {{\x00}}
EOF
echo '== nul hex regex =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/nul-hex-regex.txt" 2>&1 || true
# CHECK: == nul hex regex ==
# CHECK: nul-hex-regex.txt:2: error: regex escape \x00 is unsupported
# CHECK: nul-hex-regex.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/short-hex-var-def.txt" <<'EOF'
@@ RUN: echo "hello"
@@ CHECK: [[value:\x]]
EOF
echo '== short hex var def =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/short-hex-var-def.txt" 2>&1 || true
# CHECK: == short hex var def ==
# CHECK: short-hex-var-def.txt:2: error: invalid \xHH regex escape
# CHECK: short-hex-var-def.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/run-fails.txt" <<'EOF'
@@ RUN: sh -c 'echo stdout-line; echo stderr-line >&2; exit 7'
@@ CHECK: unreachable
EOF
echo '== run fails =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/run-fails.txt" 2>&1 || true
# CHECK: == run fails ==
# CHECK: run-fails.txt: note: RUN exit code: 7
# CHECK: run-fails.txt: error: RUN command failed with exit code 7
# CHECK: run-fails.txt: note: last 1 lines of stdout
# CHECK: stdout-line
# CHECK: run-fails.txt: note: last 1 lines of stderr
# CHECK: stderr-line
# CHECK: run-fails.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/run-signaled.txt" <<'EOF'
@@ RUN: kill -TERM $$
@@ CHECK: unreachable
EOF
echo '== run signaled =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/run-signaled.txt" 2>&1 || true
# CHECK: == run signaled ==
# CHECK: run-signaled.txt: note: RUN exit code: 143
# CHECK: run-signaled.txt: error: RUN command failed with exit code 143
# CHECK: run-signaled.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/missing-output-file.txt" <<'EOF'
@@ RUN: rm "$IMGNEKO_TEST_OUTPUT_DIR/run-stdout"
@@ CHECK: hello
EOF
echo '== missing output file =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/missing-output-file.txt" 2>&1 || true
# CHECK: == missing output file ==
# CHECK: error: failed to read output file

write_case "$TMP_DIR/missing-tail-files.txt" <<'EOF'
@@ RUN: rm "$IMGNEKO_TEST_OUTPUT_DIR/run-stdout" "$IMGNEKO_TEST_OUTPUT_DIR/run-stderr"; exit 9
@@ CHECK: unreachable
EOF
echo '== missing tail files =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/missing-tail-files.txt" 2>&1 || true
# CHECK: == missing tail files ==
# CHECK: missing-tail-files.txt: note: RUN exit code: 9
# CHECK: missing-tail-files.txt: error: RUN command failed with exit code 9
# CHECK: missing-tail-files.txt: note: failed to open stdout file
# CHECK: missing-tail-files.txt: note: failed to open stderr file
# CHECK: missing-tail-files.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/no-remaining-output.txt" <<'EOF'
@@ RUN: true
@@ CHECK: unreachable
EOF
echo '== no remaining output =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/no-remaining-output.txt" 2>&1 || true
# CHECK: == no remaining output ==
# CHECK: no-remaining-output.txt:2: error: CHECK did not match
# CHECK: no-remaining-output.txt:2: note: there is no remaining output to search
# CHECK: no-remaining-output.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/usage-too-many.txt" <<'EOF'
@@ RUN: true
@@ CHECK: hello
EOF
echo '== usage no args =='
"$RUN_AND_CHECK" 2>&1 || true
# CHECK: == usage no args ==
# CHECK: usage:

echo '== usage too many args =='
"$RUN_AND_CHECK" "$TMP_DIR/usage-too-many.txt" "$TMP_DIR/usage-too-many.txt" 2>&1 || true
# CHECK: == usage too many args ==
# CHECK: usage:
