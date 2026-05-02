#!/bin/sh
# SPDX-License-Identifier: MIT-0

set -eu

fail() {
    printf '%s\n' "$1" >&2
    exit 1
}

if [ -z "${IMGNEKO_TEST_OUTPUT_DIR:-}" ] ||
   [ ! -d "$IMGNEKO_TEST_OUTPUT_DIR" ]; then
    fail "IMGNEKO_TEST_OUTPUT_DIR is not set to an existing directory"
fi

# Assert that a variable is completely absent from the environment, not just
# set to an empty string.
require_unset() {
    var_name=$1
    eval "is_set=\${$var_name+x}"
    test -z "${is_set}" || fail "$var_name should be unset"
}

# The runner exports stable paths to the project root and selected build dir.
test -n "${IMGNEKO_ROOT_DIR:-}" || fail "IMGNEKO_ROOT_DIR is not set"
test -n "${IMGNEKO_BUILD_DIR:-}" || fail "IMGNEKO_BUILD_DIR is not set"

test -d "$IMGNEKO_ROOT_DIR" || fail "IMGNEKO_ROOT_DIR is not a directory"
test -d "$IMGNEKO_BUILD_DIR" || fail "IMGNEKO_BUILD_DIR is not a directory"
test -d "$IMGNEKO_BUILD_DIR/bin" || fail "build bin directory is missing"
test -f "$IMGNEKO_ROOT_DIR/src/main.c" || fail "project root does not look correct"

# Each test gets its own absolute output directory and runs from it.
expected_output_dir=$IMGNEKO_BUILD_DIR/test-outputs/runner/environment.sh
expected_output_file=$expected_output_dir/output
C_ENV_TEST=$IMGNEKO_BUILD_DIR/obj/test-bin/runner/environment.c.bin

test "$IMGNEKO_TEST_OUTPUT_DIR" = "$expected_output_dir" ||
    fail "IMGNEKO_TEST_OUTPUT_DIR does not match this test's output directory"
test -f "$expected_output_file" || fail "output file is not a regular file"
test -x "$C_ENV_TEST" || fail "runner environment C test binary is missing"

current_dir=$(pwd)
test "$current_dir" = "$IMGNEKO_TEST_OUTPUT_DIR" ||
    fail "current directory does not match IMGNEKO_TEST_OUTPUT_DIR"

# PATH should be prefixed with the build bin dir so tests can execute freshly
# built tools without extra setup.
path_prefix=${PATH%%:*}
test "$path_prefix" = "$IMGNEKO_BUILD_DIR/bin" ||
    fail "PATH does not start with build bin directory"

imgneko_path=$(command -v imgneko)
test "$imgneko_path" = "$IMGNEKO_BUILD_DIR/bin/imgneko" ||
    fail "imgneko is not resolved from the build bin directory"

# The runner strips inherited make variables so nested make calls from tests do
# not accidentally reuse the outer build state.
require_unset BUILD_DIR
require_unset MAKEFLAGS
require_unset MAKEOVERRIDES
require_unset MFLAGS
require_unset MAKELEVEL

invalid_output_file=$expected_output_dir/not-a-directory
: >"$invalid_output_file"

echo '== c output dir unset =='
set +e
env -u IMGNEKO_TEST_OUTPUT_DIR IMGNEKO_BUILD_DIR="$IMGNEKO_BUILD_DIR" \
    "$C_ENV_TEST" output_env 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK-NEXT: {{^}}== c output dir unset =={{$}}
# CHECK-NEXT: {{^}}output_env: IMGNEKO_TEST_OUTPUT_DIR is not set to an existing directory{{$}}
# CHECK-NEXT: {{^}}status=1{{$}}

echo '== c output dir empty =='
set +e
env IMGNEKO_BUILD_DIR="$IMGNEKO_BUILD_DIR" IMGNEKO_TEST_OUTPUT_DIR= \
    "$C_ENV_TEST" output_env 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK-NEXT: {{^}}== c output dir empty =={{$}}
# CHECK-NEXT: {{^}}output_env: IMGNEKO_TEST_OUTPUT_DIR is not set to an existing directory{{$}}
# CHECK-NEXT: {{^}}status=1{{$}}

echo '== c output dir missing =='
set +e
env IMGNEKO_BUILD_DIR="$IMGNEKO_BUILD_DIR" \
    IMGNEKO_TEST_OUTPUT_DIR="$expected_output_dir/missing-dir" \
    "$C_ENV_TEST" output_env 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK-NEXT: {{^}}== c output dir missing =={{$}}
# CHECK-NEXT: {{^}}output_env: IMGNEKO_TEST_OUTPUT_DIR is not set to an existing directory{{$}}
# CHECK-NEXT: {{^}}status=1{{$}}

echo '== c output dir file =='
set +e
env IMGNEKO_BUILD_DIR="$IMGNEKO_BUILD_DIR" \
    IMGNEKO_TEST_OUTPUT_DIR="$invalid_output_file" \
    "$C_ENV_TEST" output_env 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK-NEXT: {{^}}== c output dir file =={{$}}
# CHECK-NEXT: {{^}}output_env: IMGNEKO_TEST_OUTPUT_DIR is not set to an existing directory{{$}}
# CHECK-NEXT: {{^}}status=1{{$}}
