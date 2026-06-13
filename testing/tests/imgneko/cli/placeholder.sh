#!/usr/bin/env run-and-check
# SPDX-License-Identifier: MIT-0
# RUN: run-in-pty -- sh %s

# Exercise the imgneko CLI placeholder command.

set -eu

. "$IMGNEKO_ROOT_DIR/testing/tests/imgneko/cli/common.sh"
setup_imgneko_cli

echo '== placeholder help =='
"$IMGNEKO" placeholder --help 2>&1
# CHECK:      {{^}}== placeholder help =={{$}}
# CHECK-NEXT: {{^}}Print a terminal image placeholder.{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Usage: imgneko placeholder [options]{{$}}
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: {{^}}Options:{{$}}
# CHECK-NEXT: {{^}}  -v, --version             Show program version and exit.{{$}}
# CHECK-NEXT: {{^}}  --id ID                   Image ID to encode in the placeholder.{{$}}
# CHECK-NEXT: {{^}}  --placement-id ID         Placement ID to encode in the placeholder. (default:{{$}}
# CHECK-NEXT: {{^}}                            0){{$}}
# CHECK-NEXT: {{^}}  --rows ROWS               Placeholder height in terminal cells.{{$}}
# CHECK-NEXT: {{^}}  --cols COLS               Placeholder width in terminal cells.{{$}}
# CHECK-NEXT: {{^}}  -h, --help                Show this help message and exit.{{$}}
# CHECK-NEXT: {{^$}}

echo '== placeholder bytes =='
# Verify the stdout bytes for the requested command shape using a compact 3x2
# rectangle so the expected byte sequence remains readable.
"$IMGNEKO" placeholder --id 1234 --rows 2 --cols 3
# CHECK-NEXT: {{^}}== placeholder bytes =={{$}}
# CHECK-NEXT: {{^\x1b\[0m\x1b\[38;2;}}[[rgb(1234)]]m[[ph(0, "0:2")]]{{\x1b\[0m$}}
# CHECK-NEXT: {{^\x1b\[0m\x1b\[38;2;}}[[rgb(1234)]]m[[ph(1, "0:2")]]{{\x1b\[0m$}}

echo '== image id high byte =='
# Verify that the optional third placeholder diacritic is based only on the
# high image ID byte while the SGR color still uses the low 24 bits.
# 16777216 = 0x1000000
"$IMGNEKO" placeholder --id 16777216 --rows 1 --cols 1
# CHECK-NEXT: {{^}}== image id high byte =={{$}}
# CHECK-NEXT: {{^\x1b\[0m\x1b\[38;5;0m}}[[ph(0, 0, 16777216)]]{{\x1b\[0m$}}

echo '== placement id bytes =='
# Verify that --placement-id overrides the default placement ID and emits its
# underline color metadata.
"$IMGNEKO" placeholder --id 7 --placement-id 8 --rows 1 --cols 1
# CHECK-NEXT: {{^}}== placement id bytes =={{$}}
# CHECK-NEXT: {{^\x1b\[0m\x1b\[38;5;7m\x1b\[58;2;}}[[rgb(8)]]m[[ph(0, 0)]]{{\x1b\[0m$}}

echo '== requested dimensions =='
"$IMGNEKO" placeholder --id 1234 --rows 10 --cols 20
# CHECK-NEXT: {{^}}== requested dimensions =={{$}}
# CHECK-NEXT: {{^\x1b\[0m\x1b\[38;2;}}[[rgb(1234)]]m[[ph(0, "0:19")]]{{\x1b\[0m$}}
# CHECK-NEXT: {{^\x1b\[0m\x1b\[38;2;}}[[rgb(1234)]]m[[ph(1, "0:19")]]{{\x1b\[0m$}}
# CHECK-NEXT: {{^\x1b\[0m\x1b\[38;2;}}[[rgb(1234)]]m[[ph(2, "0:19")]]{{\x1b\[0m$}}
# CHECK-NEXT: {{^\x1b\[0m\x1b\[38;2;}}[[rgb(1234)]]m[[ph(3, "0:19")]]{{\x1b\[0m$}}
# CHECK-NEXT: {{^\x1b\[0m\x1b\[38;2;}}[[rgb(1234)]]m[[ph(4, "0:19")]]{{\x1b\[0m$}}
# CHECK-NEXT: {{^\x1b\[0m\x1b\[38;2;}}[[rgb(1234)]]m[[ph(5, "0:19")]]{{\x1b\[0m$}}
# CHECK-NEXT: {{^\x1b\[0m\x1b\[38;2;}}[[rgb(1234)]]m[[ph(6, "0:19")]]{{\x1b\[0m$}}
# CHECK-NEXT: {{^\x1b\[0m\x1b\[38;2;}}[[rgb(1234)]]m[[ph(7, "0:19")]]{{\x1b\[0m$}}
# CHECK-NEXT: {{^\x1b\[0m\x1b\[38;2;}}[[rgb(1234)]]m[[ph(8, "0:19")]]{{\x1b\[0m$}}
# CHECK-NEXT: {{^\x1b\[0m\x1b\[38;2;}}[[rgb(1234)]]m[[ph(9, "0:19")]]{{\x1b\[0m$}}
