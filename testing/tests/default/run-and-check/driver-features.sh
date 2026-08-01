#!/usr/bin/env run-and-check
# SPDX-License-Identifier: MIT-0
# RUN: sh %s

# Exercise successful nested run-and-check paths that are awkward to hit from
# the existing direct fixture files: literal regex escaping, variable-name
# shapes, percent expansion, %s quoting, and auto-created output dirs.
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
TMP_DIR=$(mktemp -d /tmp/imgneko-run-and-check-driver-features.XXXXXX)
OUTPUT_DIR=$TMP_DIR/output
QUOTE_DIR=$(mktemp -d "/tmp/imgneko-run-and-check-quote.'XXXXXX")

cleanup() {
    rm -rf "$TMP_DIR" "$QUOTE_DIR"
}

trap cleanup EXIT INT TERM HUP

[ -x "$RUN_AND_CHECK" ] || fail "missing run-and-check binary: $RUN_AND_CHECK"
mkdir -p "$OUTPUT_DIR"

write_case "$TMP_DIR/literal-metacharacters.txt" <<'EOF'
@@ RUN: echo 'a.b[?]{x}'
@@ CHECK: a.b[?]{x}
EOF
echo '== literal metacharacters =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/literal-metacharacters.txt" 2>&1 || true
# CHECK: == literal metacharacters ==
# CHECK: literal-metacharacters.txt: note: RUN exit code: 0
# CHECK: literal-metacharacters.txt: note: run-and-check result: PASS

write_case "$TMP_DIR/underscore-variables.txt" <<'EOF'
@@ RUN: echo 'name_1'
@@ CHECK: [[_name_1:[a-z]+_[0-9]+]]
EOF
echo '== underscore variables =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/underscore-variables.txt" 2>&1 || true
# CHECK: == underscore variables ==
# CHECK: underscore-variables.txt: note: RUN exit code: 0
# CHECK: underscore-variables.txt: note: run-and-check result: PASS

write_case "$TMP_DIR/percent-expansion.txt" <<'EOF'
@@ RUN: echo a%%b %q
@@ CHECK: a%b %q
EOF
echo '== percent expansion =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/percent-expansion.txt" 2>&1 || true
# CHECK: == percent expansion ==
# CHECK: percent-expansion.txt: note: RUN exit code: 0
# CHECK: percent-expansion.txt: note: run-and-check result: PASS

write_case "$TMP_DIR/ignored-directives.txt" <<'EOF'
@@ RUN: echo 'ok'
@@ RUM: ignored
@@ CHECX: ignored
@@ CHECK-NEAT: ignored
@@ CHECK-NAH: ignored
@@ CHECK: ok
EOF
echo '== ignored directives =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/ignored-directives.txt" 2>&1 || true
# CHECK: == ignored directives ==
# CHECK: ignored-directives.txt: note: RUN exit code: 0
# CHECK: ignored-directives.txt: note: run-and-check result: PASS

write_case "$TMP_DIR/optional-regex-group.txt" <<'EOF'
@@ RUN: echo 'bar'
@@ CHECK: {{(foo)?}}bar
EOF
echo '== optional regex group =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/optional-regex-group.txt" 2>&1 || true
# CHECK: == optional regex group ==
# CHECK: optional-regex-group.txt: note: RUN exit code: 0
# CHECK: optional-regex-group.txt: note: run-and-check result: PASS

write_case "$TMP_DIR/empty-output-not.txt" <<'EOF'
@@ RUN: true
@@ CHECK-NOT: forbidden
EOF
echo '== empty output not =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/empty-output-not.txt" 2>&1 || true
# CHECK: == empty output not ==
# CHECK: empty-output-not.txt: note: RUN exit code: 0
# CHECK: empty-output-not.txt: note: run-and-check result: PASS

write_case "$TMP_DIR/empty-middle-line.txt" <<'EOF'
@@ RUN: printf 'alpha\n\nomega\n'
@@ CHECK: alpha
@@ CHECK-NOT: forbidden
@@ CHECK: omega
EOF
echo '== empty middle line =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$TMP_DIR/empty-middle-line.txt" 2>&1 || true
# CHECK: == empty middle line ==
# CHECK: empty-middle-line.txt: note: RUN exit code: 0
# CHECK: empty-middle-line.txt: note: run-and-check result: PASS

write_case "$TMP_DIR/unset-output-dir.txt" <<'EOF'
@@ RUN: true
@@ CHECK-NOT: forbidden
EOF
echo '== unset output dir =='
env -u IMGNEKO_TEST_OUTPUT_DIR "$RUN_AND_CHECK" \
    "$TMP_DIR/unset-output-dir.txt" 2>&1 || true
# CHECK: == unset output dir ==
# CHECK: unset-output-dir.txt: note: stdout file: /tmp/imgneko-run-and-check.
# CHECK: unset-output-dir.txt: note: stderr file: /tmp/imgneko-run-and-check.
# CHECK: unset-output-dir.txt: note: RUN exit code: 0
# CHECK: unset-output-dir.txt: note: run-and-check result: PASS

write_case "$QUOTE_DIR/path's-quoted.txt" <<'EOF'
@@ RUN: test -f %s
@@ CHECK-NOT: forbidden
EOF
echo '== quoted path =='
env IMGNEKO_TEST_OUTPUT_DIR="$OUTPUT_DIR" "$RUN_AND_CHECK" \
    "$QUOTE_DIR/path's-quoted.txt" 2>&1 || true
# CHECK: == quoted path ==
# CHECK: path's-quoted.txt: note: RUN exit code: 0
# CHECK: path's-quoted.txt: note: run-and-check result: PASS
