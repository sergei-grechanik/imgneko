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
# CHECK: invalid-empty-var.txt:2: error: invalid variable name: ''
# CHECK: invalid-empty-var.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/invalid-first-char.txt" <<'EOF'
@@ RUN: true
@@ CHECK: [[1bad]]
EOF
echo '== invalid first char =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/invalid-first-char.txt" 2>&1 || true
# CHECK: == invalid first char ==
# CHECK: invalid-first-char.txt:2: error: invalid variable name: '1bad'
# CHECK: invalid-first-char.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/invalid-later-char.txt" <<'EOF'
@@ RUN: true
@@ CHECK: [[bad-name]]
EOF
echo '== invalid later char =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/invalid-later-char.txt" 2>&1 || true
# CHECK: == invalid later char ==
# CHECK: invalid-later-char.txt:2: error: invalid variable name: 'bad-name'
# CHECK: invalid-later-char.txt: note: run-and-check result: FAIL

write_case "$TMP_DIR/invalid-var-def.txt" <<'EOF'
@@ RUN: true
@@ CHECK: [[bad-name:[0-9]+]]
EOF
echo '== invalid var def =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/invalid-var-def.txt" 2>&1 || true
# CHECK: == invalid var def ==
# CHECK: invalid-var-def.txt:2: error: invalid variable name: 'bad-name'
# CHECK: invalid-var-def.txt: note: run-and-check result: FAIL

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
