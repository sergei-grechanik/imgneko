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
# CHECK: Usage: test-runner [--list] [--all] [--output-dir DIR] [--filter PATTERN]
# CHECK: [--tests-dir DIR] [--test-bin-dir DIR]
# CHECK: Default timeout: 180 seconds

echo '== short help =='
"$RUNNER" -h 2>&1
# CHECK: == short help ==
# CHECK: Usage: test-runner [--list] [--all] [--output-dir DIR] [--filter PATTERN]
# CHECK: [--tests-dir DIR] [--test-bin-dir DIR]

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
# CHECK: Usage: test-runner [--list] [--all] [--output-dir DIR] [--filter PATTERN]

echo '== missing filter =='
"$RUNNER" --filter 2>&1 || true
# CHECK: == missing filter ==
# CHECK: Usage: test-runner [--list] [--all] [--output-dir DIR] [--filter PATTERN]

echo '== missing tests dir =='
"$RUNNER" --tests-dir 2>&1 || true
# CHECK: == missing tests dir ==
# CHECK: Usage: test-runner [--list] [--all] [--output-dir DIR] [--filter PATTERN]

echo '== missing test bin dir =='
"$RUNNER" --test-bin-dir 2>&1 || true
# CHECK: == missing test bin dir ==
# CHECK: Usage: test-runner [--list] [--all] [--output-dir DIR] [--filter PATTERN]

echo '== missing timeout =='
"$RUNNER" --timeout 2>&1 || true
# CHECK: == missing timeout ==
# CHECK: error: --timeout requires a value
# CHECK: Usage: test-runner [--list] [--all] [--output-dir DIR] [--filter PATTERN]

echo '== timeout text =='
"$RUNNER" --timeout nope 2>&1 || true
# CHECK: == timeout text ==
# CHECK: error: invalid --timeout value: nope
# CHECK: Usage: test-runner [--list] [--all] [--output-dir DIR] [--filter PATTERN]

echo '== timeout trailing junk =='
"$RUNNER" --timeout=1x 2>&1 || true
# CHECK: == timeout trailing junk ==
# CHECK: error: invalid --timeout value: 1x
# CHECK: Usage: test-runner [--list] [--all] [--output-dir DIR] [--filter PATTERN]

echo '== timeout negative =='
"$RUNNER" --timeout -1 2>&1 || true
# CHECK: == timeout negative ==
# CHECK: error: invalid --timeout value: -1
# CHECK: Usage: test-runner [--list] [--all] [--output-dir DIR] [--filter PATTERN]

echo '== timeout huge =='
"$RUNNER" --timeout 1e5000 2>&1 || true
# CHECK: == timeout huge ==
# CHECK: error: invalid --timeout value: 1e5000
# CHECK: Usage: test-runner [--list] [--all] [--output-dir DIR] [--filter PATTERN]

echo '== missing debug flip =='
"$RUNNER" --debug-flip-exit-probability 2>&1 || true
# CHECK: == missing debug flip ==
# CHECK: error: --debug-flip-exit-probability requires a value

echo '== debug flip text =='
"$RUNNER" --debug-flip-exit-probability nope 2>&1 || true
# CHECK: == debug flip text ==
# CHECK: error: invalid --debug-flip-exit-probability value: nope
# CHECK: Usage: test-runner [--list] [--all] [--output-dir DIR] [--filter PATTERN]

echo '== debug flip negative =='
"$RUNNER" --debug-flip-exit-probability=-1 2>&1 || true
# CHECK: == debug flip negative ==
# CHECK: error: invalid --debug-flip-exit-probability value: -1
# CHECK: Usage: test-runner [--list] [--all] [--output-dir DIR] [--filter PATTERN]

echo '== debug flip trailing junk =='
"$RUNNER" --debug-flip-exit-probability=1x 2>&1 || true
# CHECK: == debug flip trailing junk ==
# CHECK: error: invalid --debug-flip-exit-probability value: 1x
# CHECK: Usage: test-runner [--list] [--all] [--output-dir DIR] [--filter PATTERN]

echo '== debug flip too large =='
"$RUNNER" --debug-flip-exit-probability=2 2>&1 || true
# CHECK: == debug flip too large ==
# CHECK: error: invalid --debug-flip-exit-probability value: 2
# CHECK: Usage: test-runner [--list] [--all] [--output-dir DIR] [--filter PATTERN]

echo '== debug flip huge =='
"$RUNNER" --debug-flip-exit-probability 1e5000 2>&1 || true
# CHECK: == debug flip huge ==
# CHECK: error: invalid --debug-flip-exit-probability value: 1e5000
# CHECK: Usage: test-runner [--list] [--all] [--output-dir DIR] [--filter PATTERN]

echo '== missing debug parent setpgid delay =='
"$RUNNER" --debug-parent-setpgid-delay 2>&1 || true
# CHECK: == missing debug parent setpgid delay ==
# CHECK: error: --debug-parent-setpgid-delay requires a value

echo '== debug parent setpgid delay text =='
"$RUNNER" --debug-parent-setpgid-delay nope 2>&1 || true
# CHECK: == debug parent setpgid delay text ==
# CHECK: error: invalid --debug-parent-setpgid-delay value: nope
# CHECK: Usage: test-runner [--list] [--all] [--output-dir DIR] [--filter PATTERN]

echo '== debug parent setpgid delay negative =='
"$RUNNER" --debug-parent-setpgid-delay=-1 2>&1 || true
# CHECK: == debug parent setpgid delay negative ==
# CHECK: error: invalid --debug-parent-setpgid-delay value: -1
# CHECK: Usage: test-runner [--list] [--all] [--output-dir DIR] [--filter PATTERN]

echo '== debug parent setpgid delay trailing junk =='
"$RUNNER" --debug-parent-setpgid-delay=1x 2>&1 || true
# CHECK: == debug parent setpgid delay trailing junk ==
# CHECK: error: invalid --debug-parent-setpgid-delay value: 1x
# CHECK: Usage: test-runner [--list] [--all] [--output-dir DIR] [--filter PATTERN]

echo '== debug parent setpgid delay huge =='
"$RUNNER" --debug-parent-setpgid-delay 1e5000 2>&1 || true
# CHECK: == debug parent setpgid delay huge ==
# CHECK: error: invalid --debug-parent-setpgid-delay value: 1e5000
# CHECK: Usage: test-runner [--list] [--all] [--output-dir DIR] [--filter PATTERN]

echo '== unknown option =='
"$RUNNER" --definitely-unknown 2>&1 || true
# CHECK: == unknown option ==
# CHECK: error: unknown option: --definitely-unknown
# CHECK: Usage: test-runner [--list] [--all] [--output-dir DIR] [--filter PATTERN]

echo '== all with filter =='
"$RUNNER" --all runner/no-subtests.c 2>&1 || true
# CHECK: == all with filter ==
# CHECK: error: --all cannot be combined with --filter or positional patterns
# CHECK: Usage: test-runner [--list] [--all] [--output-dir DIR] [--filter PATTERN]

echo '== empty filter parts =='
"$RUNNER" --list --filter '||runner/no-subtests.c|' 2>&1 || true
# CHECK: == empty filter parts ==
# CHECK: error: invalid filter pattern: ||runner/no-subtests.c|

echo '== list no matches =='
"$RUNNER" --list --filter does/not/exist 2>&1 || true
echo '== end list no matches =='
# CHECK: == list no matches ==
# CHECK-NEXT: == end list no matches ==

echo '== run no matches =='
"$RUNNER" --output-dir "$NO_MATCH_OUTPUT_DIR" \
    --filter does/not/exist 2>&1 || true
# CHECK: == run no matches ==
# CHECK: error: no tests matched the requested filters

echo '== nonempty output dir =='
"$RUNNER" --output-dir "$NONEMPTY_OUTPUT_DIR" \
    --filter runner/output.sh 2>&1 || true
# CHECK: == nonempty output dir ==
# CHECK: error: test output directory is not empty: {{.*nonempty-output}}
# CHECK: remove it first with rm -r '{{.*nonempty-output}}'

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
