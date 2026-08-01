#!/usr/bin/env run-and-check
# SPDX-License-Identifier: MIT-0
# RUN: sh %s

# Exercise the sample CLI without subcommands built on top of the generic
# option parser utility.

set -eu

fail() {
    echo "$1" >&2
    exit 1
}

SAMPLE_CLI_NOCMD=$IMGNEKO_BUILD_DIR/bin/sample-cli-nocmd

[ -x "$SAMPLE_CLI_NOCMD" ] || fail "missing sample-cli-nocmd binary: $SAMPLE_CLI_NOCMD"

echo '== help =='
env COLUMNS=80 "$SAMPLE_CLI_NOCMD" --help 2>&1
# CHECK:      {{^}}== help =={{$}}
# CHECK-NEXT: {{^}}Sample CLI without commands that exercises help, version, options, and fixed positional arguments.{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Usage: sample-cli-nocmd [options] [--] SOURCE DEST [PROFILE]{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Positional arguments:{{$}}
# CHECK-NEXT: {{^}}  SOURCE                    Input artifact to process.{{$}}
# CHECK-NEXT: {{^}}  DEST                      Output artifact path.{{$}}
# CHECK-NEXT: {{^}}  PROFILE                   Synthetic execution profile.{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Options:{{$}}
# CHECK-NEXT: {{^}}  -h, --help                Show this help message and exit.{{$}}
# CHECK-NEXT: {{^}}  --version                 Show program version and exit.{{$}}
# CHECK-NEXT: {{^}}  -v, --verbose             Enable verbose logging.{{$}}
# CHECK-NEXT: {{^}}  -r, --retries N           Retry the synthetic run N times. (default: 3){{$}}
# CHECK-NEXT: {{^}}  --color BOOL              Force colored status output. (default: true){{$}}
# CHECK-NEXT: {{^}}  -c, --config FILE         Read settings from FILE before processing inputs.{{$}}
# CHECK-NEXT: {{^$}}

echo '== help without COLUMNS =='
env -u COLUMNS "$SAMPLE_CLI_NOCMD" --help 2>&1
# CHECK-NEXT: {{^}}== help without COLUMNS =={{$}}
# CHECK-NEXT: {{^}}Sample CLI without commands that exercises help, version, options, and fixed positional arguments.{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Usage: sample-cli-nocmd [options] [--] SOURCE DEST [PROFILE]{{$}}
# CHECK:      {{^}}Positional arguments:{{$}}
# CHECK:      {{^}}  PROFILE                   Synthetic execution profile.{{$}}
# CHECK:      {{^}}Options:{{$}}
# CHECK:      {{^}}  -c, --config FILE         Read settings from FILE before processing inputs.{{$}}

echo '== zero width help =='
env COLUMNS=0 "$SAMPLE_CLI_NOCMD" --help 2>&1
# CHECK: {{^}}== zero width help =={{$}}
# CHECK: {{^}}Usage: sample-cli-nocmd {{.*}}SOURCE DEST{{.*}}{{$}}

echo '== invalid COLUMNS help =='
env COLUMNS=oops "$SAMPLE_CLI_NOCMD" --help 2>&1
# CHECK: {{^}}== invalid COLUMNS help =={{$}}
# CHECK: {{^}}Usage: sample-cli-nocmd {{.*}}SOURCE DEST{{.*}}{{$}}

echo '== narrow help =='
env COLUMNS=20 "$SAMPLE_CLI_NOCMD" --help 2>&1
# CHECK: {{^}}== narrow help =={{$}}
# CHECK: {{^}}Usage: sample-cli-nocmd {{.*}}SOURCE DEST{{.*}}{{$}}
# CHECK: {{^}}  PROFILE    Synthetic execution{{$}}
# CHECK: {{^}}             profile.{{$}}
# CHECK: {{^}}  -h, --help Show this help message and{{$}}
# CHECK: {{^}}             exit.{{$}}

echo '== version =='
"$SAMPLE_CLI_NOCMD" --version 2>&1
# CHECK:      {{^}}== version =={{$}}
# CHECK-NEXT: {{^}}sample-cli-nocmd {{.+}}{{$}}

echo '== parse =='
"$SAMPLE_CLI_NOCMD" -v -r5 --color=OFF --config settings.toml -- input.bin output.bin release 2>&1
# CHECK-NEXT: {{^}}== parse =={{$}}
# CHECK-NEXT: {{^}}version: false (default){{$}}
# CHECK-NEXT: {{^}}verbose: true (cli){{$}}
# CHECK-NEXT: {{^}}retries: 5 (cli){{$}}
# CHECK-NEXT: {{^}}color: false (cli){{$}}
# CHECK-NEXT: {{^}}config: settings.toml (cli){{$}}
# CHECK-NEXT: {{^}}source: input.bin (cli){{$}}
# CHECK-NEXT: {{^}}destination: output.bin (cli){{$}}
# CHECK-NEXT: {{^}}profile: release (cli){{$}}

echo '== bool yes =='
"$SAMPLE_CLI_NOCMD" --color yes input.bin output.bin release 2>&1
# CHECK-NEXT: {{^}}== bool yes =={{$}}
# CHECK-NEXT: {{^}}version: false (default){{$}}
# CHECK-NEXT: {{^}}verbose: false (default){{$}}
# CHECK-NEXT: {{^}}retries: 3 (default){{$}}
# CHECK-NEXT: {{^}}color: true (cli){{$}}
# CHECK-NEXT: {{^}}config: <unset>{{$}}
# CHECK-NEXT: {{^}}source: input.bin (cli){{$}}
# CHECK-NEXT: {{^}}destination: output.bin (cli){{$}}
# CHECK-NEXT: {{^}}profile: release (cli){{$}}

echo '== bool no =='
"$SAMPLE_CLI_NOCMD" --color no input.bin output.bin release 2>&1
# CHECK-NEXT: {{^}}== bool no =={{$}}
# CHECK-NEXT: {{^}}version: false (default){{$}}
# CHECK-NEXT: {{^}}verbose: false (default){{$}}
# CHECK-NEXT: {{^}}retries: 3 (default){{$}}
# CHECK-NEXT: {{^}}color: false (cli){{$}}
# CHECK-NEXT: {{^}}config: <unset>{{$}}
# CHECK-NEXT: {{^}}source: input.bin (cli){{$}}
# CHECK-NEXT: {{^}}destination: output.bin (cli){{$}}
# CHECK-NEXT: {{^}}profile: release (cli){{$}}

echo '== short cluster rejected =='
set +e
"$SAMPLE_CLI_NOCMD" -vr5 input.bin output.bin release 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK-NEXT: {{^}}== short cluster rejected =={{$}}
# CHECK-NEXT: {{^}}error: unknown option: '-vr5'{{$}}
# CHECK-NEXT: {{^}}status=2{{$}}

echo '== defaults =='
"$SAMPLE_CLI_NOCMD" input.bin output.bin 2>&1
# CHECK-NEXT: {{^}}== defaults =={{$}}
# CHECK-NEXT: {{^}}version: false (default){{$}}
# CHECK-NEXT: {{^}}verbose: false (default){{$}}
# CHECK-NEXT: {{^}}retries: 3 (default){{$}}
# CHECK-NEXT: {{^}}color: true (default){{$}}
# CHECK-NEXT: {{^}}config: <unset>{{$}}
# CHECK-NEXT: {{^}}source: input.bin (cli){{$}}
# CHECK-NEXT: {{^}}destination: output.bin (cli){{$}}
# CHECK-NEXT: {{^}}profile: <unset>{{$}}

echo '== missing source =='
set +e
"$SAMPLE_CLI_NOCMD" 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK-NEXT: {{^}}== missing source =={{$}}
# CHECK-NEXT: {{^}}error: missing required argument: SOURCE{{$}}
# CHECK-NEXT: {{^}}status=2{{$}}

echo '== missing positional =='
set +e
"$SAMPLE_CLI_NOCMD" input.bin 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK-NEXT: {{^}}== missing positional =={{$}}
# CHECK-NEXT: {{^}}error: missing required argument: DEST{{$}}
# CHECK-NEXT: {{^}}status=2{{$}}

echo '== extra positional =='
set +e
"$SAMPLE_CLI_NOCMD" input.bin output.bin release extra 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK-NEXT: {{^}}== extra positional =={{$}}
# CHECK-NEXT: {{^}}error: unexpected positional argument: 'extra'{{$}}
# CHECK-NEXT: {{^}}status=2{{$}}

echo '== extra positional after double dash =='
set +e
"$SAMPLE_CLI_NOCMD" input.bin output.bin release -- extra 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK-NEXT: {{^}}== extra positional after double dash =={{$}}
# CHECK-NEXT: {{^}}error: unexpected positional argument: 'extra'{{$}}
# CHECK-NEXT: {{^}}status=2{{$}}

echo '== unknown option =='
set +e
"$SAMPLE_CLI_NOCMD" --bogus 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK-NEXT: {{^}}== unknown option =={{$}}
# CHECK-NEXT: {{^}}error: unknown option: '--bogus'{{$}}
# CHECK-NEXT: {{^}}status=2{{$}}

echo '== invalid value =='
set +e
"$SAMPLE_CLI_NOCMD" --color maybe input.bin output.bin release 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK-NEXT: {{^}}== invalid value =={{$}}
# CHECK-NEXT: {{^}}error: invalid value for --color: 'maybe' (expected one of true, false, yes, no, on, off, 1, or 0){{$}}
# CHECK-NEXT: {{^}}status=2{{$}}
