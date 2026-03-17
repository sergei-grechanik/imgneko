#!/bin/sh

set -eu

fail() {
    printf '%s\n' "$1" >&2
    exit 1
}

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
