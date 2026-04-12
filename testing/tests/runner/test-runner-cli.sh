#!/usr/bin/env run-and-check
# RUN: sh %s

# Additional tests for test-runner CLI parsing and option handling.

set -eu

fail() {
    echo "$1" >&2
    exit 1
}

RUNNER=$IMGNEKO_BUILD_DIR/bin/test-runner
OPTION_TEST_DIR=$IMGNEKO_TEST_OUTPUT_DIR/cli-tests-dir
EMPTY_OUTPUT_DIR=$IMGNEKO_TEST_OUTPUT_DIR/existing-empty-output
PATH_OUTPUT_DIR=$IMGNEKO_TEST_OUTPUT_DIR/path-output
JOBS_OUTPUT_DIR=$IMGNEKO_TEST_OUTPUT_DIR/jobs-output
PASSTHROUGH_OUTPUT_DIR=$IMGNEKO_TEST_OUTPUT_DIR/passthrough-output
FLIP_OUTPUT_DIR=$IMGNEKO_TEST_OUTPUT_DIR/flip-output
XPASS_OUTPUT_DIR=$IMGNEKO_TEST_OUTPUT_DIR/xpass-output
NO_MATCH_OUTPUT_DIR=$IMGNEKO_TEST_OUTPUT_DIR/no-match-output
NONEMPTY_OUTPUT_DIR=$IMGNEKO_TEST_OUTPUT_DIR/nonempty-output
REPO_PARENT=$(dirname "$IMGNEKO_ROOT_DIR")

[ -x "$RUNNER" ] || fail "missing test-runner binary: $RUNNER"
mkdir -p "$OPTION_TEST_DIR" "$EMPTY_OUTPUT_DIR" "$NONEMPTY_OUTPUT_DIR"

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
echo 'stale output' >"$NONEMPTY_OUTPUT_DIR/stale"

echo '== help =='
"$RUNNER" --help 2>&1
# CHECK: == help ==
# CHECK: Usage: test-runner [--list] [--all] [-j JOBS] [--output-dir DIR]
# CHECK: [--filter PATTERN] [--tests-dir DIR] [--test-bin-dir DIR]
# The help text should report whatever default job count configure compiled in.
# CHECK: Default jobs: {{[1-9][0-9]*}}
# CHECK: Default timeout: 180 seconds

echo '== short help =='
"$RUNNER" -h 2>&1
# CHECK: == short help ==
# CHECK: Usage: test-runner [--list] [--all] [-j JOBS] [--output-dir DIR]
# CHECK: [--filter PATTERN] [--tests-dir DIR] [--test-bin-dir DIR]

echo '== jobs short option =='
"$RUNNER" --output-dir "$JOBS_OUTPUT_DIR" -j 2 \
    --filter runner/output.sh 2>&1
# CHECK: == jobs short option ==
# CHECK: RUN: runner/output.sh
# CHECK: PASS: runner/output.sh

echo '== jobs compact short option =='
"$RUNNER" --list -j2 runner/no-subtests.c 2>&1
# CHECK: == jobs compact short option ==
# CHECK-NEXT: runner/no-subtests.c

echo '== jobs long option equals =='
"$RUNNER" --list --jobs=2 runner/no-subtests.c 2>&1
# CHECK: == jobs long option equals ==
# CHECK-NEXT: runner/no-subtests.c

echo '== positional filter =='
"$RUNNER" --list runner/no-subtests.c 2>&1
# CHECK: == positional filter ==
# CHECK-NEXT: runner/no-subtests.c

echo '== option equals =='
"$RUNNER" --list --output-dir="$EMPTY_OUTPUT_DIR" \
    --test-bin-dir="$IMGNEKO_BUILD_DIR/obj/test-bin" \
    --filter=runner/spaced-subtests.c --timeout=0 2>&1
# CHECK: == option equals ==
# CHECK: runner/spaced-subtests.c/marked_disabled DISABLED
# CHECK: runner/spaced-subtests.c/marked_xfail XFAIL
# CHECK: runner/spaced-subtests.c/plain_spaced

echo '== explicit test bin dir =='
"$RUNNER" --list --test-bin-dir "$IMGNEKO_BUILD_DIR/obj/test-bin" \
    runner/no-subtests.c 2>&1
# CHECK: == explicit test bin dir ==
# CHECK-NEXT: runner/no-subtests.c

echo '== explicit tests dir =='
"$RUNNER" --list --tests-dir "$OPTION_TEST_DIR" custom.sh 2>&1
# CHECK: == explicit tests dir ==
# CHECK-NEXT: custom.sh

# Empty PATH should not append a trailing separator.
echo '== empty PATH =='
env PATH= "$RUNNER" --tests-dir "$OPTION_TEST_DIR" \
    --output-dir "$PATH_OUTPUT_DIR" --output-passthrough \
    --filter show-path.sh 2>&1
# CHECK: == empty PATH ==
# CHECK: RUN: show-path.sh
# CHECK: PATH ok
# CHECK: PASS: show-path.sh

echo '== passthrough long option =='
"$RUNNER" --output-dir "$EMPTY_OUTPUT_DIR" --output-passthrough \
    --filter runner/output.sh 2>&1
# CHECK: == passthrough long option ==
# CHECK: RUN: runner/output.sh
# CHECK: output script stdout marker
# CHECK: output script stderr marker
# CHECK: PASS: runner/output.sh
# CHECK: Result: SUCCESS

echo '== debug flip arg =='
"$RUNNER" --output-dir "$FLIP_OUTPUT_DIR" \
    --debug-flip-exit-probability 0.5 \
    --filter runner/output.sh 2>&1 || true
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
    --filter runner/xfail.sh 2>&1 || true
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
# CHECK: Usage: test-runner [--list] [--all] [-j JOBS] [--output-dir DIR]

echo '== missing jobs =='
set +e
missing_jobs_output=$("$RUNNER" -j 2>&1)
missing_jobs_status=$?
set -e
printf '%s\n' "$missing_jobs_output"
printf 'status=%d\n' "$missing_jobs_status"
# CHECK: == missing jobs ==
# CHECK: error: --jobs requires a value
# CHECK: Usage: test-runner [--list] [--all] [-j JOBS] [--output-dir DIR]
# CHECK: status=2

echo '== jobs text =='
"$RUNNER" --jobs nope 2>&1 || true
# CHECK: == jobs text ==
# CHECK: error: invalid jobs value: nope
# CHECK: Usage: test-runner [--list] [--all] [-j JOBS] [--output-dir DIR]

echo '== jobs zero =='
"$RUNNER" --jobs=0 2>&1 || true
# CHECK: == jobs zero ==
# CHECK: error: invalid jobs value: 0
# CHECK: Usage: test-runner [--list] [--all] [-j JOBS] [--output-dir DIR]

echo '== jobs trailing junk =='
"$RUNNER" -j2x 2>&1 || true
# CHECK: == jobs trailing junk ==
# CHECK: error: invalid jobs value: 2x
# CHECK: Usage: test-runner [--list] [--all] [-j JOBS] [--output-dir DIR]

echo '== jobs too large =='
"$RUNNER" --jobs=2147483648 2>&1 || true
# CHECK: == jobs too large ==
# CHECK: error: invalid jobs value: 2147483648
# CHECK: Usage: test-runner [--list] [--all] [-j JOBS] [--output-dir DIR]

echo '== jobs huge =='
"$RUNNER" --jobs=999999999999999999999999999999 2>&1 || true
# CHECK: == jobs huge ==
# CHECK: error: invalid jobs value: 999999999999999999999999999999
# CHECK: Usage: test-runner [--list] [--all] [-j JOBS] [--output-dir DIR]

echo '== missing filter =='
"$RUNNER" --filter 2>&1 || true
# CHECK: == missing filter ==
# CHECK: Usage: test-runner [--list] [--all] [-j JOBS] [--output-dir DIR]

echo '== missing tests dir =='
"$RUNNER" --tests-dir 2>&1 || true
# CHECK: == missing tests dir ==
# CHECK: Usage: test-runner [--list] [--all] [-j JOBS] [--output-dir DIR]

echo '== missing test bin dir =='
"$RUNNER" --test-bin-dir 2>&1 || true
# CHECK: == missing test bin dir ==
# CHECK: Usage: test-runner [--list] [--all] [-j JOBS] [--output-dir DIR]

echo '== missing timeout =='
"$RUNNER" --timeout 2>&1 || true
# CHECK: == missing timeout ==
# CHECK: error: --timeout requires a value
# CHECK: Usage: test-runner [--list] [--all] [-j JOBS] [--output-dir DIR]

echo '== timeout text =='
"$RUNNER" --timeout nope 2>&1 || true
# CHECK: == timeout text ==
# CHECK: error: invalid --timeout value: nope
# CHECK: Usage: test-runner [--list] [--all] [-j JOBS] [--output-dir DIR]

echo '== timeout trailing junk =='
"$RUNNER" --timeout=1x 2>&1 || true
# CHECK: == timeout trailing junk ==
# CHECK: error: invalid --timeout value: 1x
# CHECK: Usage: test-runner [--list] [--all] [-j JOBS] [--output-dir DIR]

echo '== timeout negative =='
"$RUNNER" --timeout -1 2>&1 || true
# CHECK: == timeout negative ==
# CHECK: error: invalid --timeout value: -1
# CHECK: Usage: test-runner [--list] [--all] [-j JOBS] [--output-dir DIR]

echo '== timeout huge =='
"$RUNNER" --timeout 1e5000 2>&1 || true
# CHECK: == timeout huge ==
# CHECK: error: invalid --timeout value: 1e5000
# CHECK: Usage: test-runner [--list] [--all] [-j JOBS] [--output-dir DIR]

echo '== missing debug flip =='
"$RUNNER" --debug-flip-exit-probability 2>&1 || true
# CHECK: == missing debug flip ==
# CHECK: error: --debug-flip-exit-probability requires a value

echo '== debug flip text =='
"$RUNNER" --debug-flip-exit-probability nope 2>&1 || true
# CHECK: == debug flip text ==
# CHECK: error: invalid --debug-flip-exit-probability value: nope
# CHECK: Usage: test-runner [--list] [--all] [-j JOBS] [--output-dir DIR]

echo '== debug flip negative =='
"$RUNNER" --debug-flip-exit-probability=-1 2>&1 || true
# CHECK: == debug flip negative ==
# CHECK: error: invalid --debug-flip-exit-probability value: -1
# CHECK: Usage: test-runner [--list] [--all] [-j JOBS] [--output-dir DIR]

echo '== debug flip trailing junk =='
"$RUNNER" --debug-flip-exit-probability=1x 2>&1 || true
# CHECK: == debug flip trailing junk ==
# CHECK: error: invalid --debug-flip-exit-probability value: 1x
# CHECK: Usage: test-runner [--list] [--all] [-j JOBS] [--output-dir DIR]

echo '== debug flip too large =='
"$RUNNER" --debug-flip-exit-probability=2 2>&1 || true
# CHECK: == debug flip too large ==
# CHECK: error: invalid --debug-flip-exit-probability value: 2
# CHECK: Usage: test-runner [--list] [--all] [-j JOBS] [--output-dir DIR]

echo '== debug flip huge =='
"$RUNNER" --debug-flip-exit-probability 1e5000 2>&1 || true
# CHECK: == debug flip huge ==
# CHECK: error: invalid --debug-flip-exit-probability value: 1e5000
# CHECK: Usage: test-runner [--list] [--all] [-j JOBS] [--output-dir DIR]

echo '== missing debug parent setpgid delay =='
"$RUNNER" --debug-parent-setpgid-delay 2>&1 || true
# CHECK: == missing debug parent setpgid delay ==
# CHECK: error: --debug-parent-setpgid-delay requires a value

echo '== debug parent setpgid delay text =='
"$RUNNER" --debug-parent-setpgid-delay nope 2>&1 || true
# CHECK: == debug parent setpgid delay text ==
# CHECK: error: invalid --debug-parent-setpgid-delay value: nope
# CHECK: Usage: test-runner [--list] [--all] [-j JOBS] [--output-dir DIR]

echo '== debug parent setpgid delay negative =='
"$RUNNER" --debug-parent-setpgid-delay=-1 2>&1 || true
# CHECK: == debug parent setpgid delay negative ==
# CHECK: error: invalid --debug-parent-setpgid-delay value: -1
# CHECK: Usage: test-runner [--list] [--all] [-j JOBS] [--output-dir DIR]

echo '== debug parent setpgid delay trailing junk =='
"$RUNNER" --debug-parent-setpgid-delay=1x 2>&1 || true
# CHECK: == debug parent setpgid delay trailing junk ==
# CHECK: error: invalid --debug-parent-setpgid-delay value: 1x
# CHECK: Usage: test-runner [--list] [--all] [-j JOBS] [--output-dir DIR]

echo '== debug parent setpgid delay huge =='
"$RUNNER" --debug-parent-setpgid-delay 1e5000 2>&1 || true
# CHECK: == debug parent setpgid delay huge ==
# CHECK: error: invalid --debug-parent-setpgid-delay value: 1e5000
# CHECK: Usage: test-runner [--list] [--all] [-j JOBS] [--output-dir DIR]

echo '== debug parent output chunk delay equals =='
"$RUNNER" --list --debug-parent-output-chunk-delay=0 runner/no-subtests.c 2>&1
# CHECK: == debug parent output chunk delay equals ==
# CHECK-NEXT: runner/no-subtests.c

echo '== missing debug parent output chunk delay =='
"$RUNNER" --debug-parent-output-chunk-delay 2>&1 || true
# CHECK: == missing debug parent output chunk delay ==
# CHECK: error: --debug-parent-output-chunk-delay requires a value

echo '== debug parent output chunk delay text =='
"$RUNNER" --debug-parent-output-chunk-delay nope 2>&1 || true
# CHECK: == debug parent output chunk delay text ==
# CHECK: error: invalid --debug-parent-output-chunk-delay value: nope
# CHECK: Usage: test-runner [--list] [--all] [-j JOBS] [--output-dir DIR]

echo '== debug parent output chunk delay negative =='
"$RUNNER" --debug-parent-output-chunk-delay=-1 2>&1 || true
# CHECK: == debug parent output chunk delay negative ==
# CHECK: error: invalid --debug-parent-output-chunk-delay value: -1
# CHECK: Usage: test-runner [--list] [--all] [-j JOBS] [--output-dir DIR]

echo '== unknown option =='
set +e
unknown_option_output=$("$RUNNER" --definitely-unknown 2>&1)
unknown_option_status=$?
set -e
printf '%s\n' "$unknown_option_output"
printf 'status=%d\n' "$unknown_option_status"
# CHECK: == unknown option ==
# CHECK: error: unknown option: --definitely-unknown
# CHECK: Usage: test-runner [--list] [--all] [-j JOBS] [--output-dir DIR]
# CHECK: status=2

echo '== all with filter =='
"$RUNNER" --all runner/no-subtests.c 2>&1 || true
# CHECK: == all with filter ==
# CHECK: error: --all cannot be combined with --filter or positional patterns
# CHECK: Usage: test-runner [--list] [--all] [-j JOBS] [--output-dir DIR]

echo '== empty filter parts =='
set +e
empty_filter_output=$("$RUNNER" --list --filter '||runner/no-subtests.c|' 2>&1)
empty_filter_status=$?
set -e
printf '%s\n' "$empty_filter_output"
printf 'status=%d\n' "$empty_filter_status"
# CHECK: == empty filter parts ==
# CHECK: error: invalid filter pattern: ||runner/no-subtests.c|
# CHECK: status=2

echo '== list no matches =='
"$RUNNER" --list --filter does/not/exist 2>&1 || true
echo '== end list no matches =='
# CHECK: == list no matches ==
# CHECK-NEXT: == end list no matches ==

echo '== run no matches =='
set +e
"$RUNNER" --output-dir "$NO_MATCH_OUTPUT_DIR" --filter does/not/exist 2>&1
run_no_matches_status=$?
set -e
printf 'status=%d\n' "$run_no_matches_status"
# CHECK: == run no matches ==
# CHECK: error: no tests matched the requested filters
# CHECK: status=2

echo '== nonempty output dir =='
"$RUNNER" --output-dir "$NONEMPTY_OUTPUT_DIR" \
    --filter runner/output.sh 2>&1 || true
# CHECK: == nonempty output dir ==
# CHECK: error: test output directory is not empty: {{.*nonempty-output}}
# CHECK: remove it first with rm -rf '{{.*nonempty-output}}'

echo '== unsafe slash =='
"$RUNNER" --list --output-dir / 2>&1 || true
# CHECK: == unsafe slash ==
# CHECK: error: unsafe output directory: /

echo '== unsafe repo =='
"$RUNNER" --list --output-dir "$IMGNEKO_ROOT_DIR" 2>&1 || true
# CHECK: == unsafe repo ==
# CHECK-NEXT: error: unsafe output directory: {{.+}}

# Dropping the last character keeps this path as a plain string prefix of the
# repo root without making it a path-component prefix, so it must stay allowed.
SIMILAR_REPO_OUTPUT_DIR=${IMGNEKO_ROOT_DIR%?}
echo '== similar repo prefix =='
"$RUNNER" --list --output-dir "$SIMILAR_REPO_OUTPUT_DIR" \
    runner/no-subtests.c 2>&1
# CHECK: == similar repo prefix ==
# CHECK-NEXT: runner/no-subtests.c

echo '== unsafe parent =='
"$RUNNER" --list --output-dir "$REPO_PARENT" 2>&1 || true
# CHECK: == unsafe parent ==
# CHECK-NEXT: error: unsafe output directory: {{.+}}

echo '== unsafe build =='
"$RUNNER" --list --output-dir "$IMGNEKO_BUILD_DIR" 2>&1 || true
# CHECK: == unsafe build ==
# CHECK-NEXT: error: unsafe output directory: {{.+}}
