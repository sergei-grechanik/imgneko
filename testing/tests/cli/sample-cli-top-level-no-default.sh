#!/usr/bin/env run-and-check
# SPDX-License-Identifier: MIT-0
# RUN: sh %s

# Exercise the sample CLI with top-level options and no default command.

set -eu

fail() {
    echo "$1" >&2
    exit 1
}

SAMPLE_CLI=$IMGNEKO_BUILD_DIR/bin/sample-cli-top-level-no-default

[ -x "$SAMPLE_CLI" ] || fail "missing sample-cli-top-level-no-default binary: $SAMPLE_CLI"

echo '== program help =='
env COLUMNS=80 "$SAMPLE_CLI" --help 2>&1
# CHECK:      {{^}}== program help =={{$}}
# CHECK-NEXT: {{^}}Sample CLI with top-level options and no default command.{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Usage: sample-cli-top-level-no-default [options] <command> [command options]{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Commands:{{$}}
# CHECK-NEXT: {{^}}  show                      Exercise explicit command selection with top-level{{$}}
# CHECK-NEXT: {{^}}                            options.{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Options:{{$}}
# CHECK-NEXT: {{^}}  -c, --config FILE         Read a synthetic config file.{{$}}
# CHECK-NEXT: {{^}}  --profile NAME            Select a synthetic top-level profile.{{$}}
# CHECK-NEXT: {{^}}  -h, --help                Show overall help and exit.{{$}}
# CHECK-NEXT: {{^$}}

echo '== top-level only parse =='
"$SAMPLE_CLI" --profile dev 2>&1
# CHECK-NEXT: {{^}}== top-level only parse =={{$}}
# CHECK-NEXT: {{^}}command: <none>{{$}}
# CHECK-NEXT: {{^}}config: <unset>{{$}}
# CHECK-NEXT: {{^}}profile: dev (cli){{$}}

echo '== explicit command parse =='
"$SAMPLE_CLI" -c settings.toml show beta.png 2>&1
# CHECK-NEXT: {{^}}== explicit command parse =={{$}}
# CHECK-NEXT: {{^}}command: show{{$}}
# CHECK-NEXT: {{^}}config: settings.toml (cli){{$}}
# CHECK-NEXT: {{^}}profile: <unset>{{$}}
# CHECK-NEXT: {{^}}image: beta.png (cli){{$}}

echo '== empty parse =='
"$SAMPLE_CLI" 2>&1
# CHECK-NEXT: {{^}}== empty parse =={{$}}
# CHECK-NEXT: {{^}}command: <none>{{$}}
# CHECK-NEXT: {{^}}config: <unset>{{$}}
# CHECK-NEXT: {{^}}profile: <unset>{{$}}

echo '== unknown option =='
set +e
"$SAMPLE_CLI" --bogus 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK-NEXT: {{^}}== unknown option =={{$}}
# CHECK-NEXT: {{^}}error: unknown option: '--bogus'{{$}}
# CHECK-NEXT: {{^}}status=2{{$}}
