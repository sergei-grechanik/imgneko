#!/bin/sh

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

test "$IMGNEKO_TEST_OUTPUT_DIR" = "$expected_output_dir" ||
    fail "IMGNEKO_TEST_OUTPUT_DIR does not match this test's output directory"
test -f "$expected_output_file" || fail "output file is not a regular file"

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
