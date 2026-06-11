#!/usr/bin/env run-and-check
# SPDX-License-Identifier: MIT-0
# RUN: sh %s

# Exercise top-level imgneko CLI behavior that is not specific to a subcommand.

set -eu

. "$IMGNEKO_ROOT_DIR/testing/tests/imgneko/cli/common.sh"
setup_imgneko_cli

echo '== program help =='
env COLUMNS=80 "$IMGNEKO" --help 2>&1
# CHECK:      {{^}}== program help =={{$}}
# CHECK-NEXT: {{^}}Terminal image placeholder utilities.{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Usage: imgneko [options] <command> [command options]{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Commands:{{$}}
# CHECK-NEXT: {{^}}  placeholder               Print a terminal image placeholder.{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Options:{{$}}
# CHECK-NEXT: {{^}}  -v, --version             Show program version and exit.{{$}}
# CHECK-NEXT: {{^}}  -h, --help                Show overall help and exit.{{$}}
# CHECK-NEXT: {{^$}}

echo '== version =='
# Verify that the top-level version option keeps reporting build information.
"$IMGNEKO" --version
# CHECK-NEXT: {{^== version ==$}}
# CHECK-NEXT: {{^version: .+$}}

echo '== missing command =='
set +e
"$IMGNEKO" 2>&1
printf 'status=%d\n' "$?"
set -e
# CHECK:      {{^== missing command ==$}}
# CHECK-NEXT: {{^error: missing command$}}
# CHECK-NEXT: {{^status=2$}}
