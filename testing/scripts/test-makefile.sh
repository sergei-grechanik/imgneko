#!/bin/sh
# SPDX-License-Identifier: MIT-0

# This is a test for the Makefile and the configure script. It runs a variety of
# scenarios that cover the expected use cases and error paths.

set -eu

ROOT_DIR=$(CDPATH= cd "$(dirname "$0")/../.." && pwd)
SOURCE_ROOT=${IMGNEKO_TEST_MAKEFILE_SOURCE_ROOT:-$ROOT_DIR}

# Copy only git-tracked files from the source working tree into a fresh
# destination. This keeps local build artifacts out of test repos while still
# including any uncommitted edits to tracked files.
copy_tracked_repo() {
    destination=$1

    mkdir -p "$destination"
    (
        cd "$SOURCE_ROOT" &&
        git ls-files -z | tar --null -T - -cf -
    ) | (
        cd "$destination" &&
        tar -xf -
    )
}

if [ "${IMGNEKO_TEST_MAKEFILE_ISOLATED:-0}" != 1 ]; then
    ISOLATED_ROOT=$(mktemp -d /tmp/imgneko-makefile-test-repo.XXXXXX)
    ISOLATED_REPO=$ISOLATED_ROOT/repo

    copy_tracked_repo "$ISOLATED_REPO"

    set +e
    (
        cd "$ISOLATED_REPO" &&
        IMGNEKO_TEST_MAKEFILE_ISOLATED=1 \
        IMGNEKO_TEST_MAKEFILE_SOURCE_ROOT="$ROOT_DIR" \
            sh "$ISOLATED_REPO/testing/scripts/test-makefile.sh"
    )
    status=$?
    set -e

    rm -rf "$ISOLATED_ROOT"
    exit "$status"
fi

DEFAULT_BUILD=$ROOT_DIR/build/default
CUSTOM_BUILD=$ROOT_DIR/build/test-debug-custom-cc
DEV_BUILD=$ROOT_DIR/build/test-dev-profile
MISSING_CONFIG_BUILD=$ROOT_DIR/build/test-missing-config
INVALID_FEATURE_BUILD=$ROOT_DIR/build/test-invalid-feature
INVALID_COMPDB_BUILD=$ROOT_DIR/build/test-invalid-compdb
INVALID_COVERAGE_BUILD=$ROOT_DIR/build/test-invalid-coverage
INVALID_DEPFILES_BUILD=$ROOT_DIR/build/test-invalid-depfiles
INVALID_TEST_JOBS_BUILD=$ROOT_DIR/build/test-invalid-test-jobs
COMPDB_WITH_MESSAGE_BUILD=$ROOT_DIR/build/test-compdb-with-message
COMPDB_WITHOUT_MESSAGE_BUILD=$ROOT_DIR/build/test-compdb-without-message
COVERAGE_WITH_MESSAGE_BUILD=$ROOT_DIR/build/test-coverage-with-message
COVERAGE_WITHOUT_MESSAGE_BUILD=$ROOT_DIR/build/test-coverage-without-message
DEPFILES_WITH_MESSAGE_BUILD=$ROOT_DIR/build/test-depfiles-with-message
DEPFILES_WITHOUT_MESSAGE_BUILD=$ROOT_DIR/build/test-depfiles-without-message
COMPDB_CLANG_BUILD=$ROOT_DIR/build/test-compdb-clang
COVERAGE_BUILD=$ROOT_DIR/build/test-coverage
COVERAGE_TEST_FILTER='runner/output.sh|unit/util/path.c/append_segment'
SPACE_BUILD="$ROOT_DIR/build/test bad dir"
RECONFIGURE_BUILD=$ROOT_DIR/build/test-reconfigure-check
RELATIVE_BUILD_DIR_TEST=$ROOT_DIR/build/test-relative-builddir
TEST_JOBS_BUILD=$ROOT_DIR/build/test-jobs-default

# Use one temporary root for scenarios that intentionally leave the project
# tree, and clean it up on exit.
TMP_TEST_ROOT=$(mktemp -d /tmp/imgneko-makefile-test-artifacts.XXXXXX)
LOG_DIR=$TMP_TEST_ROOT/logs
OUTSIDE_BUILD=$TMP_TEST_ROOT/outside-build
INSTALL_ROOT=$TMP_TEST_ROOT/install-root
STALE_REPO=$TMP_TEST_ROOT/stale-repo
NO_VERSION_REPO=$TMP_TEST_ROOT/no-version-repo
DEPFILES_REPO=$TMP_TEST_ROOT/depfiles-repo
SPACE_ROOT_REPO="$TMP_TEST_ROOT/root with spaces"
NO_BUILD_REPO=$TMP_TEST_ROOT/no-build-repo
UNIQUE_BUILD_REPO=$TMP_TEST_ROOT/unique-build-repo
AMBIGUOUS_BUILD_REPO=$TMP_TEST_ROOT/ambiguous-build-repo
COVERAGE_IGNORE_REPO=$TMP_TEST_ROOT/coverage-ignore-repo

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

# Verify that a file does not match a forbidden regular expression.
assert_file_not_matches() {
    path=$1
    pattern=$2

    if grep -E -- "$pattern" "$path" >/dev/null 2>&1; then
        printf '%s\n' "Expected file not to match: $pattern" >&2
        printf '%s\n' "Actual file: $path" >&2
        sed -n '1,200p' "$path" >&2
        exit 1
    fi
}

# Verify that a file matches a required regular expression.
assert_file_matches() {
    path=$1
    pattern=$2

    if ! grep -E -- "$pattern" "$path" >/dev/null 2>&1; then
        printf '%s\n' "Expected file to match: $pattern" >&2
        printf '%s\n' "Actual file: $path" >&2
        sed -n '1,200p' "$path" >&2
        exit 1
    fi
}

# Remove coverage suppressions from the copied test-runner so the test can
# force visible uncovered quickfix entries without mutating the source tree.
unsuppress_coverage_test_probes() {
    if ! grep -Fqx "testing/tools/test-runner.c coverage_ignore_*" "$ROOT_DIR/coverage-ignore"; then
        fail "missing expected coverage-ignore rule for test-runner.c"
    fi

    sed -i '/^testing\/tools\/test-runner\.c coverage_ignore_\*$/d' \
        "$ROOT_DIR/coverage-ignore"
    sed -i '/IMGNEKO_UNCOVERED_OK/d' "$ROOT_DIR/testing/tools/test-runner.c"
}

# Narrow the copied repo's coverage recipe so this Makefile test runs only a
# small representative subset instead of the whole test suite.
restrict_coverage_run_to_subset() {
    if ! grep -Fqx '	@LLVM_PROFILE_FILE="$(COVERAGE_PROFILE_DIR)/%m-%p.profraw" "$(BIN_TEST_RUNNER)" -j "$(TEST_RUNNER_JOBS)" --all' "$ROOT_DIR/Makefile"; then
        fail "missing expected coverage test-runner recipe in $ROOT_DIR/Makefile"
    fi

    sed -i "/^\$(COVERAGE_TESTS_STAMP):/,/COMPILE_DB_REFRESH/ s#--all#--filter '$COVERAGE_TEST_FILTER'#" \
        "$ROOT_DIR/Makefile"
}

# Verify that two strings are exactly equal.
assert_equal() {
    expected=$1
    actual=$2

    if [ "$expected" != "$actual" ]; then
        fail "expected '$expected', got '$actual'"
    fi
}

# Verify that the actual integer is strictly greater than the expected one.
assert_greater() {
    smaller=$1
    larger=$2

    if [ "$larger" -le "$smaller" ]; then
        fail "expected integer greater than $smaller, got $larger"
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

# Copy a clean tracked-file snapshot for tests that need to mutate top-level
# files such as configure or VERSION without touching the active test repo.
copy_repo() {
    destination=$1

    copy_tracked_repo "$destination"
}

trap cleanup EXIT

assert_path_absent "$LOG_DIR"
assert_path_absent "$DEFAULT_BUILD"
assert_path_absent "$CUSTOM_BUILD"
assert_path_absent "$DEV_BUILD"
assert_path_absent "$MISSING_CONFIG_BUILD"
assert_path_absent "$INVALID_FEATURE_BUILD"
assert_path_absent "$INVALID_COMPDB_BUILD"
assert_path_absent "$INVALID_COVERAGE_BUILD"
assert_path_absent "$INVALID_DEPFILES_BUILD"
assert_path_absent "$INVALID_TEST_JOBS_BUILD"
assert_path_absent "$COMPDB_WITH_MESSAGE_BUILD"
assert_path_absent "$COMPDB_WITHOUT_MESSAGE_BUILD"
assert_path_absent "$COVERAGE_WITH_MESSAGE_BUILD"
assert_path_absent "$COVERAGE_WITHOUT_MESSAGE_BUILD"
assert_path_absent "$DEPFILES_WITH_MESSAGE_BUILD"
assert_path_absent "$DEPFILES_WITHOUT_MESSAGE_BUILD"
assert_path_absent "$COMPDB_CLANG_BUILD"
assert_path_absent "$COVERAGE_BUILD"
assert_path_absent "$SPACE_BUILD"
assert_path_absent "$RECONFIGURE_BUILD"
assert_path_absent "$RELATIVE_BUILD_DIR_TEST"
assert_path_absent "$TEST_JOBS_BUILD"
mkdir -p "$LOG_DIR"

# Verify the fully default path: no --build-dir, no profile override, and a
# normal build/run from build/default.
say "Default configure/build/run"
sh "$ROOT_DIR/configure"
assert_file_exists "$DEFAULT_BUILD/config.mk"
assert_file_exists "$DEFAULT_BUILD/Makefile"
assert_file_exists "$DEFAULT_BUILD/configure.cmd"
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

# The dev profile is the CI/development configuration: ASan plus every
# generation-oriented build feature enabled by default.
say "Dev profile enables ASan, coverage, compile database fragments, and depfiles"
sh "$ROOT_DIR/configure" --build-dir="$DEV_BUILD" --profile=dev
assert_file_contains "$DEV_BUILD/config.mk" "override PROFILE = dev"
assert_file_contains "$DEV_BUILD/config.mk" "override CC = clang"
assert_file_contains "$DEV_BUILD/config.mk" "-fsanitize=address,undefined"
assert_file_contains "$DEV_BUILD/config.mk" "override COMP_DB_MJ = ON"
assert_file_contains "$DEV_BUILD/config.mk" "override COVERAGE_REPORT = ON"
assert_file_contains "$DEV_BUILD/config.mk" "override DEPFILES = ON"

# In a fresh repository copy with no build directories yet, plain root-level
# make should direct the user to configure first instead of inventing a default
# build path on its own.
say "Root make requires configure when no build directories exist"
copy_repo "$NO_BUILD_REPO"

run_capture "$LOG_DIR/root-make-no-builds.out" make -C "$NO_BUILD_REPO"
assert_status_nonzero
assert_output_contains "error: no build directories found under $NO_BUILD_REPO/build"
assert_output_contains "run ./configure first"

# In a fresh repository copy with only one build directory under ./build, plain
# root-level make should select that directory even before it is configured.
say "Root make auto-selects a unique non-default build directory"
copy_repo "$UNIQUE_BUILD_REPO"
UNIQUE_BUILD=$UNIQUE_BUILD_REPO/build/solo
mkdir -p "$UNIQUE_BUILD"

run_capture "$LOG_DIR/root-make-unique-missing-config.out" make -C "$UNIQUE_BUILD_REPO"
assert_status_nonzero
assert_output_contains "error: ./build/solo/config.mk does not exist"
assert_output_contains "run ./configure --build-dir='./build/solo' first"
assert_path_absent "$UNIQUE_BUILD_REPO/build/default"

sh "$UNIQUE_BUILD_REPO/configure" --build-dir="$UNIQUE_BUILD" --profile=asan

run_capture "$LOG_DIR/root-make-unique.out" make -C "$UNIQUE_BUILD_REPO"
assert_status_zero
assert_file_exists "$UNIQUE_BUILD/bin/imgneko"
assert_path_absent "$UNIQUE_BUILD_REPO/build/default"

# With multiple build directories under ./build, plain root-level make must ask
# users to disambiguate even if neither directory is configured yet.
say "Root make requires explicit disambiguation with multiple build directories"
copy_repo "$AMBIGUOUS_BUILD_REPO"
mkdir -p "$AMBIGUOUS_BUILD_REPO/build/one" "$AMBIGUOUS_BUILD_REPO/build/two"

run_capture "$LOG_DIR/root-make-ambiguous.out" make -C "$AMBIGUOUS_BUILD_REPO"
assert_status_nonzero
assert_output_contains "pass BUILD_DIR=<path> explicitly"

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
assert_output_contains "error: depfile generation is disabled in ./build/default/config.mk; rerun ./configure --build-dir='./build/default' --depfiles"

# Reject `make coverage` unless configure explicitly enabled coverage
# instrumentation for that build directory.
say "Makefile error when coverage report generation is disabled"
run_capture "$LOG_DIR/make-coverage-disabled.out" make -C "$DEFAULT_BUILD" coverage
assert_status_nonzero
assert_output_contains "error: coverage report generation is disabled in ./build/default/config.mk; rerun ./configure --build-dir='./build/default' --coverage-report"

# Build test prerequisites without running them.
say "Build test dependencies without executing tests"
run_capture "$LOG_DIR/make-test-deps.out" make -C "$DEFAULT_BUILD" test-deps
assert_status_zero
assert_file_exists "$DEFAULT_BUILD/bin/test-runner"
assert_file_exists "$DEFAULT_BUILD/obj/test-bin/unit/util/path.c.bin"
assert_output_not_contains "RUN:"

# Configure a build with a non-default compiled-in parallelism and verify the
# runner help reflects that saved default.
say "Configure default test jobs"
sh "$ROOT_DIR/configure" --build-dir="$TEST_JOBS_BUILD" --test-jobs=3
run_capture "$LOG_DIR/make-test-jobs-tools.out" make -C "$TEST_JOBS_BUILD" test-tools
assert_status_zero
run_capture "$LOG_DIR/test-jobs-help.out" "$TEST_JOBS_BUILD/bin/test-runner" --help
assert_status_zero
assert_output_contains "Run up to JOBS tests concurrently. (default: 3)"

# The top-level `make test` target should accept both JOBS and PARALLEL as the
# user-facing override knobs for test-runner parallelism.
say "Make test accepts JOBS and PARALLEL"
run_capture "$LOG_DIR/make-test-jobs-var.out" make -C "$DEFAULT_BUILD" test FILTER=runner/output.sh JOBS=2
assert_status_zero
assert_output_contains "discovered: 1"
assert_output_contains "passed: 1"
run_capture "$LOG_DIR/make-test-parallel-var.out" make -C "$DEFAULT_BUILD" test FILTER=runner/output.sh PARALLEL=2
assert_status_zero
assert_output_contains "discovered: 1"
assert_output_contains "passed: 1"

# A relative BUILD_DIR with a trailing slash should normalize to the same
# absolute build directory so test-runner env vars remain stable, and `make
# test` should also clear the default output tree before running tests.
say "Root make test accepts relative BUILD_DIR with trailing slash"
sh "$ROOT_DIR/configure" --build-dir="$RELATIVE_BUILD_DIR_TEST"
mkdir -p "$RELATIVE_BUILD_DIR_TEST/test-outputs/stale"
printf '%s\n' stale >"$RELATIVE_BUILD_DIR_TEST/test-outputs/stale/old-file"
run_capture "$LOG_DIR/root-make-relative-builddir.out" make -C "$ROOT_DIR" test BUILD_DIR=build/test-relative-builddir/ FILTER=runner/environment.sh
assert_status_zero
assert_output_contains "discovered: 1"
assert_output_contains "passed: 1"
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
assert_output_contains "error: ./build/test-missing-config/config.mk does not exist"
assert_output_contains "run ./configure --build-dir='./build/test-missing-config' first"

# Build from a copied repository after making configure newer than config.mk so
# the staleness error path blocks the build.
say "Makefile error when config.mk is older than configure"
copy_repo "$STALE_REPO"
sh "$STALE_REPO/configure" --build-dir="$STALE_REPO/build/stale"
sleep 1
touch "$STALE_REPO/configure"

run_capture "$LOG_DIR/stale-build.out" make -C "$STALE_REPO/build/stale"
assert_status_nonzero
assert_output_contains "error: ./build/stale/config.mk is older than ./configure. Reconfigure or touch config.mk"
assert_output_contains "rerun: ./configure --build-dir=./build/stale --force"
assert_path_absent "$STALE_REPO/build/stale/bin/imgneko"

# clean-test-output should still work when the saved configuration is stale so
# users can discard stale test logs before rerunning configure.
mkdir -p "$STALE_REPO/build/stale/bin" "$STALE_REPO/build/stale/test-outputs"
touch "$STALE_REPO/build/stale/bin/imgneko" "$STALE_REPO/build/stale/test-outputs/stale.log"

run_capture "$LOG_DIR/stale-clean-test-output.out" make -C "$STALE_REPO/build/stale" clean-test-output
assert_status_zero
assert_file_exists "$STALE_REPO/build/stale/bin/imgneko"
assert_path_absent "$STALE_REPO/build/stale/test-outputs/stale.log"

mkdir -p "$STALE_REPO/build/stale/test-outputs"
touch "$STALE_REPO/build/stale/test-outputs/stale.log"

# clean should keep delegating to clean-test-output while also removing other
# build outputs when the saved configuration is stale.
run_capture "$LOG_DIR/stale-clean.out" make -C "$STALE_REPO/build/stale" clean
assert_status_zero
assert_path_absent "$STALE_REPO/build/stale/bin/imgneko"
assert_path_absent "$STALE_REPO/build/stale/test-outputs/stale.log"

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

# Pass an invalid coverage-report toggle and verify the dedicated validation
# error.
say "configure error: invalid COVERAGE_REPORT"
run_capture "$LOG_DIR/cfg-bad-coverage.out" sh "$ROOT_DIR/configure" --build-dir="$INVALID_COVERAGE_BUILD" COVERAGE_REPORT=MAYBE
assert_status_nonzero
assert_output_contains "error: COVERAGE_REPORT must be ON or OFF (got: MAYBE)"

# Pass an invalid depfile-generation toggle and verify the dedicated validation
# error.
say "configure error: invalid DEPFILES"
run_capture "$LOG_DIR/cfg-bad-depfiles.out" sh "$ROOT_DIR/configure" --build-dir="$INVALID_DEPFILES_BUILD" DEPFILES=MAYBE
assert_status_nonzero
assert_output_contains "error: DEPFILES must be ON or OFF (got: MAYBE)"

# Pass an invalid default test parallelism and verify the dedicated validation
# error.
say "configure error: invalid TEST_JOBS"
run_capture "$LOG_DIR/cfg-bad-test-jobs.out" sh "$ROOT_DIR/configure" --build-dir="$INVALID_TEST_JOBS_BUILD" TEST_JOBS=0
assert_status_nonzero
assert_output_contains "error: TEST_JOBS must be a positive integer (got: 0)"

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

# Ask for --coverage-report with compiler flags that force the coverage probe
# to fail and print a first stderr line, which should be echoed in the custom
# error.
say "configure error: --coverage-report with stderr output"
run_capture "$LOG_DIR/cfg-coverage-with-message.out" sh "$ROOT_DIR/configure" --build-dir="$COVERAGE_WITH_MESSAGE_BUILD" --coverage-report --cc=clang --cflags=--definitely-invalid-flag
assert_status_nonzero
assert_output_contains "error: --coverage-report requires a Clang-compatible compiler and linker that accept -fprofile-instr-generate -fcoverage-mapping (got:"

# Ask for --coverage-report with a command that fails without producing stderr
# so the shorter fallback error path is tested too.
say "configure error: --coverage-report without stderr output"
run_capture "$LOG_DIR/cfg-coverage-without-message.out" sh "$ROOT_DIR/configure" --build-dir="$COVERAGE_WITHOUT_MESSAGE_BUILD" --coverage-report --cc=false
assert_status_nonzero
assert_output_contains "error: --coverage-report requires a Clang-compatible compiler and linker that accept -fprofile-instr-generate -fcoverage-mapping"
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

# Enable coverage reporting, patch the copied Makefile to run only a tiny
# representative subset, and confirm the resulting report still reflects the
# configured mode.
say "Coverage report generation writes an incremental summary for instrumented source files"
restrict_coverage_run_to_subset
sh "$ROOT_DIR/configure" --build-dir="$COVERAGE_BUILD" --profile=debug --cc=clang --coverage-report

run_capture "$LOG_DIR/coverage-report-missing.out" make -C "$COVERAGE_BUILD" coverage-report
assert_status_nonzero
assert_output_contains "error: coverage report inputs are missing in ./build/test-coverage/coverage; rerun make coverage first"

run_capture "$LOG_DIR/coverage-build.out" make -C "$COVERAGE_BUILD" coverage
assert_status_zero
assert_output_contains "Wrote ./build/test-coverage/coverage/summary.txt"
assert_output_contains "Wrote ./build/test-coverage/coverage/uncovered.qf"
assert_file_exists "$COVERAGE_BUILD/coverage/summary.txt"
assert_file_exists "$COVERAGE_BUILD/coverage/coverage.profdata"
assert_file_exists "$COVERAGE_BUILD/coverage/uncovered.qf"
assert_file_exists "$COVERAGE_BUILD/coverage/tests.stamp"
assert_file_contains "$COVERAGE_BUILD/coverage/summary.txt" "File 'src/main.c'"
assert_file_contains "$COVERAGE_BUILD/coverage/summary.txt" "File 'src/util/path.c'"
assert_file_contains "$COVERAGE_BUILD/coverage/summary.txt" "File 'testing/tools/test-runner.c'"
assert_file_contains "$COVERAGE_BUILD/coverage/summary.txt" "File 'testing/tests/unit/util/path.c'"
assert_file_contains "$COVERAGE_BUILD/coverage/summary.txt" "Lines executed:"
assert_file_contains "$COVERAGE_BUILD/coverage/summary.txt" "Branches covered:"
assert_file_contains "$COVERAGE_BUILD/coverage/summary.txt" "Uncovered locations:"

coverage_initial_mtime=$(stat -c %Y "$COVERAGE_BUILD/coverage/uncovered.qf")
coverage_tests_initial_mtime=$(stat -c %Y "$COVERAGE_BUILD/coverage/tests.stamp")
sleep 1
run_capture "$LOG_DIR/coverage-repeat.out" make -C "$COVERAGE_BUILD" coverage
assert_status_zero
coverage_repeat_mtime=$(stat -c %Y "$COVERAGE_BUILD/coverage/uncovered.qf")
assert_greater "$coverage_initial_mtime" "$coverage_repeat_mtime"
coverage_tests_repeat_mtime=$(stat -c %Y "$COVERAGE_BUILD/coverage/tests.stamp")
assert_equal "$coverage_tests_initial_mtime" "$coverage_tests_repeat_mtime"

sleep 1
touch "$ROOT_DIR/testing/tests/runner/test-runner-cli.sh"
run_capture "$LOG_DIR/coverage-rerun-on-test-change.out" make -C "$COVERAGE_BUILD" coverage
assert_status_zero
coverage_changed_mtime=$(stat -c %Y "$COVERAGE_BUILD/coverage/uncovered.qf")
coverage_tests_changed_mtime=$(stat -c %Y "$COVERAGE_BUILD/coverage/tests.stamp")
assert_greater "$coverage_tests_repeat_mtime" "$coverage_tests_changed_mtime"

sleep 1
run_capture "$LOG_DIR/coverage-report-repeat.out" make -C "$COVERAGE_BUILD" coverage-report
assert_status_zero
coverage_report_repeat_mtime=$(stat -c %Y "$COVERAGE_BUILD/coverage/uncovered.qf")
coverage_tests_report_mtime=$(stat -c %Y "$COVERAGE_BUILD/coverage/tests.stamp")
assert_greater "$coverage_changed_mtime" "$coverage_report_repeat_mtime"
assert_equal "$coverage_tests_changed_mtime" "$coverage_tests_report_mtime"

# The checked-in suppressions may leave `uncovered.qf` empty. Remove the
# copied repo's ignore rule and inline suppression comments from test-runner.c,
# then regenerate the report and verify concrete quickfix entries.
sleep 1
unsuppress_coverage_test_probes
run_capture "$LOG_DIR/coverage-report-unsuppressed.out" make -C "$COVERAGE_BUILD" coverage-report
assert_status_zero
assert_file_contains "$COVERAGE_BUILD/coverage/uncovered.qf" "uncovered line"
assert_file_contains "$COVERAGE_BUILD/coverage/uncovered.qf" "branch not fully covered"
assert_file_contains "$COVERAGE_BUILD/coverage/uncovered.qf" "function never executed:"
assert_file_contains "$COVERAGE_BUILD/coverage/uncovered.qf" "function never executed: coverage_ignore_probe"
assert_file_contains "$COVERAGE_BUILD/coverage/uncovered.qf" "function never executed: uncovered_ok_range_probe"
assert_file_contains "$COVERAGE_BUILD/coverage/uncovered.qf" "function never executed: uncovered_ok_count_probe"
assert_file_contains "$COVERAGE_BUILD/coverage/uncovered.qf" "return arg;"
assert_file_contains "$COVERAGE_BUILD/coverage/uncovered.qf" "return false;"

sleep 1
touch "$ROOT_DIR/coverage-ignore"
run_capture "$LOG_DIR/coverage-report-rebuild.out" make -C "$COVERAGE_BUILD" coverage-report
assert_status_zero
coverage_report_rebuild_mtime=$(stat -c %Y "$COVERAGE_BUILD/coverage/uncovered.qf")
coverage_tests_rebuild_mtime=$(stat -c %Y "$COVERAGE_BUILD/coverage/tests.stamp")
assert_greater "$coverage_report_repeat_mtime" "$coverage_report_rebuild_mtime"
assert_equal "$coverage_tests_changed_mtime" "$coverage_tests_rebuild_mtime"

run_capture "$LOG_DIR/coverage-filter-error.out" make -C "$COVERAGE_BUILD" coverage FILTER=runner/expectations.sh
assert_status_nonzero
assert_output_contains "error: make coverage does not support FILTER; rerun without FILTER"

run_capture "$LOG_DIR/coverage-report-filter-error.out" make -C "$COVERAGE_BUILD" coverage-report FILTER=runner/expectations.sh
assert_status_nonzero
assert_output_contains "error: make coverage-report does not support FILTER; rerun without FILTER"

run_capture "$LOG_DIR/coverage-version.out" "$COVERAGE_BUILD/bin/imgneko" --version
assert_status_zero
assert_output_contains "coverage_report: ON"

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
assert_output_contains "testing/tools/test-runner.c"
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
