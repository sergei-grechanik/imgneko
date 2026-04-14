#!/usr/bin/env run-and-check
# RUN: sh %s

# Exercise the sample CLI that is built on top of the generic option parser
# utility.

set -eu

fail() {
    echo "$1" >&2
    exit 1
}

SAMPLE_CLI=$IMGNEKO_BUILD_DIR/bin/sample-cli
RUN_IN_PTY=$IMGNEKO_BUILD_DIR/bin/run-in-pty

[ -x "$SAMPLE_CLI" ] || fail "missing sample-cli binary: $SAMPLE_CLI"
[ -x "$RUN_IN_PTY" ] || fail "missing run-in-pty binary: $RUN_IN_PTY"

echo '== program help =='
env COLUMNS=80 "$SAMPLE_CLI" --help 2>&1
# CHECK:      {{^}}== program help =={{$}}
# CHECK-NEXT: {{^}}Sample CLI that exercises shared options, commands, version handling, boolean modes, defaults, custom scalar and list values, and positional parsing.{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Usage: sample-cli [options] <command> [command options]{{$}}
# CHECK-NEXT: {{^}}       sample-cli [morph options] [--] [SUBJECT...]{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Commands:{{$}}
# CHECK-NEXT: {{^}}  morph (default)           Exercise the default-command path with shared and{{$}}
# CHECK-NEXT: {{^}}                            custom options.{{$}}
# CHECK-NEXT: {{^}}  audit                     Exercise an explicit command with list and{{$}}
# CHECK-NEXT: {{^}}                            bool-value parsing.{{$}}
# CHECK-NEXT: {{^}}  purge                     Exercise a command without positionals and with{{$}}
# CHECK-NEXT: {{^}}                            short flags.{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Options:{{$}}
# CHECK-NEXT: {{^}}  --version                 Show program version and exit.{{$}}
# CHECK-NEXT: {{^}}  -h, --help                Show overall help and exit.{{$}}
# CHECK-NEXT: {{^$}}

echo '== short help =='
env COLUMNS=80 "$SAMPLE_CLI" -h 2>&1
# CHECK-NEXT: {{^}}== short help =={{$}}
# CHECK-NEXT: {{^}}Sample CLI that exercises shared options, commands, version handling, boolean modes, defaults, custom scalar and list values, and positional parsing.{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Usage: sample-cli [options] <command> [command options]{{$}}

echo '== command help =='
env COLUMNS=80 "$SAMPLE_CLI" morph --help 2>&1
# CHECK:      {{^}}== command help =={{$}}
# CHECK-NEXT: {{^}}Exercise the default-command path with shared and custom options.{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Usage: sample-cli morph [options] [--] [SUBJECT...]{{$}}
# CHECK-NEXT: {{^}}       sample-cli [options] [--] [SUBJECT...]{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Positional arguments:{{$}}
# CHECK-NEXT: {{^}}  SUBJECT...                Subjects to morph.{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Options:{{$}}
# CHECK-NEXT: {{^}}  --version                 Show program version and exit.{{$}}
# CHECK-NEXT: {{^}}  -c, --config FILE         Read settings from FILE before applying command-line{{$}}
# CHECK-NEXT: {{^}}                            overrides.{{$}}
# CHECK-NEXT: {{^}}  -v, --verbose             Enable verbose logging.{{$}}
# CHECK-NEXT: {{^}}  -p, --passes N            Run N synthetic transformation passes. (default: 24){{$}}
# CHECK-NEXT: {{^}}  -q, --quota N             Use a positive quota as the synthetic work budget.{{$}}
# CHECK-NEXT: {{^}}  --window START:END        Limit the synthetic pass window to the inclusive{{$}}
# CHECK-NEXT: {{^}}                            START:END range. (default: 1:3){{$}}
# CHECK-NEXT: {{^}}  --grid NxM                Use an NxM work grid for the synthetic job.{{$}}
# CHECK-NEXT: {{^}}                            (default: 80x24){{$}}
# CHECK-NEXT: {{^}}  --profile NAME            Select the synthetic profile. (default: auto){{$}}
# CHECK-NEXT: {{^}}  -k, --cache-results, -K, --no-cache-results{{$}}
# CHECK-NEXT: {{^}}                            Cache or skip cached morph results.{{$}}
# CHECK-NEXT: {{^}}  --keep-workspace          Retain the synthetic workspace after the morph run.{{$}}
# CHECK-NEXT: {{^}}                            (default: false){{$}}
# CHECK-NEXT: {{^}}  --no-cleanup              Clean up the synthetic workspace after the morph{{$}}
# CHECK-NEXT: {{^}}                            run. This an awkward negate-only negatable flag,{{$}}
# CHECK-NEXT: {{^}}                            just for testing, prefer just normal flags in real{{$}}
# CHECK-NEXT: {{^}}                            life. (default: true){{$}}
# CHECK-NEXT: {{^}}  -h, --help                Show this help message and exit.{{$}}
# CHECK-NEXT: {{^$}}

echo '== deferred command help =='
env COLUMNS=80 "$SAMPLE_CLI" --help morph 2>&1
# CHECK-NEXT: {{^}}== deferred command help =={{$}}
# CHECK-NEXT: {{^}}Exercise the default-command path with shared and custom options.{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Usage: sample-cli morph [options] [--] [SUBJECT...]{{$}}
# CHECK-NEXT: {{^}}       sample-cli [options] [--] [SUBJECT...]{{$}}

echo '== non-default command help =='
env COLUMNS=80 "$SAMPLE_CLI" audit --help 2>&1
# CHECK:      {{^}}== non-default command help =={{$}}
# CHECK-NEXT: {{^}}Exercise an explicit command with list and bool-value parsing.{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Usage: sample-cli audit [options] [--] [TARGET...]{{$}}

echo '== no-positional command help =='
env COLUMNS=80 "$SAMPLE_CLI" purge --help 2>&1
# CHECK:      {{^}}== no-positional command help =={{$}}
# CHECK-NEXT: {{^}}Exercise a command without positionals and with short flags.{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Usage: sample-cli purge [options]{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Options:{{$}}
# CHECK:      {{^}}  -a, --all                 Remove all cached artifacts.{{$}}

echo '== help with narrow COLUMNS =='
env COLUMNS=20 "$SAMPLE_CLI" --help 2>&1
# CHECK:      {{^}}== help with narrow COLUMNS =={{$}}
# CHECK-NEXT: {{^}}Sample CLI that exercises shared options, commands, version handling, boolean modes, defaults, custom scalar and list values, and positional parsing.{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Usage: sample-cli [options] <command> [command options]{{$}}
# CHECK-NEXT: {{^}}       sample-cli [morph options] [--] [SUBJECT...]{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Commands:{{$}}
# CHECK-NEXT: {{^}}  morph (default){{$}}
# CHECK-NEXT: {{^}}             Exercise the{{$}}
# CHECK-NEXT: {{^}}             default-command path with{{$}}
# CHECK-NEXT: {{^}}             shared and custom options.{{$}}
# CHECK-NEXT: {{^}}  audit      Exercise an explicit{{$}}
# CHECK-NEXT: {{^}}             command with list and{{$}}
# CHECK-NEXT: {{^}}             bool-value parsing.{{$}}
# CHECK-NEXT: {{^}}  purge      Exercise a command without{{$}}
# CHECK-NEXT: {{^}}             positionals and with short{{$}}
# CHECK-NEXT: {{^}}             flags.{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Options:{{$}}
# CHECK-NEXT: {{^}}  --version  Show program version and{{$}}
# CHECK-NEXT: {{^}}             exit.{{$}}
# CHECK-NEXT: {{^}}  -h, --help Show overall help and exit.{{$}}
# CHECK-NEXT: {{^$}}

echo '== help with empty COLUMNS =='
env COLUMNS= "$SAMPLE_CLI" --help 2>&1
# CHECK:      {{^}}== help with empty COLUMNS =={{$}}
# CHECK-NEXT: {{^}}Sample CLI that exercises shared options, commands, version handling, boolean modes, defaults, custom scalar and list values, and positional parsing.{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Usage: sample-cli [options] <command> [command options]{{$}}
# CHECK-NEXT: {{^}}       sample-cli [morph options] [--] [SUBJECT...]{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Commands:{{$}}
# CHECK-NEXT: {{^}}  morph (default)           Exercise the default-command path with shared and{{$}}
# CHECK-NEXT: {{^}}                            custom options.{{$}}
# CHECK-NEXT: {{^}}  audit                     Exercise an explicit command with list and{{$}}
# CHECK-NEXT: {{^}}                            bool-value parsing.{{$}}
# CHECK-NEXT: {{^}}  purge                     Exercise a command without positionals and with{{$}}
# CHECK-NEXT: {{^}}                            short flags.{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Options:{{$}}
# CHECK-NEXT: {{^}}  --version                 Show program version and exit.{{$}}
# CHECK-NEXT: {{^}}  -h, --help                Show overall help and exit.{{$}}
# CHECK-NEXT: {{^$}}

echo '== help with overflowing COLUMNS =='
env COLUMNS=184467440737095516161844674407370955161 \
    "$SAMPLE_CLI" --help 2>&1
# CHECK:      {{^}}== help with overflowing COLUMNS =={{$}}
# CHECK-NEXT: {{^}}Sample CLI that exercises shared options, commands, version handling, boolean modes, defaults, custom scalar and list values, and positional parsing.{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Usage: sample-cli [options] <command> [command options]{{$}}
# CHECK-NEXT: {{^}}       sample-cli [morph options] [--] [SUBJECT...]{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Commands:{{$}}
# CHECK-NEXT: {{^}}  morph (default)           Exercise the default-command path with shared and{{$}}
# CHECK-NEXT: {{^}}                            custom options.{{$}}
# CHECK-NEXT: {{^}}  audit                     Exercise an explicit command with list and{{$}}
# CHECK-NEXT: {{^}}                            bool-value parsing.{{$}}
# CHECK-NEXT: {{^}}  purge                     Exercise a command without positionals and with{{$}}
# CHECK-NEXT: {{^}}                            short flags.{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Options:{{$}}
# CHECK-NEXT: {{^}}  --version                 Show program version and exit.{{$}}
# CHECK-NEXT: {{^}}  -h, --help                Show overall help and exit.{{$}}
# CHECK-NEXT: {{^$}}

echo '== help via pty stdout =='
"$RUN_IN_PTY" --rows 24 --cols 55 -- "$SAMPLE_CLI" --help 2>&1
# CHECK:      {{^}}== help via pty stdout =={{$}}
# CHECK-NEXT: {{^}}Sample CLI that exercises shared options, commands, version handling, boolean modes, defaults, custom scalar and list values, and positional parsing.{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Usage: sample-cli [options] <command> [command options]{{$}}
# CHECK-NEXT: {{^}}       sample-cli [morph options] [--] [SUBJECT...]{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Commands:{{$}}
# CHECK-NEXT: {{^}}  morph (default)           Exercise the{{$}}
# CHECK-NEXT: {{^}}                            default-command path with{{$}}
# CHECK-NEXT: {{^}}                            shared and custom options.{{$}}
# CHECK-NEXT: {{^}}  audit                     Exercise an explicit{{$}}
# CHECK-NEXT: {{^}}                            command with list and{{$}}
# CHECK-NEXT: {{^}}                            bool-value parsing.{{$}}
# CHECK-NEXT: {{^}}  purge                     Exercise a command without{{$}}
# CHECK-NEXT: {{^}}                            positionals and with short{{$}}
# CHECK-NEXT: {{^}}                            flags.{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Options:{{$}}
# CHECK-NEXT: {{^}}  --version                 Show program version and{{$}}
# CHECK-NEXT: {{^}}                            exit.{{$}}
# CHECK-NEXT: {{^}}  -h, --help                Show overall help and exit.{{$}}
# CHECK-NEXT: {{^$}}

echo '== default command =='
"$SAMPLE_CLI" -p10 --window 2:5 --grid 120x40 --profile solarized -K --no-cache-results -- alpha.subject -beta.subject 2>&1
# CHECK: == default command ==
# CHECK: command: morph
# CHECK: config: <unset>
# CHECK: verbose: false (default)
# CHECK: passes: 10 (cli)
# CHECK: quota: <unset>
# CHECK: window: 2:5 (cli)
# CHECK: grid: 120x40 (cli)
# CHECK: profile: solarized (cli)
# CHECK: cache_results: false (cli)
# CHECK: keep_workspace: false (default)
# CHECK: cleanup: true (default)
# CHECK: subjects: ["alpha.subject", "-beta.subject"] (cli)
# CHECK: format: summary (default)
# CHECK: strict: <unset>
# CHECK: slices: <unset>
# CHECK: targets: <unset>
# CHECK: all: false (default)
# CHECK: dry_run: false (default)
# CHECK: cache_root: <unset>

echo '== single subject morph =='
"$SAMPLE_CLI" morph --keep-workspace --no-cleanup alpha.subject 2>&1
# CHECK: == single subject morph ==
# CHECK: command: morph
# CHECK: keep_workspace: true (cli)
# CHECK: cleanup: false (cli)
# CHECK: subjects: ["alpha.subject"] (cli)

echo '== negate-only option =='
"$SAMPLE_CLI" --no-cleanup alpha.subject 2>&1
# CHECK: == negate-only option ==
# CHECK: command: morph
# CHECK: cleanup: false (cli)
# CHECK: subjects: ["alpha.subject"] (cli)

echo '== explicit quota =='
"$SAMPLE_CLI" morph --quota 7 alpha.subject 2>&1
# CHECK: == explicit quota ==
# CHECK: command: morph
# CHECK: quota: 7 (cli)
# CHECK: subjects: ["alpha.subject"] (cli)

echo '== repeated bool aliases =='
"$SAMPLE_CLI" -k --cache-results alpha.subject 2>&1
# CHECK: == repeated bool aliases ==
# CHECK: command: morph
# CHECK: cache_results: true (cli)
# CHECK: keep_workspace: false (default)
# CHECK: cleanup: true (default)
# CHECK: subjects: ["alpha.subject"] (cli)

echo '== explicit audit =='
"$SAMPLE_CLI" audit -v --format=json --strict=OFF --slice 1:2 --slice 4:6 cache thumbs 2>&1
# CHECK: == explicit audit ==
# CHECK: command: audit
# CHECK: config: <unset>
# CHECK: verbose: true (cli)
# CHECK: passes: 24 (default)
# CHECK: quota: <unset>
# CHECK: window: 1:3 (default)
# CHECK: grid: 80x24 (default)
# CHECK: profile: auto (default)
# CHECK: cache_results: <unset>
# CHECK: keep_workspace: false (default)
# CHECK: cleanup: true (default)
# CHECK: subjects: <unset>
# CHECK: format: json (cli)
# CHECK: strict: false (cli)
# CHECK: slices: [1:2, 4:6] (cli)
# CHECK: targets: ["cache", "thumbs"] (cli)
# CHECK: all: false (default)
# CHECK: dry_run: false (default)
# CHECK: cache_root: <unset>

echo '== explicit bool on =='
"$SAMPLE_CLI" audit --strict on cache 2>&1
# CHECK: == explicit bool on ==
# CHECK: command: audit
# CHECK: strict: true (cli)
# CHECK: targets: ["cache"] (cli)

echo '== duplicate explicit bool invalid value =='
set +e
"$SAMPLE_CLI" audit --strict on --strict maybe 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK: == duplicate explicit bool invalid value ==
# CHECK: error: option specified multiple times: --strict
# CHECK: status=2

echo '== uppercase grid =='
"$SAMPLE_CLI" --grid 10X20 alpha.subject 2>&1
# CHECK: == uppercase grid ==
# CHECK: command: morph
# CHECK: grid: 10x20 (cli)
# CHECK: subjects: ["alpha.subject"] (cli)

echo '== invalid window value =='
set +e
"$SAMPLE_CLI" --window nope 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK: == invalid window value ==
# CHECK: error: invalid value for --window: nope (expected START:END with positive integers and START <= END)
# CHECK: status=2

echo '== invalid window separator =='
set +e
"$SAMPLE_CLI" --window 1:2:3 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK: == invalid window separator ==
# CHECK: error: invalid value for --window: 1:2:3 (expected START:END with positive integers and START <= END)
# CHECK: status=2

echo '== invalid window start =='
set +e
"$SAMPLE_CLI" --window 0:2 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK: == invalid window start ==
# CHECK: error: invalid value for --window: 0:2 (expected START:END with positive integers and START <= END)
# CHECK: status=2

echo '== invalid window order =='
set +e
"$SAMPLE_CLI" --window 3:2 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK: == invalid window order ==
# CHECK: error: invalid value for --window: 3:2 (expected START:END with positive integers and START <= END)
# CHECK: status=2

echo '== invalid window end =='
set +e
"$SAMPLE_CLI" --window 1:nope 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK: == invalid window end ==
# CHECK: error: invalid value for --window: 1:nope (expected START:END with positive integers and START <= END)
# CHECK: status=2

echo '== invalid grid separator =='
set +e
"$SAMPLE_CLI" --grid 10xx20 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK: == invalid grid separator ==
# CHECK: error: invalid value for --grid: 10xx20 (expected exactly one x separator)
# CHECK: status=2

echo '== invalid grid format =='
set +e
"$SAMPLE_CLI" --grid 10 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK: == invalid grid format ==
# CHECK: error: invalid value for --grid: 10 (expected NxM with positive integers)
# CHECK: status=2

echo '== invalid grid width =='
set +e
"$SAMPLE_CLI" --grid 0x20 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK: == invalid grid width ==
# CHECK: error: invalid value for --grid: 0x20 (width must be a positive integer)
# CHECK: status=2

echo '== invalid grid dimension =='
set +e
"$SAMPLE_CLI" --grid 10x0 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK: == invalid grid dimension ==
# CHECK: error: invalid value for --grid: 10x0 (height must be a positive integer)
# CHECK: status=2

# Each slice needs its own --slice, otherwise the argument will be interpreted
# as a positional target value.
echo '== single-value list occurrence =='
"$SAMPLE_CLI" audit --slice 1:2 4:6 cache 2>&1
# CHECK: == single-value list occurrence ==
# CHECK: command: audit
# CHECK: slices: [1:2] (cli)
# CHECK: targets: ["4:6", "cache"] (cli)

echo '== invalid slice =='
set +e
"$SAMPLE_CLI" audit --slice 0:2 cache 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK: == invalid slice ==
# CHECK: error: invalid value for --slice: 0:2 (expected START:END with positive integers and START <= END)
# CHECK: status=2

echo '== purge split shorts =='
"$SAMPLE_CLI" purge -a -n -o build/cache 2>&1
# CHECK: == purge split shorts ==
# CHECK: command: purge
# CHECK: config: <unset>
# CHECK: verbose: false (default)
# CHECK: passes: 24 (default)
# CHECK: quota: <unset>
# CHECK: window: 1:3 (default)
# CHECK: grid: 80x24 (default)
# CHECK: profile: auto (default)
# CHECK: cache_results: <unset>
# CHECK: keep_workspace: false (default)
# CHECK: cleanup: true (default)
# CHECK: subjects: <unset>
# CHECK: format: summary (default)
# CHECK: strict: <unset>
# CHECK: slices: <unset>
# CHECK: targets: <unset>
# CHECK: all: true (cli)
# CHECK: dry_run: true (cli)
# CHECK: cache_root: build/cache (cli)

echo '== short cluster rejected =='
set +e
"$SAMPLE_CLI" purge -an -o build/cache 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK: == short cluster rejected ==
# CHECK: error: unknown option: -an
# CHECK: status=2

echo '== version =='
"$SAMPLE_CLI" --version 2>&1
# CHECK: == version ==
# CHECK: sample-cli {{.*}}

echo '== subcommand version =='
"$SAMPLE_CLI" morph --version 2>&1
# CHECK: == subcommand version ==
# CHECK: sample-cli {{.*}}

echo '== non-default command version =='
"$SAMPLE_CLI" audit --version 2>&1
# CHECK: == non-default command version ==
# CHECK: sample-cli {{.*}}

echo '== no pseudo help command =='
"$SAMPLE_CLI" help morph 2>&1
# CHECK: == no pseudo help command ==
# CHECK-NEXT: command: morph
# CHECK: subjects: ["help", "morph"] (cli)

echo '== invalid value =='
set +e
"$SAMPLE_CLI" --quota=-1 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK: == invalid value ==
# CHECK: error: invalid value for --quota: -1 (must be positive)
# CHECK: status=2

echo '== invalid integer text =='
set +e
"$SAMPLE_CLI" --quota nope 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK: == invalid integer text ==
# CHECK: error: invalid value for --quota: nope (expected a base-10 integer)
# CHECK: status=2

echo '== inline value for flag rejected =='
set +e
"$SAMPLE_CLI" morph --keep-workspace=false alpha.subject 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK: == inline value for flag rejected ==
# CHECK: error: option does not take a value: --keep-workspace=false
# CHECK: status=2

echo '== missing value =='
set +e
"$SAMPLE_CLI" -q 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK: == missing value ==
# CHECK-NEXT: error: --quota requires a value
# CHECK-NEXT: status=2

echo '== missing long value =='
set +e
"$SAMPLE_CLI" --quota 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK: == missing long value ==
# CHECK-NEXT: error: --quota requires a value
# CHECK-NEXT: status=2

echo '== renamed option rejected =='
set +e
"$SAMPLE_CLI" purge --artifact-dir build/cache 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK: == renamed option rejected ==
# CHECK: error: unknown option: --artifact-dir
# CHECK: status=2

echo '== duplicate scalar option =='
set +e
"$SAMPLE_CLI" audit --format=json --format=summary cache 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK: == duplicate scalar option ==
# CHECK: error: option specified multiple times: --format
# CHECK: status=2

echo '== contradictory bool aliases =='
set +e
"$SAMPLE_CLI" -k --no-cache-results alpha.subject 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK: == contradictory bool aliases ==
# CHECK: error: option specified multiple times: --cache-results
# CHECK: status=2
