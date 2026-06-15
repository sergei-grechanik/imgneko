#!/usr/bin/env run-and-check
# SPDX-License-Identifier: MIT-0
# RUN: sh %s

# Additional tests for test-runner CLI parsing and option handling.

set -eu

fail() {
    echo "$1" >&2
    exit 1
}

if [ -z "${IMGNEKO_TEST_OUTPUT_DIR:-}" ] ||
   [ ! -d "$IMGNEKO_TEST_OUTPUT_DIR" ]; then
    fail "IMGNEKO_TEST_OUTPUT_DIR is not set to an existing directory"
fi

RUNNER=$IMGNEKO_BUILD_DIR/bin/test-runner
TESTS_ROOT=$IMGNEKO_ROOT_DIR/testing/tests
NO_SUBTESTS_ABS_PATH=$TESTS_ROOT/runner/no-subtests.c
NO_SUBTESTS_REPO_PATH=testing/tests/runner/no-subtests.c
NO_SUBTESTS_PATH=$NO_SUBTESTS_ABS_PATH
OUTPUT_C_SUBTEST_PATH=$TESTS_ROOT/runner/output.c/emit_output
OPTION_TEST_DIR=$IMGNEKO_TEST_OUTPUT_DIR/cli-tests-dir
EMPTY_OUTPUT_DIR=$IMGNEKO_TEST_OUTPUT_DIR/existing-empty-output
PATH_OUTPUT_DIR=$IMGNEKO_TEST_OUTPUT_DIR/path-output
JOBS_OUTPUT_DIR=$IMGNEKO_TEST_OUTPUT_DIR/jobs-output
PASSTHROUGH_OUTPUT_DIR=$IMGNEKO_TEST_OUTPUT_DIR/passthrough-output
FLIP_OUTPUT_DIR=$IMGNEKO_TEST_OUTPUT_DIR/flip-output
XPASS_OUTPUT_DIR=$IMGNEKO_TEST_OUTPUT_DIR/xpass-output
NO_MATCH_OUTPUT_DIR=$IMGNEKO_TEST_OUTPUT_DIR/no-match-output
NONEMPTY_OUTPUT_DIR=$IMGNEKO_TEST_OUTPUT_DIR/nonempty-output
BAD_CWD_PARENT=$IMGNEKO_TEST_OUTPUT_DIR/bad-cwd-parent
BAD_CWD_CHILD=$BAD_CWD_PARENT/child
BAD_TEST_CWD_PARENT=$IMGNEKO_TEST_OUTPUT_DIR/bad-test-cwd-parent
BAD_TEST_CWD_CHILD=$BAD_TEST_CWD_PARENT/child
OUTSIDE_CWD_DIR=$IMGNEKO_TEST_OUTPUT_DIR/outside-cwd
FIFO_TEST_PATH=$IMGNEKO_TEST_OUTPUT_DIR/fifo-test-path
OUTSIDE_BLOCKED_TEST_PATH_DIR=$IMGNEKO_TEST_OUTPUT_DIR/outside-blocked-test-path-dir
BLOCKED_TEST_PATH_DIR=$OPTION_TEST_DIR/blocked-test-path-dir
REPO_PARENT=$(dirname "$IMGNEKO_ROOT_DIR")

[ -x "$RUNNER" ] || fail "missing test-runner binary: $RUNNER"
mkdir -p "$OPTION_TEST_DIR" "$EMPTY_OUTPUT_DIR" "$NONEMPTY_OUTPUT_DIR" \
    "$OUTSIDE_CWD_DIR" "$OUTSIDE_BLOCKED_TEST_PATH_DIR" \
    "$BLOCKED_TEST_PATH_DIR"
mkfifo "$FIFO_TEST_PATH"

cat >"$OPTION_TEST_DIR/custom.sh" <<'EOF'
#!/bin/sh
exit 0
EOF
chmod +x "$OPTION_TEST_DIR/custom.sh"

cat >"$OPTION_TEST_DIR/show-path.sh" <<'EOF'
#!/bin/sh
if [ "$PATH" = "$IMGNEKO_BUILD_DIR/bin" ]; then
    echo 'PATH ok'
    exit 0
fi
echo "PATH bad: $PATH"
exit 1
EOF
chmod +x "$OPTION_TEST_DIR/show-path.sh"
touch "$OPTION_TEST_DIR/not-a-test.txt"
touch "$OUTSIDE_CWD_DIR/custom.sh"
mkdir -p "$OPTION_TEST_DIR/dir" "$OPTION_TEST_DIR/fake.c" \
    "$OPTION_TEST_DIR/shadow"
echo 'stale output' >"$NONEMPTY_OUTPUT_DIR/stale"

cat >"$OPTION_TEST_DIR/dir/nested.sh" <<'EOF'
#!/bin/sh
exit 0
EOF
chmod +x "$OPTION_TEST_DIR/dir/nested.sh"

cat >"$OPTION_TEST_DIR/shadow/custom.sh" <<'EOF'
#!/bin/sh
exit 0
EOF
chmod +x "$OPTION_TEST_DIR/shadow/custom.sh"

echo '== help =='
"$RUNNER" --help 2>&1
# CHECK: == help ==
# CHECK-NEXT: Usage: test-runner [options] [--] [PATTERN...]
# CHECK: Positional arguments:
# CHECK: PATTERN...
# CHECK: Options:
# CHECK: --list                    List matching tests without running them.
# CHECK: --all                     Run the entire discovered test set.
# CHECK: -j, --jobs JOBS
# CHECK: --output-dir, --out-dir DIR
# CHECK: --tests-dir DIR
# CHECK: --test-bin-dir DIR
# CHECK: --timeout SECONDS
# CHECK: -p, --output-passthrough

echo '== short help =='
"$RUNNER" -h 2>&1
# CHECK: == short help ==
# CHECK-NEXT: Usage: test-runner [options] [--] [PATTERN...]

echo '== jobs short option =='
"$RUNNER" --output-dir "$JOBS_OUTPUT_DIR" -j 2 \
    runner/output.sh 2>&1
# CHECK: == jobs short option ==
# CHECK: RUN: runner/output.sh
# CHECK: PASS: runner/output.sh

echo '== jobs compact short option =='
"$RUNNER" --list -j2 "$NO_SUBTESTS_PATH" 2>&1
# CHECK: == jobs compact short option ==
# CHECK-NEXT: runner/no-subtests.c

echo '== jobs long option equals =='
"$RUNNER" --list --jobs=2 "$NO_SUBTESTS_PATH" 2>&1
# CHECK: == jobs long option equals ==
# CHECK-NEXT: runner/no-subtests.c

# Verify that an absolute test file path selects that test.
echo '== absolute path =='
"$RUNNER" --list "$NO_SUBTESTS_ABS_PATH" 2>&1
# CHECK: == absolute path ==
# CHECK-NEXT: runner/no-subtests.c

# Verify that a cwd-relative path under the repository selects the same test.
echo '== relative path =='
(
    cd "$IMGNEKO_ROOT_DIR"
    "$RUNNER" --list "./$NO_SUBTESTS_REPO_PATH" 2>&1
)
# CHECK: == relative path ==
# CHECK-NEXT: runner/no-subtests.c

# Verify that a tests-dir-relative path pattern works when the invocation cwd
# is the tests directory.
echo '== tests cwd relative path =='
(
    cd "$TESTS_ROOT"
    "$RUNNER" --list ./runner/no-subtests.c 2>&1
)
# CHECK: == tests cwd relative path ==
# CHECK-NEXT: runner/no-subtests.c

# Verify that a test-name pattern works regardless of the invocation cwd.
echo '== name pattern =='
"$RUNNER" --list runner/no-subtests.c 2>&1
# CHECK: == name pattern ==
# CHECK-NEXT: runner/no-subtests.c

echo '== option equals =='
"$RUNNER" --list --output-dir="$EMPTY_OUTPUT_DIR" \
    --test-bin-dir="$IMGNEKO_BUILD_DIR/obj/test-bin" \
    --timeout=0 runner/spaced-subtests.c 2>&1
# CHECK: == option equals ==
# CHECK: runner/spaced-subtests.c/marked_disabled DISABLED
# CHECK: runner/spaced-subtests.c/marked_xfail XFAIL
# CHECK: runner/spaced-subtests.c/plain_spaced

echo '== explicit test bin dir =='
"$RUNNER" --list --test-bin-dir "$IMGNEKO_BUILD_DIR/obj/test-bin" \
    "$NO_SUBTESTS_PATH" 2>&1
# CHECK: == explicit test bin dir ==
# CHECK-NEXT: runner/no-subtests.c

echo '== explicit tests dir =='
"$RUNNER" --list --tests-dir "$OPTION_TEST_DIR" custom.sh 2>&1
# CHECK: == explicit tests dir ==
# CHECK-NEXT: custom.sh

# Verify that a nested cwd-relative path selects the expected sibling test.
echo '== nested cwd relative path =='
(
    cd "$OPTION_TEST_DIR/dir"
    "$RUNNER" --list --tests-dir "$OPTION_TEST_DIR" ../custom.sh 2>&1
)
echo '== end =='
# CHECK: == nested cwd relative path ==
# CHECK-NEXT: custom.sh
# CHECK-NEXT: == end ==

# Verify that a relative path is not also interpreted relative to --tests-dir.
echo '== relative path uses cwd only =='
(
    cd "$OPTION_TEST_DIR/shadow"
    "$RUNNER" --list --tests-dir "$OPTION_TEST_DIR" ./custom.sh 2>&1
)
echo '== end =='
# CHECK: == relative path uses cwd only ==
# CHECK-NEXT: shadow/custom.sh
# CHECK-NEXT: == end ==

# Verify that --tests-dir does not make relative paths match from that directory.
echo '== tests-dir path not used from outside cwd =='
set +e
(
    cd "$OUTSIDE_CWD_DIR"
    "$RUNNER" --list --tests-dir "$OPTION_TEST_DIR" ./custom.sh 2>&1
)
printf 'status=%d\n' "$?"
set -e
# CHECK: == tests-dir path not used from outside cwd ==
# CHECK: error: no tests matched pattern: './custom.sh'
# CHECK: status=2

# Verify that a directory wildcard pattern selects tests below that directory.
echo '== directory wildcard pattern =='
(
    cd "$OPTION_TEST_DIR"
    "$RUNNER" --list --tests-dir "$OPTION_TEST_DIR" 'dir/*' 2>&1
)
# CHECK: == directory wildcard pattern ==
# CHECK-NEXT: dir/nested.sh

# Verify that a cwd-relative wildcard selects tests below the invocation cwd.
echo '== cwd root wildcard pattern =='
(
    cd "$OPTION_TEST_DIR"
    "$RUNNER" --list --tests-dir "$OPTION_TEST_DIR" './*' 2>&1
)
# CHECK: == cwd root wildcard pattern ==
# CHECK-NEXT: custom.sh
# CHECK-NEXT: dir/nested.sh
# CHECK-NEXT: shadow/custom.sh
# CHECK-NEXT: show-path.sh

# Verify that a C subtest can be selected by appending the subtest name to the
# C test file path.
echo '== c subtest path =='
"$RUNNER" --list "$OUTPUT_C_SUBTEST_PATH" 2>&1
# CHECK: == c subtest path ==
# CHECK-NEXT: runner/output.c/emit_output

# Verify that multiple positional patterns are ORed.
echo '== multiple patterns =='
"$RUNNER" --list runner/no-subtests.c runner/output.sh 2>&1
# CHECK: == multiple patterns ==
# CHECK-NEXT: runner/no-subtests.c
# CHECK-NEXT: runner/output.sh

# Verify that an existing non-test path pattern reports that no tests matched.
echo '== path selects no tests =='
set +e
"$RUNNER" --list --tests-dir "$OPTION_TEST_DIR" \
    "$OPTION_TEST_DIR/not-a-test.txt" 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK: == path selects no tests ==
# CHECK: error: no tests matched pattern: '{{.*not-a-test.txt}}'
# CHECK: status=2

# Verify that an existing C test with an unknown subtest reports no match.
echo '== missing c subtest =='
set +e
(
    cd "$TESTS_ROOT"
    "$RUNNER" --list runner/output.c/not_a_subtest 2>&1
)
printf 'status=%d\n' "$?"
set -e
# CHECK: == missing c subtest ==
# CHECK: error: no tests matched pattern: '{{.*output.c/not_a_subtest}}'
# CHECK: status=2

# Verify that a trailing slash after a C file path is normalized to the test
# file instead of being treated as a subtest selector.
echo '== c test trailing slash path =='
"$RUNNER" --list "$TESTS_ROOT/runner/output.c/" 2>&1
# CHECK: == c test trailing slash path ==
# CHECK-NEXT: runner/output.c/emit_output

# Verify that a C subtest path whose file does not exist reports no match.
echo '== missing c subtest file =='
set +e
"$RUNNER" --list "$TESTS_ROOT/runner/missing.c/subtest" 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK: == missing c subtest file ==
# CHECK: error: no tests matched pattern: '{{.*missing.c/subtest}}'
# CHECK: status=2

# Verify that `.c` must end a path component before the remaining path is
# interpreted as a C subtest.
echo '== absolute c path non boundary =='
set +e
"$RUNNER" --list "$TESTS_ROOT/runner/output.c-extra/subtest" 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK: == absolute c path non boundary ==
# CHECK: error: no tests matched pattern: '{{.*output.c-extra/subtest}}'
# CHECK: status=2

# Verify that a directory named like a C file is not interpreted as a C test.
echo '== c subtest prefix is directory =='
set +e
(
    cd "$OPTION_TEST_DIR"
    "$RUNNER" --list --tests-dir "$OPTION_TEST_DIR" fake.c/subtest 2>&1
)
printf 'status=%d\n' "$?"
set -e
# CHECK: == c subtest prefix is directory ==
# CHECK: error: no tests matched pattern: 'fake.c/subtest'
# CHECK: status=2

# Verify that a short unmatched relative path reports no path match.
echo '== short missing path =='
set +e
(
    cd "$OPTION_TEST_DIR"
    "$RUNNER" --list --tests-dir "$OPTION_TEST_DIR" x 2>&1
)
printf 'status=%d\n' "$?"
set -e
# CHECK: == short missing path ==
# CHECK: error: no tests matched pattern: 'x'
# CHECK: status=2

# Verify that a trailing slash on a missing non-C path reports no path match.
echo '== non c trailing slash path =='
set +e
(
    cd "$OPTION_TEST_DIR"
    "$RUNNER" --list --tests-dir "$OPTION_TEST_DIR" abx/ 2>&1
)
printf 'status=%d\n' "$?"
set -e
# CHECK: == non c trailing slash path ==
# CHECK: error: no tests matched pattern: 'abx/'
# CHECK: status=2

# Verify that a trailing slash on a missing C file path reports no path match.
echo '== missing c file trailing slash path =='
set +e
(
    cd "$OPTION_TEST_DIR"
    "$RUNNER" --list --tests-dir "$OPTION_TEST_DIR" ab.c/ 2>&1
)
printf 'status=%d\n' "$?"
set -e
# CHECK: == missing c file trailing slash path ==
# CHECK: error: no tests matched pattern: 'ab.c/'
# CHECK: status=2

# Verify that dotted non-C paths are not interpreted as virtual subtest paths.
echo '== non c dotted virtual path =='
set +e
(
    cd "$OPTION_TEST_DIR"
    "$RUNNER" --list --tests-dir "$OPTION_TEST_DIR" missing.x/subtest 2>&1
)
printf 'status=%d\n' "$?"
set -e
# CHECK: == non c dotted virtual path ==
# CHECK: error: no tests matched pattern: 'missing.x/subtest'
# CHECK: status=2

# Verify that unsupported filesystem nodes are treated as unmatched path
# patterns, not as test files.
echo '== unsupported path type =='
set +e
"$RUNNER" --list --tests-dir "$IMGNEKO_TEST_OUTPUT_DIR" \
    "$FIFO_TEST_PATH" 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK: == unsupported path type ==
# CHECK: error: no tests matched pattern: '{{.*fifo-test-path}}'
# CHECK: status=2

# Verify that an absolute path outside the tests directory reports no match,
# even when part of the path cannot be searched.
chmod 000 "$OUTSIDE_BLOCKED_TEST_PATH_DIR"
echo '== outside test path =='
set +e
"$RUNNER" --list "$OUTSIDE_BLOCKED_TEST_PATH_DIR/child" 2>&1
printf 'status=%d\n' "$?"
set -e
chmod 700 "$OUTSIDE_BLOCKED_TEST_PATH_DIR"
# CHECK: == outside test path ==
# CHECK: error: no tests matched pattern: '{{.*outside-blocked-test-path-dir/child}}'
# CHECK: status=2

# Verify that a cwd-relative path outside the tests directory reports no match
# without walking an unreadable outside directory.
chmod 000 "$OUTSIDE_BLOCKED_TEST_PATH_DIR"
echo '== relative blocked outside test path =='
set +e
(
    cd "$IMGNEKO_TEST_OUTPUT_DIR"
    "$RUNNER" --list outside-blocked-test-path-dir/child 2>&1
)
printf 'status=%d\n' "$?"
set -e
chmod 700 "$OUTSIDE_BLOCKED_TEST_PATH_DIR"
# CHECK: == relative blocked outside test path ==
# CHECK: error: no tests matched pattern: 'outside-blocked-test-path-dir/child'
# CHECK: status=2

# Verify that a cwd-relative path inside an unreadable test directory reaches
# normal discovery and reports the directory-open failure.
chmod 000 "$BLOCKED_TEST_PATH_DIR"
echo '== blocked test path =='
set +e
(
    cd "$OPTION_TEST_DIR"
    "$RUNNER" --list --tests-dir "$OPTION_TEST_DIR" \
        blocked-test-path-dir/child 2>&1
)
printf 'status=%d\n' "$?"
set -e
chmod 700 "$BLOCKED_TEST_PATH_DIR"
# CHECK: == blocked test path ==
# CHECK: error: failed to open tests directory: Permission denied
# CHECK: status=1

# Empty PATH should not append a trailing separator.
echo '== empty PATH =='
env PATH= "$RUNNER" --tests-dir "$OPTION_TEST_DIR" \
    --output-dir "$PATH_OUTPUT_DIR" --output-passthrough \
    show-path.sh 2>&1
# CHECK: == empty PATH ==
# CHECK: RUN: show-path.sh
# CHECK: PATH ok
# CHECK: PASS: show-path.sh

echo '== passthrough long option =='
"$RUNNER" --output-dir "$EMPTY_OUTPUT_DIR" --output-passthrough \
    runner/output.sh 2>&1
# CHECK: == passthrough long option ==
# CHECK: RUN: runner/output.sh
# CHECK: output script stdout marker
# CHECK: output script stderr marker
# CHECK: PASS: runner/output.sh
# CHECK: Result: SUCCESS

echo '== debug flip arg =='
"$RUNNER" --output-dir "$FLIP_OUTPUT_DIR" \
    --debug-flip-exit-probability 0.5 \
    runner/output.sh 2>&1 || true
# CHECK: == debug flip arg ==
# CHECK: RUN: runner/output.sh
# CHECK: {{PASS|FAIL}}: runner/output.sh
# CHECK: Summary:
# CHECK: discovered: 1
# CHECK: {{passed|failed}}: 1
# CHECK: Result: {{SUCCESS|FAILURE}}

echo '== debug flip failing test =='
"$RUNNER" --output-dir "$XPASS_OUTPUT_DIR" \
    --debug-flip-exit-probability=1 \
    runner/xfail.sh 2>&1 || true
# CHECK: == debug flip failing test ==
# CHECK: RUN: runner/xfail.sh
# CHECK: DEBUG: flipped exit code for runner/xfail.sh (1 -> 0)
# CHECK: XPASS: runner/xfail.sh
# CHECK: xpassed tests:
# CHECK: runner/xfail.sh
# CHECK: Summary:
# CHECK: discovered: 1
# CHECK: xpassed: 1
# CHECK: Result: FAILURE

echo '== missing output dir =='
"$RUNNER" --output-dir 2>&1 || true
# CHECK: == missing output dir ==
# CHECK: error: --output-dir requires a value

echo '== missing jobs =='
set +e
"$RUNNER" -j 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK: == missing jobs ==
# CHECK: error: --jobs requires a value
# CHECK: status=2

echo '== jobs text =='
"$RUNNER" --jobs nope 2>&1 || true
# CHECK: == jobs text ==
# CHECK: error: invalid value for --jobs: 'nope'

echo '== jobs zero =='
"$RUNNER" --jobs=0 2>&1 || true
# CHECK: == jobs zero ==
# CHECK: error: invalid value for --jobs: '0'

echo '== jobs trailing junk =='
"$RUNNER" -j2x 2>&1 || true
# CHECK: == jobs trailing junk ==
# CHECK: error: invalid value for --jobs: '2x'

echo '== jobs too large =='
"$RUNNER" --jobs=2147483648 2>&1 || true
# CHECK: == jobs too large ==
# CHECK: error: invalid value for --jobs: '2147483648'

echo '== jobs huge =='
"$RUNNER" --jobs=999999999999999999999999999999 2>&1 || true
# CHECK: == jobs huge ==
# CHECK: error: invalid value for --jobs: '999999999999999999999999999999'

echo '== removed filter option =='
set +e
"$RUNNER" --filter 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK: == removed filter option ==
# CHECK: error: unknown option: '--filter'
# CHECK: status=2

echo '== removed short filter option =='
set +e
"$RUNNER" -f 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK: == removed short filter option ==
# CHECK: error: unknown option: '-f'
# CHECK: status=2

echo '== missing tests dir =='
"$RUNNER" --tests-dir 2>&1 || true
# CHECK: == missing tests dir ==
# CHECK: error: --tests-dir requires a value

echo '== missing test bin dir =='
"$RUNNER" --test-bin-dir 2>&1 || true
# CHECK: == missing test bin dir ==
# CHECK: error: --test-bin-dir requires a value

echo '== missing timeout =='
"$RUNNER" --timeout 2>&1 || true
# CHECK: == missing timeout ==
# CHECK: error: --timeout requires a value

echo '== timeout text =='
"$RUNNER" --timeout nope 2>&1 || true
# CHECK: == timeout text ==
# CHECK: error: invalid value for --timeout: 'nope'

echo '== timeout trailing junk =='
"$RUNNER" --timeout=1x 2>&1 || true
# CHECK: == timeout trailing junk ==
# CHECK: error: invalid value for --timeout: '1x'

echo '== timeout negative =='
"$RUNNER" --timeout -1 2>&1 || true
# CHECK: == timeout negative ==
# CHECK: error: invalid value for --timeout: '-1'

echo '== timeout huge =='
"$RUNNER" --timeout 1e5000 2>&1 || true
# CHECK: == timeout huge ==
# CHECK: error: invalid value for --timeout: '1e5000'

echo '== missing debug flip =='
"$RUNNER" --debug-flip-exit-probability 2>&1 || true
# CHECK: == missing debug flip ==
# CHECK: error: --debug-flip-exit-probability requires a value

echo '== debug flip text =='
"$RUNNER" --debug-flip-exit-probability nope 2>&1 || true
# CHECK: == debug flip text ==
# CHECK: error: invalid value for --debug-flip-exit-probability: 'nope'

echo '== debug flip negative =='
"$RUNNER" --debug-flip-exit-probability=-1 2>&1 || true
# CHECK: == debug flip negative ==
# CHECK: error: invalid value for --debug-flip-exit-probability: '-1'

echo '== debug flip trailing junk =='
"$RUNNER" --debug-flip-exit-probability=1x 2>&1 || true
# CHECK: == debug flip trailing junk ==
# CHECK: error: invalid value for --debug-flip-exit-probability: '1x'

echo '== debug flip too large =='
"$RUNNER" --debug-flip-exit-probability=2 2>&1 || true
# CHECK: == debug flip too large ==
# CHECK: error: invalid value for --debug-flip-exit-probability: '2'

echo '== debug flip huge =='
"$RUNNER" --debug-flip-exit-probability 1e5000 2>&1 || true
# CHECK: == debug flip huge ==
# CHECK: error: invalid value for --debug-flip-exit-probability: '1e5000'

echo '== missing debug parent setpgid delay =='
"$RUNNER" --debug-parent-setpgid-delay 2>&1 || true
# CHECK: == missing debug parent setpgid delay ==
# CHECK: error: --debug-parent-setpgid-delay requires a value

echo '== debug parent setpgid delay text =='
"$RUNNER" --debug-parent-setpgid-delay nope 2>&1 || true
# CHECK: == debug parent setpgid delay text ==
# CHECK: error: invalid value for --debug-parent-setpgid-delay: 'nope'

echo '== debug parent setpgid delay negative =='
"$RUNNER" --debug-parent-setpgid-delay=-1 2>&1 || true
# CHECK: == debug parent setpgid delay negative ==
# CHECK: error: invalid value for --debug-parent-setpgid-delay: '-1'

echo '== debug parent setpgid delay trailing junk =='
"$RUNNER" --debug-parent-setpgid-delay=1x 2>&1 || true
# CHECK: == debug parent setpgid delay trailing junk ==
# CHECK: error: invalid value for --debug-parent-setpgid-delay: '1x'

echo '== debug parent setpgid delay huge =='
"$RUNNER" --debug-parent-setpgid-delay 1e5000 2>&1 || true
# CHECK: == debug parent setpgid delay huge ==
# CHECK: error: invalid value for --debug-parent-setpgid-delay: '1e5000'

echo '== debug parent output chunk delay equals =='
"$RUNNER" --list --debug-parent-output-chunk-delay=0 \
    "$NO_SUBTESTS_PATH" 2>&1
# CHECK: == debug parent output chunk delay equals ==
# CHECK-NEXT: runner/no-subtests.c

echo '== missing debug parent output chunk delay =='
"$RUNNER" --debug-parent-output-chunk-delay 2>&1 || true
# CHECK: == missing debug parent output chunk delay ==
# CHECK: error: --debug-parent-output-chunk-delay requires a value

echo '== debug parent output chunk delay text =='
"$RUNNER" --debug-parent-output-chunk-delay nope 2>&1 || true
# CHECK: == debug parent output chunk delay text ==
# CHECK: error: invalid value for --debug-parent-output-chunk-delay: 'nope'

echo '== debug parent output chunk delay negative =='
"$RUNNER" --debug-parent-output-chunk-delay=-1 2>&1 || true
# CHECK: == debug parent output chunk delay negative ==
# CHECK: error: invalid value for --debug-parent-output-chunk-delay: '-1'

echo '== unknown option =='
set +e
"$RUNNER" --definitely-unknown 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK: == unknown option ==
# CHECK: error: unknown option: '--definitely-unknown'
# CHECK: status=2

echo '== all with pattern =='
"$RUNNER" --all runner/no-subtests.c 2>&1 || true
# CHECK: == all with pattern ==
# CHECK: error: --all cannot be combined with patterns

# Verify that --all rejects absolute path patterns too.
echo '== all with path =='
"$RUNNER" --all "$NO_SUBTESTS_PATH" 2>&1 || true
# CHECK: == all with path ==
# CHECK: error: --all cannot be combined with patterns

echo '== empty pattern parts =='
set +e
"$RUNNER" --list '||runner/no-subtests.c|' 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK: == empty pattern parts ==
# CHECK: error: invalid test pattern: '||runner/no-subtests.c|'
# CHECK: status=2

# Pattern diagnostics should escape control bytes from command-line arguments
# so a malformed pattern cannot split the error message across lines.
echo '== escaped empty pattern parts =='
BAD_ALT_PATTERN=$(printf 'bad\n|')
set +e
"$RUNNER" --list "$BAD_ALT_PATTERN" 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK: == escaped empty pattern parts ==
# CHECK: error: invalid test pattern: 'bad<LF>|'
# CHECK: status=2

# Verify that every alternation atom must match at least one test.
echo '== unmatched alternation part =='
set +e
"$RUNNER" --list 'does/not/exist|runner/no-subtests.c' 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK: == unmatched alternation part ==
# CHECK: error: no tests matched pattern: 'does/not/exist'
# CHECK: status=2

echo '== escaped no matches =='
NEWLINE_PATTERN=$(printf 'does\nnot/exist')
set +e
"$RUNNER" --list "$NEWLINE_PATTERN" 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK: == escaped no matches ==
# CHECK: error: no tests matched pattern: 'does<LF>not/exist'
# CHECK: status=2

echo '== list no matches =='
set +e
"$RUNNER" --list does/not/exist 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK: == list no matches ==
# CHECK: error: no tests matched pattern: 'does/not/exist'
# CHECK: status=2

echo '== run no matches =='
set +e
"$RUNNER" --output-dir "$NO_MATCH_OUTPUT_DIR" does/not/exist 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK: == run no matches ==
# CHECK: error: no tests matched pattern: 'does/not/exist'
# CHECK: status=2

echo '== nonempty output dir =='
"$RUNNER" --output-dir "$NONEMPTY_OUTPUT_DIR" \
    runner/output.sh 2>&1 || true
# CHECK: == nonempty output dir ==
# CHECK: error: test output directory is not empty: {{.*nonempty-output}}
# CHECK: remove it first with rm -rf '{{.*nonempty-output}}'

echo '== unsafe slash =='
"$RUNNER" --output-dir / "$NO_SUBTESTS_PATH" 2>&1 || true
# CHECK: == unsafe slash ==
# CHECK: error: unsafe output directory: /

echo '== unsafe repo =='
"$RUNNER" --output-dir "$IMGNEKO_ROOT_DIR" "$NO_SUBTESTS_PATH" 2>&1 || true
# CHECK: == unsafe repo ==
# CHECK-NEXT: error: unsafe output directory: {{.+}}

echo '== unsafe parent =='
"$RUNNER" --output-dir "$REPO_PARENT" "$NO_SUBTESTS_PATH" 2>&1 || true
# CHECK: == unsafe parent ==
# CHECK-NEXT: error: unsafe output directory: {{.+}}

echo '== unsafe build =='
"$RUNNER" --output-dir "$IMGNEKO_BUILD_DIR" "$NO_SUBTESTS_PATH" 2>&1 || true
# CHECK: == unsafe build ==
# CHECK-NEXT: error: unsafe output directory: {{.+}}

mkdir -p "$BAD_CWD_CHILD"
echo '== relative output dir from deleted cwd =='
set +e
(
    cd "$BAD_CWD_CHILD"
    rmdir "$BAD_CWD_CHILD"
    "$RUNNER" --list --output-dir relative-output \
        runner/no-subtests.c 2>&1
)
printf 'status=%d\n' "$?"
set -e
# CHECK: == relative output dir from deleted cwd ==
# CHECK: error: failed to resolve output directory: No such file or directory
# CHECK: status=1

# Verify that a relative test pattern cannot be resolved when the invocation cwd
# has been deleted.
mkdir -p "$BAD_TEST_CWD_CHILD"
echo '== relative test path from deleted cwd =='
set +e
(
    cd "$BAD_TEST_CWD_CHILD"
    rmdir "$BAD_TEST_CWD_CHILD"
    "$RUNNER" --list "$NO_SUBTESTS_REPO_PATH" 2>&1
)
printf 'status=%d\n' "$?"
set -e
# CHECK: == relative test path from deleted cwd ==
# CHECK: error: failed to resolve test pattern: No such file or directory
# CHECK: status=1

echo "== --out-tmp =="
# Run a single fast test with --out-tmp to verify that the runner creates a
# temporary output directory and reports its path on completion.
"$RUNNER" --out-tmp "$NO_SUBTESTS_PATH" 2>&1
# CHECK: == --out-tmp ==
# CHECK: Output dir: /tmp/imgneko-test-{{.+}}
