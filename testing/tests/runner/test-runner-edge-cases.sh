#!/usr/bin/env run-and-check
# RUN: sh %s

# Exercise additional reachable edge cases in test-runner without mutating the
# repo tree or build outputs.

set -eu

fail() {
    echo "$1" >&2
    exit 1
}

RUNNER=$IMGNEKO_BUILD_DIR/bin/test-runner
[ -x "$RUNNER" ] || fail "missing test-runner binary: $RUNNER"

MARKER_TEST_DIR=$IMGNEKO_TEST_OUTPUT_DIR/marker-tests
EMPTY_TEST_DIR=$IMGNEKO_TEST_OUTPUT_DIR/empty-tests
BLOCKED_DISCOVERY_DIR=$IMGNEKO_TEST_OUTPUT_DIR/blocked-discovery
BLOCKED_DISCOVERY_CHILD=$BLOCKED_DISCOVERY_DIR/locked
EXEC_RACE_TEST_DIR=$IMGNEKO_TEST_OUTPUT_DIR/exec-race-tests
SETPGID_RACE_TEST_DIR=$IMGNEKO_TEST_OUTPUT_DIR/setpgid-race-tests
OUTPUT_DRAIN_RACE_TEST_DIR=$IMGNEKO_TEST_OUTPUT_DIR/output-drain-race-tests
TIMEOUT_TEST_DIR=$IMGNEKO_TEST_OUTPUT_DIR/timeout-tests
PARALLEL_TEST_DIR=$IMGNEKO_TEST_OUTPUT_DIR/parallel-tests
FAKE_C_TEST_DIR=$IMGNEKO_TEST_OUTPUT_DIR/fake-c-tests
FAKE_TEST_BIN_DIR=$IMGNEKO_TEST_OUTPUT_DIR/fake-test-bin
FILE_OUTPUT_PATH=$IMGNEKO_TEST_OUTPUT_DIR/output-file
BLOCKED_OUTPUT_DIR=$IMGNEKO_TEST_OUTPUT_DIR/blocked-output
EXEC_RACE_OUTPUT_DIR=$IMGNEKO_TEST_OUTPUT_DIR/exec-race-output
SETPGID_RACE_OUTPUT_DIR=$IMGNEKO_TEST_OUTPUT_DIR/setpgid-race-output
OUTPUT_DRAIN_RACE_OUTPUT_DIR=$IMGNEKO_TEST_OUTPUT_DIR/output-drain-race-output
PARALLEL_COMPLETION_OUTPUT_DIR=$IMGNEKO_TEST_OUTPUT_DIR/parallel-completion-output
PARALLEL_CLOSED_FDS_OUTPUT_DIR=$IMGNEKO_TEST_OUTPUT_DIR/parallel-closed-fds-output
FLIP_MANY_OUTPUT_DIR=$IMGNEKO_TEST_OUTPUT_DIR/flip-many-output
TIMEOUT_PASSTHROUGH_OUTPUT_DIR=$IMGNEKO_TEST_OUTPUT_DIR/timeout-passthrough-output
TIMEOUT_SIGKILL_OUTPUT_DIR=$IMGNEKO_TEST_OUTPUT_DIR/timeout-sigkill-output
PARALLEL_TIMEOUT_OUTPUT_DIR=$IMGNEKO_TEST_OUTPUT_DIR/parallel-timeout-output

mkdir -p "$MARKER_TEST_DIR" "$EMPTY_TEST_DIR" "$BLOCKED_DISCOVERY_CHILD"
mkdir -p "$EXEC_RACE_TEST_DIR" "$SETPGID_RACE_TEST_DIR"
mkdir -p "$OUTPUT_DRAIN_RACE_TEST_DIR" "$TIMEOUT_TEST_DIR" "$PARALLEL_TEST_DIR"
mkdir -p "$FAKE_C_TEST_DIR/runner" "$FAKE_TEST_BIN_DIR/runner"
mkdir -p "$BLOCKED_OUTPUT_DIR"


cat >"$MARKER_TEST_DIR/start-marker.sh" <<'EOF'
#!/bin/sh
XFAIL
exit 0
EOF
chmod +x "$MARKER_TEST_DIR/start-marker.sh"

cat >"$MARKER_TEST_DIR/alnum-marker.sh" <<'EOF'
#!/bin/sh
AXFAIL XFAIL
exit 0
EOF
chmod +x "$MARKER_TEST_DIR/alnum-marker.sh"

cat >"$MARKER_TEST_DIR/underscore-marker.sh" <<'EOF'
#!/bin/sh
_XFAIL XFAIL_ XFAIL
exit 0
EOF
chmod +x "$MARKER_TEST_DIR/underscore-marker.sh"

cat >"$MARKER_TEST_DIR/plain-marker.sh" <<'EOF'
#!/bin/sh
exit 0
EOF
chmod +x "$MARKER_TEST_DIR/plain-marker.sh"

echo '== marker boundaries =='
"$RUNNER" --jobs=1 --list --tests-dir="$MARKER_TEST_DIR" \
    --filter 'start-marker.sh|alnum-marker.sh|underscore-marker.sh|plain-marker.sh' \
    2>&1
# CHECK: == marker boundaries ==
# CHECK: alnum-marker.sh XFAIL
# CHECK: plain-marker.sh
# CHECK: start-marker.sh XFAIL
# CHECK: underscore-marker.sh XFAIL


ROOT_PREFIX_SAFE_OUTPUT_DIR=$(dirname "$IMGNEKO_ROOT_DIR")/fake-output-dir
echo '== tests dir equals and similar prefix output =='
"$RUNNER" --jobs=1 --list --output-dir "$ROOT_PREFIX_SAFE_OUTPUT_DIR" \
    --tests-dir="$MARKER_TEST_DIR" --filter start-marker.sh 2>&1
# CHECK: == tests dir equals and similar prefix output ==
# CHECK-NEXT: start-marker.sh XFAIL

echo '== path unset =='
env -u PATH "$RUNNER" --jobs=1 --list runner/no-subtests.c 2>&1
# CHECK: == path unset ==
# CHECK-NEXT: runner/no-subtests.c

echo 'stale output path' >"$FILE_OUTPUT_PATH"
echo '== output path is file =='
"$RUNNER" --jobs=1 --output-dir "$FILE_OUTPUT_PATH" \
    --filter runner/output.sh 2>&1 || true
# CHECK: == output path is file ==
# CHECK: error: test output path exists and is not a directory: {{.*output-file}}

chmod 000 "$BLOCKED_OUTPUT_DIR"
echo '== blocked output dir =='
"$RUNNER" --jobs=1 --output-dir "$BLOCKED_OUTPUT_DIR" \
    --filter runner/output.sh 2>&1 || true
# CHECK: == blocked output dir ==
# CHECK: error: failed to open output directory: Permission denied

echo '== no tests found =='
"$RUNNER" --jobs=1 --list --tests-dir "$EMPTY_TEST_DIR" 2>&1 || true
# CHECK: == no tests found ==
# CHECK: error: no tests found under {{.*empty-tests}}

chmod 000 "$BLOCKED_DISCOVERY_CHILD"
echo '== blocked discovery =='
"$RUNNER" --jobs=1 --list --tests-dir "$BLOCKED_DISCOVERY_DIR" 2>&1 || true
# CHECK: == blocked discovery ==
# CHECK: error: failed to open tests directory: Permission denied


cat >"$FAKE_C_TEST_DIR/runner/single-char-subtests.c" <<'EOF'
int main(void) { return 0; }
EOF

cat >"$FAKE_C_TEST_DIR/runner/many-subtests.c" <<'EOF'
int main(void) { return 0; }
EOF

cat >"$FAKE_TEST_BIN_DIR/runner/single-char-subtests.c.bin" <<'EOF'
#!/bin/sh
if [ "$1" = "--list" ]; then
    echo x
    echo 'short_xfail XFAIL'
    exit 0
fi
if [ "$1" = x ]; then
    exit 0
fi
if [ "$1" = short_xfail ]; then
    exit 1
fi
exit 1
EOF
chmod +x "$FAKE_TEST_BIN_DIR/runner/single-char-subtests.c.bin"

cat >"$FAKE_TEST_BIN_DIR/runner/many-subtests.c.bin" <<'EOF'
#!/bin/sh
if [ "$1" = "--list" ]; then
    i=0
    while [ "$i" -lt 64 ]; do
        echo "case-$i"
        i=$((i + 1))
    done
    exit 0
fi
exit 0
EOF
chmod +x "$FAKE_TEST_BIN_DIR/runner/many-subtests.c.bin"

echo '== single-char c subtests =='
"$RUNNER" --jobs=1 --list --tests-dir "$FAKE_C_TEST_DIR" \
    --test-bin-dir "$FAKE_TEST_BIN_DIR" \
    --filter runner/single-char-subtests.c 2>&1
# CHECK: == single-char c subtests ==
# CHECK: runner/single-char-subtests.c/short_xfail XFAIL
# CHECK: runner/single-char-subtests.c/x

# A temporary fake C test with many subtests makes the 0.5 debug-flip path hit
# both flipped and unflipped outcomes with negligible flake risk.
echo '== probabilistic debug flip =='
"$RUNNER" --jobs=1 --tests-dir "$FAKE_C_TEST_DIR" \
    --test-bin-dir "$FAKE_TEST_BIN_DIR" \
    --output-dir "$FLIP_MANY_OUTPUT_DIR" \
    --debug-flip-exit-probability 0.5 \
    --filter runner/many-subtests.c 2>&1 || true
# CHECK: == probabilistic debug flip ==
# CHECK: RUN: runner/many-subtests.c/case-0
# CHECK: DEBUG: flipped exit code for runner/many-subtests.c/
# CHECK: PASS: runner/many-subtests.c/
# CHECK: FAIL: runner/many-subtests.c/
# CHECK: Summary:
# CHECK: discovered: 64
# CHECK: Result: FAILURE

cat >"$EXEC_RACE_TEST_DIR/a-remove-exec.sh" <<EOF
#!/bin/sh
chmod -x '$EXEC_RACE_TEST_DIR/b-target.sh'
EOF
chmod +x "$EXEC_RACE_TEST_DIR/a-remove-exec.sh"

cat >"$EXEC_RACE_TEST_DIR/b-target.sh" <<'EOF'
#!/bin/sh
exit 0
EOF
chmod +x "$EXEC_RACE_TEST_DIR/b-target.sh"

echo '== exec permission race =='
"$RUNNER" --jobs=1 --tests-dir "$EXEC_RACE_TEST_DIR" \
    --output-dir "$EXEC_RACE_OUTPUT_DIR" 2>&1 || true
# CHECK: == exec permission race ==
# CHECK: RUN: a-remove-exec.sh
# CHECK: PASS: a-remove-exec.sh
# CHECK: RUN: b-target.sh
# CHECK: FAIL: b-target.sh
# CHECK: LAST 20 LINES OF TEST OUTPUT {{.*b-target\.sh/output}}
# CHECK: error: failed to exec {{.*b-target\.sh}}: Permission denied
# CHECK: ===== }}} END TEST OUTPUT =====
# CHECK: failed tests:
# CHECK: b-target.sh
# CHECK: Summary:
# CHECK: discovered: 2
# CHECK: passed: 1
# CHECK: failed: 1
# CHECK: Result: FAILURE

cat >"$SETPGID_RACE_TEST_DIR/eacces-after-exec.sh" <<'EOF'
#!/bin/sh
# Keep the exec'd shell alive long enough for the parent to hit EACCES in its
# follow-up setpgid() call after the injected debug delay.
sleep 2
EOF
chmod +x "$SETPGID_RACE_TEST_DIR/eacces-after-exec.sh"

echo '== parent setpgid eacces =='
"$RUNNER" --jobs=1 --tests-dir "$SETPGID_RACE_TEST_DIR" \
    --output-dir "$SETPGID_RACE_OUTPUT_DIR" \
    --debug-parent-setpgid-delay 1 \
    --filter eacces-after-exec.sh 2>&1
# CHECK: == parent setpgid eacces ==
# CHECK: RUN: eacces-after-exec.sh
# CHECK: PASS: eacces-after-exec.sh
# CHECK: Summary:
# CHECK: discovered: 1
# CHECK: passed: 1
# CHECK: Result: SUCCESS

cat >"$OUTPUT_DRAIN_RACE_TEST_DIR/reaped-before-output-drain.sh" <<'EOF'
#!/bin/sh
# Emit more than two pipe chunks and exit immediately so the parent can reap
# the child while unread output still remains buffered.
i=0
while [ "$i" -lt 256 ]; do
    printf '%080d\n' "$i"
    i=$((i + 1))
done
EOF
chmod +x "$OUTPUT_DRAIN_RACE_TEST_DIR/reaped-before-output-drain.sh"

echo '== child reaped before output drain =='
"$RUNNER" --jobs=1 --tests-dir "$OUTPUT_DRAIN_RACE_TEST_DIR" \
    --output-dir "$OUTPUT_DRAIN_RACE_OUTPUT_DIR" \
    --debug-parent-output-chunk-delay 0.01 \
    --filter reaped-before-output-drain.sh 2>&1
# CHECK: == child reaped before output drain ==
# CHECK: RUN: reaped-before-output-drain.sh
# CHECK: PASS: reaped-before-output-drain.sh
# CHECK: Summary:
# CHECK: discovered: 1
# CHECK: passed: 1
# CHECK: Result: SUCCESS

cat >"$PARALLEL_TEST_DIR/instant-success.sh" <<'EOF'
#!/bin/sh
exit 0
EOF
chmod +x "$PARALLEL_TEST_DIR/instant-success.sh"

cat >"$PARALLEL_TEST_DIR/slow-success.sh" <<'EOF'
#!/bin/sh
sleep 1
exit 0
EOF
chmod +x "$PARALLEL_TEST_DIR/slow-success.sh"

echo '== parallel completed child before wait =='
"$RUNNER" --tests-dir "$PARALLEL_TEST_DIR" \
    --output-dir "$PARALLEL_COMPLETION_OUTPUT_DIR" \
    -j 2 --filter 'instant-success.sh|slow-success.sh' 2>&1
# CHECK: == parallel completed child before wait ==
# CHECK: RUN: instant-success.sh
# CHECK: RUN: slow-success.sh
# CHECK: PASS: instant-success.sh
# CHECK: PASS: slow-success.sh
# CHECK: Summary:
# CHECK: discovered: 2
# CHECK: passed: 2
# CHECK: Result: SUCCESS

cat >"$PARALLEL_TEST_DIR/closed-fds-sleeper.sh" <<'EOF'
#!/bin/sh
exec 1>&- 2>&-
sleep 1
EOF
chmod +x "$PARALLEL_TEST_DIR/closed-fds-sleeper.sh"

cat >"$PARALLEL_TEST_DIR/delayed-output.sh" <<'EOF'
#!/bin/sh
sleep 0.2
echo 'delayed output marker'
EOF
chmod +x "$PARALLEL_TEST_DIR/delayed-output.sh"

echo '== parallel closed fds =='
"$RUNNER" --tests-dir "$PARALLEL_TEST_DIR" \
    --output-dir "$PARALLEL_CLOSED_FDS_OUTPUT_DIR" \
    -j 2 --filter 'closed-fds-sleeper.sh|delayed-output.sh' 2>&1
# CHECK: == parallel closed fds ==
# CHECK: RUN: closed-fds-sleeper.sh
# CHECK: RUN: delayed-output.sh
# CHECK: PASS: delayed-output.sh
# CHECK: PASS: closed-fds-sleeper.sh
# CHECK: Summary:
# CHECK: discovered: 2
# CHECK: passed: 2
# CHECK: Result: SUCCESS

cat >"$TIMEOUT_TEST_DIR/timeout-passthrough.sh" <<'EOF'
#!/bin/sh
echo 'timeout passthrough marker'
sleep 5
EOF
chmod +x "$TIMEOUT_TEST_DIR/timeout-passthrough.sh"

cat >"$TIMEOUT_TEST_DIR/ignore-term.sh" <<'EOF'
#!/bin/sh
trap '' TERM
sleep 5
EOF
chmod +x "$TIMEOUT_TEST_DIR/ignore-term.sh"

cat >"$TIMEOUT_TEST_DIR/ignore-term-too.sh" <<'EOF'
#!/bin/sh
trap '' TERM
sleep 5
EOF
chmod +x "$TIMEOUT_TEST_DIR/ignore-term-too.sh"

cat >"$TIMEOUT_TEST_DIR/detached-output.sh" <<'EOF'
#!/bin/sh
printf '%s\n' 'timeout detached output marker'
setsid sh -c 'sleep 5' &
sleep 2
EOF
chmod +x "$TIMEOUT_TEST_DIR/detached-output.sh"

echo '== timeout sigkill =='
"$RUNNER" --jobs=1 --tests-dir "$TIMEOUT_TEST_DIR" \
    --output-dir "$TIMEOUT_SIGKILL_OUTPUT_DIR" \
    --timeout 1 --filter ignore-term.sh 2>&1 || true
# CHECK: == timeout sigkill ==
# CHECK: RUN: ignore-term.sh
# CHECK: TIMEOUT: ignore-term.sh
# CHECK: timed out tests:
# CHECK: ignore-term.sh
# CHECK: Result: FAILURE

echo '== parallel timeout bookkeeping =='
"$RUNNER" --tests-dir "$TIMEOUT_TEST_DIR" \
    --output-dir "$PARALLEL_TIMEOUT_OUTPUT_DIR" \
    -j 2 --timeout 1 \
    --filter 'ignore-term.sh|detached-output.sh' 2>&1 || true
# CHECK: == parallel timeout bookkeeping ==
# CHECK: RUN: detached-output.sh
# CHECK: RUN: ignore-term.sh
# CHECK: TIMEOUT: ignore-term.sh
# CHECK: TIMEOUT: detached-output.sh
# CHECK: timed out tests:
# CHECK: ignore-term.sh
# CHECK: detached-output.sh
# CHECK: Summary:
# CHECK: discovered: 2
# CHECK: timeout: 2
# CHECK: Result: FAILURE

echo '== parallel timeout sigkill deadlines =='
"$RUNNER" --tests-dir "$TIMEOUT_TEST_DIR" \
    --output-dir "$PARALLEL_TIMEOUT_OUTPUT_DIR-sigkill" \
    -j 2 --timeout 1 \
    --filter 'ignore-term.sh|ignore-term-too.sh' 2>&1 || true
# CHECK: == parallel timeout sigkill deadlines ==
# CHECK: RUN: ignore-term-too.sh
# CHECK: RUN: ignore-term.sh
# CHECK: TIMEOUT: ignore-term{{|-too}}.sh
# CHECK: TIMEOUT: ignore-term{{|-too}}.sh
# CHECK: timed out tests:
# CHECK: ignore-term{{|-too}}.sh
# CHECK: ignore-term{{|-too}}.sh
# CHECK: Summary:
# CHECK: discovered: 2
# CHECK: timeout: 2
# CHECK: Result: FAILURE

echo '== timeout passthrough =='
"$RUNNER" --jobs=1 --tests-dir "$TIMEOUT_TEST_DIR" \
    --output-dir "$TIMEOUT_PASSTHROUGH_OUTPUT_DIR" \
    --output-passthrough --timeout 1 --filter timeout-passthrough.sh \
    2>&1 || true
# CHECK: == timeout passthrough ==
# CHECK: RUN: timeout-passthrough.sh
# CHECK: timeout passthrough marker
# CHECK: TIMEOUT: timeout-passthrough.sh
# CHECK: timed out tests:
# CHECK: timeout-passthrough.sh
# CHECK: Result: FAILURE
# CHECK-NOT: ===== LAST
