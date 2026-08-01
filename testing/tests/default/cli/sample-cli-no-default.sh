#!/usr/bin/env run-and-check
# SPDX-License-Identifier: MIT-0
# RUN: sh %s

# Exercise the sample CLI with multiple commands and no default command.

set -eu

fail() {
    echo "$1" >&2
    exit 1
}

SAMPLE_CLI=$IMGNEKO_BUILD_DIR/bin/sample-cli-no-default

[ -x "$SAMPLE_CLI" ] || fail "missing sample-cli-no-default binary: $SAMPLE_CLI"

echo '== program help =='
env COLUMNS=80 "$SAMPLE_CLI" --help 2>&1
# CHECK:      {{^}}== program help =={{$}}
# CHECK-NEXT: {{^}}Sample CLI with multiple commands and no default command.{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Usage: sample-cli-no-default <command> [options]{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Commands:{{$}}
# CHECK-NEXT: {{^}}  plan                      Exercise explicit command selection with{{$}}
# CHECK-NEXT: {{^}}                            command-local options.{{$}}
# CHECK-NEXT: {{^}}  apply                     Exercise no-default parsing with two positional{{$}}
# CHECK-NEXT: {{^}}                            arguments.{{$}}
# CHECK-NEXT: {{^}}  repeat-items              Exercise integer positional parsing without a{{$}}
# CHECK-NEXT: {{^}}                            default command.{{$}}
# CHECK-NEXT: {{^}}  unexplained{{$}}
# CHECK-NEXT: {{^}}  labels                    Exercise help and diagnostic label fallbacks.{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Options:{{$}}
# CHECK-NEXT: {{^}}  -h, --help                Show overall help and exit.{{$}}
# CHECK-NEXT: {{^$}}

echo '== short program help =='
env COLUMNS=80 "$SAMPLE_CLI" -h 2>&1
# CHECK-NEXT: {{^}}== short program help =={{$}}
# CHECK-NEXT: {{^}}Sample CLI with multiple commands and no default command.{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Usage: sample-cli-no-default <command> [options]{{$}}

echo '== deferred command help =='
env COLUMNS=80 "$SAMPLE_CLI" --help apply 2>&1
# CHECK:      {{^}}== deferred command help =={{$}}
# CHECK-NEXT: {{^}}Exercise no-default parsing with two positional arguments.{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Usage: sample-cli-no-default apply [options] [--] TARGET COUNT{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Positional arguments:{{$}}
# CHECK-NEXT: {{^}}  TARGET                    Thing to modify.{{$}}
# CHECK-NEXT: {{^}}  COUNT                     Number of synthetic changes to apply.{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Options:{{$}}
# CHECK-NEXT: {{^}}  -f, --force               Apply without a confirmation prompt.{{$}}
# CHECK-NEXT: {{^}}  -r, --retries N           Retry a failing apply step N times. (default: 1){{$}}
# CHECK-NEXT: {{^}}  -h, --help                Show this help message and exit.{{$}}
# CHECK-NEXT: {{^$}}

echo '== explicit command parse =='
"$SAMPLE_CLI" plan -v --format json repo 2>&1
# CHECK-NEXT: {{^}}== explicit command parse =={{$}}
# CHECK-NEXT: {{^}}command: plan{{$}}
# CHECK-NEXT: {{^}}verbose: true (cli){{$}}
# CHECK-NEXT: {{^}}format: json (cli){{$}}
# CHECK-NEXT: {{^}}target: repo (cli){{$}}

echo '== default plan values =='
"$SAMPLE_CLI" plan repo 2>&1
# CHECK-NEXT: {{^}}== default plan values =={{$}}
# CHECK-NEXT: {{^}}command: plan{{$}}
# CHECK-NEXT: {{^}}verbose: false (default){{$}}
# CHECK-NEXT: {{^}}format: summary (default){{$}}
# CHECK-NEXT: {{^}}target: repo (cli){{$}}

echo '== short-only invalid value =='
set +e
"$SAMPLE_CLI" plan -n nope repo 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK-NEXT: {{^}}== short-only invalid value =={{$}}
# CHECK-NEXT: {{^}}error: invalid value for -n: 'nope' (expected a base-10 integer){{$}}
# CHECK-NEXT: {{^}}status=2{{$}}

echo '== short-only duplicate =='
set +e
"$SAMPLE_CLI" plan -n 1 -n 2 repo 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK-NEXT: {{^}}== short-only duplicate =={{$}}
# CHECK-NEXT: {{^}}error: option specified multiple times: -n{{$}}
# CHECK-NEXT: {{^}}status=2{{$}}

echo '== multi-short invalid value =='
set +e
"$SAMPLE_CLI" plan -M nope repo 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK-NEXT: {{^}}== multi-short invalid value =={{$}}
# CHECK-NEXT: {{^}}error: invalid value for -m: 'nope' (expected a base-10 integer){{$}}
# CHECK-NEXT: {{^}}status=2{{$}}

echo '== multi-short duplicate =='
set +e
"$SAMPLE_CLI" plan -M 1 -m 2 repo 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK-NEXT: {{^}}== multi-short duplicate =={{$}}
# CHECK-NEXT: {{^}}error: option specified multiple times: -m{{$}}
# CHECK-NEXT: {{^}}status=2{{$}}

echo '== silent parse diagnostic =='
set +e
"$SAMPLE_CLI" plan --silent nope repo 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK-NEXT: {{^}}== silent parse diagnostic =={{$}}
# CHECK-NEXT: {{^}}error: invalid value for --silent: 'nope'{{$}}
# CHECK-NEXT: {{^}}status=2{{$}}

echo '== negate-only silent diagnostic =='
set +e
"$SAMPLE_CLI" plan --disable-feature repo 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK-NEXT: {{^}}== negate-only silent diagnostic =={{$}}
# CHECK-NEXT: {{^}}error: invalid value for --disable-feature: '--disable-feature'{{$}}
# CHECK-NEXT: {{^}}status=2{{$}}

echo '== explicit apply parse =='
"$SAMPLE_CLI" apply --force --retries 3 repo 42 2>&1
# CHECK-NEXT: {{^}}== explicit apply parse =={{$}}
# CHECK-NEXT: {{^}}command: apply{{$}}
# CHECK-NEXT: {{^}}force: true (cli){{$}}
# CHECK-NEXT: {{^}}retries: 3 (cli){{$}}
# CHECK-NEXT: {{^}}target: repo (cli){{$}}
# CHECK-NEXT: {{^}}count: 42 (cli){{$}}

echo '== invalid apply count =='
set +e
"$SAMPLE_CLI" apply repo nope 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK-NEXT: {{^}}== invalid apply count =={{$}}
# CHECK-NEXT: {{^}}error: invalid value for COUNT: 'nope' (expected a base-10 integer){{$}}
# CHECK-NEXT: {{^}}status=2{{$}}

echo '== extra apply positional =='
set +e
"$SAMPLE_CLI" apply repo 42 extra 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK-NEXT: {{^}}== extra apply positional =={{$}}
# CHECK-NEXT: {{^}}error: unexpected positional argument: 'extra'{{$}}
# CHECK-NEXT: {{^}}status=2{{$}}

echo '== labels command help =='
env COLUMNS=80 "$SAMPLE_CLI" labels --help 2>&1
# CHECK-NEXT: {{^}}== labels command help =={{$}}
# CHECK-NEXT: {{^}}Exercise help and diagnostic label fallbacks.{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Usage: sample-cli-no-default labels [options] [--] FILE2_COUNT{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Positional arguments:{{$}}
# CHECK-NEXT: {{^}}  FILE2_COUNT               Fallback positional.{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Options:{{$}}
# CHECK-NEXT: {{^}}  -s SIZE{{$}}
# CHECK-NEXT: {{^}}  --item NAME...{{$}}
# CHECK-NEXT: {{^}}  --positive-only           Synthetic negatable option without declared negative{{$}}
# CHECK-NEXT: {{^}}                            aliases.{{$}}
# CHECK-NEXT: {{^}}  -h, --help                Show this help message and exit.{{$}}
# CHECK-NEXT: {{^$}}

echo '== unexplained command help =='
env COLUMNS=80 "$SAMPLE_CLI" unexplained --help 2>&1
# CHECK-NEXT: {{^}}== unexplained command help =={{$}}
# CHECK-NEXT: {{^}}Usage: sample-cli-no-default unexplained [IMAGE]{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Positional arguments:{{$}}
# CHECK-NEXT: {{^}}  IMAGE                     Optional unexplained sample positional.{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Options:{{$}}
# CHECK-NEXT: {{^}}  -h, --help                Show this help message and exit.{{$}}
# CHECK-NEXT: {{^$}}

echo '== unexplained parse =='
"$SAMPLE_CLI" unexplained alpha.png 2>&1
# CHECK-NEXT: {{^}}== unexplained parse =={{$}}
# CHECK-NEXT: {{^}}command: unexplained{{$}}
# CHECK-NEXT: {{^}}image: alpha.png (cli){{$}}

echo '== labels parse =='
"$SAMPLE_CLI" labels -s value --item alpha --item beta 7 2>&1
# CHECK-NEXT: {{^}}== labels parse =={{$}}
# CHECK-NEXT: {{^}}command: labels{{$}}
# CHECK-NEXT: {{^}}short_only: value (cli){{$}}
# CHECK-NEXT: {{^}}item_name: [alpha, beta] (cli){{$}}
# CHECK-NEXT: {{^}}file2_count: 7 (cli){{$}}

echo '== labels unset list =='
"$SAMPLE_CLI" labels -s value 7 2>&1
# CHECK-NEXT: {{^}}== labels unset list =={{$}}
# CHECK-NEXT: {{^}}command: labels{{$}}
# CHECK-NEXT: {{^}}short_only: value (cli){{$}}
# CHECK-NEXT: {{^}}item_name: <unset>{{$}}
# CHECK-NEXT: {{^}}file2_count: 7 (cli){{$}}

echo '== labels unset short only =='
"$SAMPLE_CLI" labels --item alpha 7 2>&1
# CHECK-NEXT: {{^}}== labels unset short only =={{$}}
# CHECK-NEXT: {{^}}command: labels{{$}}
# CHECK-NEXT: {{^}}short_only: <unset>{{$}}
# CHECK-NEXT: {{^}}item_name: [alpha] (cli){{$}}
# CHECK-NEXT: {{^}}file2_count: 7 (cli){{$}}

echo '== labels invalid positional =='
set +e
"$SAMPLE_CLI" labels nope 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK-NEXT: {{^}}== labels invalid positional =={{$}}
# CHECK-NEXT: {{^}}error: invalid value for FILE2_COUNT: 'nope' (expected a base-10 integer){{$}}
# CHECK-NEXT: {{^}}status=2{{$}}

echo '== repeat-items command help =='
env COLUMNS=80 "$SAMPLE_CLI" repeat-items --help 2>&1
# CHECK-NEXT: {{^}}== repeat-items command help =={{$}}
# CHECK-NEXT: {{^}}Exercise integer positional parsing without a default command.{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Usage: sample-cli-no-default repeat-items COPIES{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Positional arguments:{{$}}
# CHECK-NEXT: {{^}}  COPIES                    Number of synthetic repetitions.{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Options:{{$}}
# CHECK-NEXT: {{^}}  -h, --help                Show this help message and exit.{{$}}
# CHECK-NEXT: {{^$}}

echo '== explicit repeat-items parse =='
"$SAMPLE_CLI" repeat-items 3 2>&1
# CHECK-NEXT: {{^}}== explicit repeat-items parse =={{$}}
# CHECK-NEXT: {{^}}command: repeat-items{{$}}
# CHECK-NEXT: {{^}}copies: 3 (cli){{$}}

echo '== invalid repeat-items count =='
set +e
"$SAMPLE_CLI" repeat-items nope 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK-NEXT: {{^}}== invalid repeat-items count =={{$}}
# CHECK-NEXT: {{^}}error: invalid value for COPIES: 'nope' (expected a base-10 integer){{$}}
# CHECK-NEXT: {{^}}status=2{{$}}

echo '== unknown option with later command help =='
env COLUMNS=80 "$SAMPLE_CLI" --bogus apply --help 2>&1
# CHECK-NEXT: {{^}}== unknown option with later command help =={{$}}
# CHECK-NEXT: {{^}}error: unknown option: '--bogus'{{$}}
# CHECK-NEXT: {{^}}Exercise no-default parsing with two positional arguments.{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Usage: sample-cli-no-default apply [options] [--] TARGET COUNT{{$}}

echo '== help after double dash ignored =='
set +e
env COLUMNS=80 "$SAMPLE_CLI" --bogus -- --help 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK: {{^}}== help after double dash ignored =={{$}}
# CHECK-NEXT: {{^}}error: unknown option: '--bogus'{{$}}
# CHECK-NEXT: {{^}}status=2{{$}}

echo '== unknown command with help =='
env COLUMNS=80 "$SAMPLE_CLI" --help missing 2>&1
# CHECK-NEXT: {{^}}== unknown command with help =={{$}}
# CHECK-NEXT: {{^}}error: unknown command: 'missing'{{$}}
# CHECK: {{^}}Sample CLI with multiple commands and no default command.{{$}}

echo '== early error with later help =='
env COLUMNS=80 "$SAMPLE_CLI" -f 1 missing -c 1 --help 2>&1
# CHECK: {{^}}== early error with later help =={{$}}
# CHECK-NEXT: {{^}}error: unknown option: '-f'{{$}}
# CHECK: {{^}}Sample CLI with multiple commands and no default command.{{$}}

echo '== missing command =='
set +e
"$SAMPLE_CLI" 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK: {{^}}== missing command =={{$}}
# CHECK-NEXT: {{^}}error: missing command{{$}}
# CHECK-NEXT: {{^}}status=2{{$}}
