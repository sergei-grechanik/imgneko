#!/bin/sh

# This is a test for the Makefile and the configure script. It runs a variety of
# scenarios that cover the expected use cases and error paths.

set -eu

ROOT_DIR=$(CDPATH= cd "$(dirname "$0")/../.." && pwd)

# Keep all repository-local artifacts directly under ./build so the script
# exercises the same layout that regular users will hit.
LOG_DIR=$ROOT_DIR/build/test-makefile-logs
DEFAULT_BUILD=$ROOT_DIR/build/default
CUSTOM_BUILD=$ROOT_DIR/build/test-debug-custom-cc
MISSING_CONFIG_BUILD=$ROOT_DIR/build/test-missing-config
INVALID_FEATURE_BUILD=$ROOT_DIR/build/test-invalid-feature
INVALID_COMPDB_BUILD=$ROOT_DIR/build/test-invalid-compdb
INVALID_DEPFILES_BUILD=$ROOT_DIR/build/test-invalid-depfiles
COMPDB_WITH_MESSAGE_BUILD=$ROOT_DIR/build/test-compdb-with-message
COMPDB_WITHOUT_MESSAGE_BUILD=$ROOT_DIR/build/test-compdb-without-message
DEPFILES_WITH_MESSAGE_BUILD=$ROOT_DIR/build/test-depfiles-with-message
DEPFILES_WITHOUT_MESSAGE_BUILD=$ROOT_DIR/build/test-depfiles-without-message
COMPDB_CLANG_BUILD=$ROOT_DIR/build/test-compdb-clang
SPACE_BUILD="$ROOT_DIR/build/test bad dir"
RECONFIGURE_BUILD=$ROOT_DIR/build/test-reconfigure-check
RELATIVE_BUILD_DIR_TEST=$ROOT_DIR/build/test-relative-builddir

# Use one temporary root for scenarios that intentionally leave the project
# tree, and clean it up on exit.
TMP_TEST_ROOT=$(mktemp -d /tmp/imgneko-makefile-test-artifacts.XXXXXX)
OUTSIDE_BUILD=$TMP_TEST_ROOT/outside-build
INSTALL_ROOT=$TMP_TEST_ROOT/install-root
STALE_REPO=$TMP_TEST_ROOT/stale-repo
NO_VERSION_REPO=$TMP_TEST_ROOT/no-version-repo
DEPFILES_REPO=$TMP_TEST_ROOT/depfiles-repo
SPACE_ROOT_REPO="$TMP_TEST_ROOT/root with spaces"

LAST_STATUS=0
LAST_OUTPUT=

# Print the current high-level test step so the run is easy to follow.
say() {
    printf '%s\n' "$*"
}

# Stop the script with a consistent failure message.
fail() {
    printf '%s\n' "FAIL: $*" >&2
    exit 1
}

cleanup() {
    rm -rf "$TMP_TEST_ROOT"
}

# Refuse to use a path that already exists so the test never clobbers a build
# or temporary directory that the user cares about.
assert_path_absent() {
    path=$1

    if [ -e "$path" ]; then
        fail "refusing to run because $path already exists"
    fi
}

# Verify that a regular file was created where the scenario expects one.
assert_file_exists() {
    path=$1

    if [ ! -f "$path" ]; then
        fail "expected file to exist: $path"
    fi
}

# Verify that a file contains a required string.
assert_file_contains() {
    path=$1
    needle=$2

    if ! grep -F -- "$needle" "$path" >/dev/null 2>&1; then
        printf '%s\n' "Expected file to contain: $needle" >&2
        printf '%s\n' "Actual file: $path" >&2
        sed -n '1,200p' "$path" >&2
        exit 1
    fi
}

# Verify that a file does not contain a forbidden string.
assert_file_not_contains() {
    path=$1
    needle=$2

    if grep -F -- "$needle" "$path" >/dev/null 2>&1; then
        printf '%s\n' "Expected file not to contain: $needle" >&2
        printf '%s\n' "Actual file: $path" >&2
        sed -n '1,200p' "$path" >&2
        exit 1
    fi
}

# Verify that a directory was created where the scenario expects one.
assert_dir_exists() {
    path=$1

    if [ ! -d "$path" ]; then
        fail "expected directory to exist: $path"
    fi
}

# Check that the last captured command output contains a required string.
assert_output_contains() {
    needle=$1
    assert_file_contains "$LAST_OUTPUT" "$needle"
}

# Check that the last captured command output does not contain a forbidden
# string.
assert_output_not_contains() {
    needle=$1
    assert_file_not_contains "$LAST_OUTPUT" "$needle"
}

# Verify that the last captured command succeeded.
assert_status_zero() {
    if [ "$LAST_STATUS" -ne 0 ]; then
        printf '%s\n' "Expected success but command failed with status $LAST_STATUS" >&2
        sed -n '1,200p' "$LAST_OUTPUT" >&2
        exit 1
    fi
}

# Verify that the last captured command failed.
assert_status_nonzero() {
    if [ "$LAST_STATUS" -eq 0 ]; then
        printf '%s\n' "Expected failure but command succeeded" >&2
        sed -n '1,200p' "$LAST_OUTPUT" >&2
        exit 1
    fi
}

# Run a command and capture both stdout and stderr into the exact output path
# supplied by the caller. The caller must provide the .out suffix explicitly.
run_capture() {
    LAST_OUTPUT=$1
    shift

    case $LAST_OUTPUT in
        *.out) ;;
        *)
            fail "run_capture output path must end with .out: $LAST_OUTPUT"
            ;;
    esac

    set +e
    "$@" >"$LAST_OUTPUT" 2>&1
    LAST_STATUS=$?
    set -e
}

# Copy the whole repository tree for tests that need to mutate top-level files
# such as configure or VERSION without touching the real checkout.
copy_repo() {
    destination=$1

    cp -R "$ROOT_DIR" "$destination"
    chmod +x "$destination/configure"
}

trap cleanup EXIT

assert_path_absent "$LOG_DIR"
assert_path_absent "$DEFAULT_BUILD"
assert_path_absent "$CUSTOM_BUILD"
assert_path_absent "$MISSING_CONFIG_BUILD"
assert_path_absent "$INVALID_FEATURE_BUILD"
assert_path_absent "$INVALID_COMPDB_BUILD"
assert_path_absent "$INVALID_DEPFILES_BUILD"
assert_path_absent "$COMPDB_WITH_MESSAGE_BUILD"
assert_path_absent "$COMPDB_WITHOUT_MESSAGE_BUILD"
assert_path_absent "$DEPFILES_WITH_MESSAGE_BUILD"
assert_path_absent "$DEPFILES_WITHOUT_MESSAGE_BUILD"
assert_path_absent "$COMPDB_CLANG_BUILD"
assert_path_absent "$SPACE_BUILD"
assert_path_absent "$RECONFIGURE_BUILD"
assert_path_absent "$RELATIVE_BUILD_DIR_TEST"
mkdir -p "$LOG_DIR"

# Verify the fully default path: no --build-dir, no profile override, and a
# normal build/run from build/default.
say "Default configure/build/run"
sh "$ROOT_DIR/configure"
assert_file_exists "$DEFAULT_BUILD/config.mk"
assert_file_exists "$DEFAULT_BUILD/Makefile"
assert_path_absent "$DEFAULT_BUILD/bin/imgneko"

make -C "$ROOT_DIR"
assert_file_exists "$DEFAULT_BUILD/bin/imgneko"

run_capture "$LOG_DIR/default-version.out" "$DEFAULT_BUILD/bin/imgneko" --version
assert_status_zero
assert_output_contains "profile: default"

# Configure a non-default profile, override a bad CC from the environment, and
# confirm that the saved settings affect the build metadata and compiler flags.
say "Debug profile with explicit CC and FEATURE_X"
env CC=false sh "$ROOT_DIR/configure" --build-dir="$CUSTOM_BUILD" --profile=debug --cc=cc --cppflags=-DFEATURE_X=1

run_capture "$LOG_DIR/custom-build.out" make -C "$CUSTOM_BUILD"
assert_status_zero
assert_output_contains "-DFEATURE_X=1"
assert_file_exists "$CUSTOM_BUILD/bin/imgneko"

run_capture "$LOG_DIR/custom-version.out" "$CUSTOM_BUILD/bin/imgneko" --version
assert_status_zero
assert_output_contains "profile: debug"
assert_output_contains "cc: cc"
assert_output_contains "-DFEATURE_X=1"

# With multiple configured build directories, plain root-level make must ask
# users to disambiguate instead of silently picking build/default.
say "Root make requires explicit disambiguation with multiple build directories"
run_capture "$LOG_DIR/root-make-ambiguous.out" make -C "$ROOT_DIR"
assert_status_nonzero
assert_output_contains "run make -C build/<name> or pass BUILD_DIR=<path> explicitly"

# Explicit BUILD_DIR from the repository root should still work.
say "Root make with explicit BUILD_DIR succeeds"
run_capture "$LOG_DIR/root-make-explicit-default.out" make -C "$ROOT_DIR" BUILD_DIR="$DEFAULT_BUILD"
assert_status_zero
assert_file_exists "$DEFAULT_BUILD/bin/imgneko"

# Reject `make depfile` unless configure explicitly enabled depfile generation
# for that build directory.
say "Makefile error when depfile generation is disabled"
run_capture "$LOG_DIR/make-depfile-disabled.out" make -C "$DEFAULT_BUILD" depfile
assert_status_nonzero
assert_output_contains "error: depfile generation is disabled in $DEFAULT_BUILD/config.mk; rerun ./configure --build-dir='$DEFAULT_BUILD' --depfiles"

# Build test prerequisites without running them.
say "Build test dependencies without executing tests"
run_capture "$LOG_DIR/make-test-deps.out" make -C "$DEFAULT_BUILD" test-deps
assert_status_zero
assert_file_exists "$DEFAULT_BUILD/bin/test-runner"
assert_file_exists "$DEFAULT_BUILD/obj/test-bin/unit/util/path.c.bin"
assert_output_not_contains "RUN:"

# A relative BUILD_DIR with a trailing slash should normalize to the same
# absolute build directory so test-runner env vars remain stable, and `make
# test` should also clear the default output tree before running tests.
say "Root make test accepts relative BUILD_DIR with trailing slash"
sh "$ROOT_DIR/configure" --build-dir="$RELATIVE_BUILD_DIR_TEST"
mkdir -p "$RELATIVE_BUILD_DIR_TEST/test-outputs/stale"
printf '%s\n' stale >"$RELATIVE_BUILD_DIR_TEST/test-outputs/stale/old-file"
run_capture "$LOG_DIR/root-make-relative-builddir.out" make -C "$ROOT_DIR" test BUILD_DIR=build/test-relative-builddir/ FILTER=runner/environment.sh
assert_status_zero
assert_output_contains "1/1 tests passed"
assert_path_absent "$RELATIVE_BUILD_DIR_TEST/test-outputs/stale/old-file"

# Exercise a build directory outside ./build and verify that install still puts
# the binary in the requested DESTDIR layout.
say "Build and install from a build directory outside ./build"
sh "$ROOT_DIR/configure" --build-dir="$OUTSIDE_BUILD" --profile=release
assert_dir_exists "$OUTSIDE_BUILD"

make -C "$OUTSIDE_BUILD"
assert_file_exists "$OUTSIDE_BUILD/bin/imgneko"

make -C "$OUTSIDE_BUILD" DESTDIR="$INSTALL_ROOT" install
assert_file_exists "$INSTALL_ROOT/usr/local/bin/imgneko"

# Ask make to build from a directory that was never configured so the Makefile
# emits its missing-config guidance.
say "Makefile error when config.mk is missing"
run_capture "$LOG_DIR/make-missing-config.out" make -C "$ROOT_DIR" BUILD_DIR="$MISSING_CONFIG_BUILD"
assert_status_nonzero
assert_output_contains "error: $MISSING_CONFIG_BUILD/config.mk does not exist"
assert_output_contains "run ./configure --build-dir='$MISSING_CONFIG_BUILD' first"

# Build from a copied repository after making configure newer than config.mk so
# the staleness error path blocks the build.
say "Makefile error when config.mk is older than configure"
copy_repo "$STALE_REPO"
sh "$STALE_REPO/configure" --build-dir="$STALE_REPO/build/stale"
sleep 1
touch "$STALE_REPO/configure"

run_capture "$LOG_DIR/stale-build.out" make -C "$STALE_REPO/build/stale"
assert_status_nonzero
assert_output_contains "error: $STALE_REPO/build/stale/config.mk is older than $STALE_REPO/configure. Reconfigure or touch config.mk"
assert_path_absent "$STALE_REPO/build/stale/bin/imgneko"

# Remove VERSION in a copied repository so the top-level Makefile parse-time
# check fails before any target logic runs.
say "Makefile parse error when VERSION is missing"
copy_repo "$NO_VERSION_REPO"
rm -f "$NO_VERSION_REPO/VERSION"

run_capture "$LOG_DIR/make-missing-version.out" make -C "$NO_VERSION_REPO" help
assert_status_nonzero
assert_output_contains "Missing required VERSION file"

# Trigger configure's unknown-option error directly.
say "configure error: unknown option"
run_capture "$LOG_DIR/cfg-unknown-option.out" sh "$ROOT_DIR/configure" --definitely-unknown
assert_status_nonzero
assert_output_contains "error: unknown option or assignment: --definitely-unknown"

# Use -- to force configure into its unexpected-positional-arguments path.
say "configure error: unexpected positional argument"
run_capture "$LOG_DIR/cfg-unexpected-positional.out" sh "$ROOT_DIR/configure" -- unexpected
assert_status_nonzero
assert_output_contains "error: unexpected positional arguments"

# Select a profile that does not exist.
say "configure error: unknown profile"
run_capture "$LOG_DIR/cfg-unknown-profile.out" sh "$ROOT_DIR/configure" --profile=bogus
assert_status_nonzero
assert_output_contains "error: unknown profile: bogus"

# Pass an invalid compile-database toggle and verify the dedicated validation
# error.
say "configure error: invalid COMP_DB_MJ"
run_capture "$LOG_DIR/cfg-bad-compdb.out" sh "$ROOT_DIR/configure" --build-dir="$INVALID_COMPDB_BUILD" COMP_DB_MJ=MAYBE
assert_status_nonzero
assert_output_contains "error: COMP_DB_MJ must be ON or OFF (got: MAYBE)"

# Pass an invalid depfile-generation toggle and verify the dedicated validation
# error.
say "configure error: invalid DEPFILES"
run_capture "$LOG_DIR/cfg-bad-depfiles.out" sh "$ROOT_DIR/configure" --build-dir="$INVALID_DEPFILES_BUILD" DEPFILES=MAYBE
assert_status_nonzero
assert_output_contains "error: DEPFILES must be ON or OFF (got: MAYBE)"

# Ask for --comp-db-mj with compiler flags that force the probe to fail and
# print a first stderr line, which should be echoed in the custom error.
say "configure error: --comp-db-mj with stderr output"
run_capture "$LOG_DIR/cfg-compdb-with-message.out" sh "$ROOT_DIR/configure" --build-dir="$COMPDB_WITH_MESSAGE_BUILD" --comp-db-mj --cc=gcc --cflags=--definitely-invalid-flag
assert_status_nonzero
assert_output_contains "error: --comp-db-mj requires a compiler that accepts -MJ (got:"

# Ask for --comp-db-mj with a command that fails without producing stderr so
# the shorter fallback error path is tested too.
say "configure error: --comp-db-mj without stderr output"
run_capture "$LOG_DIR/cfg-compdb-without-message.out" sh "$ROOT_DIR/configure" --build-dir="$COMPDB_WITHOUT_MESSAGE_BUILD" --comp-db-mj --cc=false
assert_status_nonzero
assert_output_contains "error: --comp-db-mj requires a compiler that accepts -MJ"
assert_output_not_contains "(got:"

# Ask for --depfiles with compiler flags that force the probe to fail and print
# a first stderr line, which should be echoed in the custom error.
say "configure error: --depfiles with stderr output"
run_capture "$LOG_DIR/cfg-depfiles-with-message.out" sh "$ROOT_DIR/configure" --build-dir="$DEPFILES_WITH_MESSAGE_BUILD" --depfiles --cc=gcc --cflags=--definitely-invalid-flag
assert_status_nonzero
assert_output_contains "error: --depfiles requires a compiler that accepts -MMD -MP -MT -MF (got:"

# Ask for --depfiles with a command that fails without producing stderr so the
# shorter fallback error path is tested too.
say "configure error: --depfiles without stderr output"
run_capture "$LOG_DIR/cfg-depfiles-without-message.out" sh "$ROOT_DIR/configure" --build-dir="$DEPFILES_WITHOUT_MESSAGE_BUILD" --depfiles --cc=false
assert_status_nonzero
assert_output_contains "error: --depfiles requires a compiler that accepts -MMD -MP -MT -MF"
assert_output_not_contains "(got:"

# Enable compile_commands.json generation with clang and verify that
# non-test builds keep test entries out, while test-list brings them in.
say "compile_commands.json excludes test runner after make all, includes it after test-list"
sh "$ROOT_DIR/configure" --build-dir="$COMPDB_CLANG_BUILD" --cc=clang --comp-db-mj

run_capture "$LOG_DIR/compdb-make-all.out" make -C "$COMPDB_CLANG_BUILD" all
assert_status_zero
assert_file_exists "$COMPDB_CLANG_BUILD/bin/imgneko"
assert_path_absent "$COMPDB_CLANG_BUILD/bin/test-runner"
assert_path_absent "$COMPDB_CLANG_BUILD/obj/test-bin"
assert_file_exists "$COMPDB_CLANG_BUILD/compile_commands.json"
assert_file_contains "$COMPDB_CLANG_BUILD/compile_commands.json" "src/main.c"
assert_file_not_contains "$COMPDB_CLANG_BUILD/compile_commands.json" "test-runner.c"
assert_file_not_contains "$COMPDB_CLANG_BUILD/compile_commands.json" "/tests/"

run_capture "$LOG_DIR/compdb-test-list.out" make -C "$COMPDB_CLANG_BUILD" test-list
assert_status_zero
assert_file_exists "$COMPDB_CLANG_BUILD/compile_commands.json"
assert_file_contains "$COMPDB_CLANG_BUILD/compile_commands.json" "test-runner.c"

# Regenerate the checked-in dependency file in a copied repository so the real
# checkout stays untouched, then confirm a normal build consumes it to rebuild
# on header changes without configure's depfile mode.
say "Regenerate checked-in dependency file and reuse it in a normal build"
copy_repo "$DEPFILES_REPO"
DEPFILES_BUILD=$DEPFILES_REPO/build/depfiles
NORMAL_DEPS_BUILD=$DEPFILES_REPO/build/normal

sh "$DEPFILES_REPO/configure" --build-dir="$DEPFILES_BUILD" --depfiles
run_capture "$LOG_DIR/depfile-generate.out" make -C "$DEPFILES_BUILD" depfile
assert_status_zero
assert_file_exists "$DEPFILES_REPO/mk/dependencies.mk"
assert_file_contains "$DEPFILES_REPO/mk/dependencies.mk" '$(BUILD_DIR)/obj/src/util/path.o:'
assert_file_contains "$DEPFILES_REPO/mk/dependencies.mk" '$(BUILD_DIR)/generated/build_info.h'
assert_file_contains "$DEPFILES_REPO/mk/dependencies.mk" '$(ROOT_DIR)/src/util/path.h'
assert_file_not_contains "$DEPFILES_REPO/mk/dependencies.mk" "$DEPFILES_REPO"

sh "$DEPFILES_REPO/configure" --build-dir="$NORMAL_DEPS_BUILD"
run_capture "$LOG_DIR/depfile-normal-baseline.out" make -C "$NORMAL_DEPS_BUILD" test-list
assert_status_zero

touch "$DEPFILES_REPO/src/util/path.h"
run_capture "$LOG_DIR/depfile-normal-rebuild.out" make -C "$NORMAL_DEPS_BUILD" test-list
assert_status_zero
assert_output_contains "src/util/path.c"
assert_output_contains "testing/support/test-runner.c"
assert_output_contains "testing/tests/unit/util/path.c"
assert_output_not_contains "src/main.c"

# Reject build directories whose path includes whitespace before any files are
# written there.
say "configure error: build dir contains whitespace"
run_capture "$LOG_DIR/cfg-builddir-whitespace.out" sh "$ROOT_DIR/configure" "--build-dir=$SPACE_BUILD"
assert_status_nonzero
assert_output_contains "error: BUILD_DIR must not contain whitespace ($SPACE_BUILD)"

# Reject a build dir that resolves to the repository root itself.
say "configure error: build dir resolves to project root"
run_capture "$LOG_DIR/cfg-builddir-root.out" sh "$ROOT_DIR/configure" --build-dir=.
assert_status_nonzero
assert_output_contains "error: BUILD_DIR must not resolve to ROOT_DIR ($ROOT_DIR)"

# Configure and build once so there is an existing executable, then prove that
# changing the saved configuration is rejected unless --force is supplied.
say "configure error: existing config differs without --force"
sh "$ROOT_DIR/configure" --build-dir="$RECONFIGURE_BUILD" --profile=default
make -C "$RECONFIGURE_BUILD"
assert_file_exists "$RECONFIGURE_BUILD/bin/imgneko"

run_capture "$LOG_DIR/force-before-version.out" "$RECONFIGURE_BUILD/bin/imgneko" --version
assert_status_zero
assert_output_contains "profile: default"

run_capture "$LOG_DIR/cfg-force-error.out" sh "$ROOT_DIR/configure" --build-dir="$RECONFIGURE_BUILD" --profile=release
assert_status_nonzero
assert_output_contains "error: $RECONFIGURE_BUILD/config.mk already exists and differs; rerun with --force to replace it"

# Re-run configure with --force, rebuild in the same directory, and verify that
# the executable now reports the new profile and release flags via --version.
say "configure --force reconfigures and rebuilds"
sh "$ROOT_DIR/configure" --force --build-dir="$RECONFIGURE_BUILD" --profile=release

run_capture "$LOG_DIR/force-rebuild.out" make -C "$RECONFIGURE_BUILD"
assert_status_zero
assert_output_contains "-DNDEBUG"

run_capture "$LOG_DIR/force-after-version.out" "$RECONFIGURE_BUILD/bin/imgneko" --version
assert_status_zero
assert_output_contains "profile: release"
assert_output_contains "-DNDEBUG"

# Copy the repository into a directory with spaces so configure rejects the
# project root path before doing any other work.
say "configure error: project root contains whitespace"
copy_repo "$SPACE_ROOT_REPO"

run_capture "$LOG_DIR/cfg-root-whitespace.out" sh "$SPACE_ROOT_REPO/configure"
assert_status_nonzero
assert_output_contains "error: ROOT_DIR must not contain whitespace ($SPACE_ROOT_REPO)"

say "All makefile/configure scenarios passed."
