#!/usr/bin/env run-and-check
# RUN: sh %s

# Exercise the sample CLI with a default command and no top-level schema.

set -eu

fail() {
    echo "$1" >&2
    exit 1
}

SAMPLE_CLI=$IMGNEKO_BUILD_DIR/bin/sample-cli-default-no-top-level

[ -x "$SAMPLE_CLI" ] || fail "missing sample-cli-default-no-top-level binary: $SAMPLE_CLI"

echo '== default command help without command =='
env COLUMNS=80 "$SAMPLE_CLI" --help 2>&1
# CHECK:      {{^}}== default command help without command =={{$}}
# CHECK-NEXT: {{^}}Sample CLI with a default command and no top-level schema.{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Usage: sample-cli-default-no-top-level <command> [options]{{$}}
# CHECK-NEXT: {{^}}       sample-cli-default-no-top-level [show options] [--] [IMAGE]{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Commands:{{$}}
# CHECK-NEXT: {{^}}  show (default)            Exercise default-command help without top-level{{$}}
# CHECK-NEXT: {{^}}                            options.{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Options:{{$}}
# CHECK-NEXT: {{^}}  -h, --help                Show overall help and exit.{{$}}
# CHECK-NEXT: {{^$}}

echo '== default command help =='
env COLUMNS=80 "$SAMPLE_CLI" show --help 2>&1
# CHECK-NEXT: {{^}}== default command help =={{$}}
# CHECK-NEXT: {{^}}Exercise default-command help without top-level options.{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Usage: sample-cli-default-no-top-level show [options] [--] [IMAGE]{{$}}
# CHECK-NEXT: {{^}}       sample-cli-default-no-top-level [options] [--] [IMAGE]{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Positional arguments:{{$}}
# CHECK-NEXT: {{^}}  IMAGE                     Synthetic optional image.{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Options:{{$}}
# CHECK-NEXT: {{^}}  -n, --count N             Synthetic count. (default: 3){{$}}
# CHECK-NEXT: {{^}}  -h, --help                Show this help message and exit.{{$}}
# CHECK-NEXT: {{^$}}

echo '== default command parse =='
"$SAMPLE_CLI" alpha.png 2>&1
# CHECK-NEXT: {{^}}== default command parse =={{$}}
# CHECK-NEXT: {{^}}command: show{{$}}
# CHECK-NEXT: {{^}}count: 3 (default){{$}}
# CHECK-NEXT: {{^}}image: alpha.png (cli){{$}}

echo '== default command without image =='
"$SAMPLE_CLI" 2>&1
# CHECK-NEXT: {{^}}== default command without image =={{$}}
# CHECK-NEXT: {{^}}command: show{{$}}
# CHECK-NEXT: {{^}}count: 3 (default){{$}}
# CHECK-NEXT: {{^}}image: <unset>{{$}}

echo '== unknown option =='
set +e
"$SAMPLE_CLI" --bogus 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK-NEXT: {{^}}== unknown option =={{$}}
# CHECK-NEXT: {{^}}error: unknown option: --bogus{{$}}
# CHECK-NEXT: {{^}}status=2{{$}}
