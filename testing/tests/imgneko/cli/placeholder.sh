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
# CHECK-NEXT: {{^\x1b\[0m\x1b\[38;2;0;4;210m\xf4\x8e\xbb\xae\xcc\x85\xcc\x85\xf4\x8e\xbb\xae\xcc\x85\xcc\x8d\xf4\x8e\xbb\xae\xcc\x85\xcc\x8e\x1b\[0m$}}
# CHECK-NEXT: {{^\x1b\[0m\x1b\[38;2;0;4;210m\xf4\x8e\xbb\xae\xcc\x8d\xcc\x85\xf4\x8e\xbb\xae\xcc\x8d\xcc\x8d\xf4\x8e\xbb\xae\xcc\x8d\xcc\x8e\x1b\[0m$}}

echo '== placement id bytes =='
# Verify that --placement-id overrides the default placement ID and emits its
# underline color metadata.
"$IMGNEKO" placeholder --id 7 --placement-id 8 --rows 1 --cols 1
# CHECK-NEXT: {{^}}== placement id bytes =={{$}}
# CHECK-NEXT: {{^\x1b\[0m\x1b\[38;5;7m\x1b\[58;2;0;0;8m\xf4\x8e\xbb\xae\xcc\x85\xcc\x85\x1b\[0m$}}

echo '== requested dimensions =='
# Run the documented 20x10 form and check representative start/end bytes. The
# full output is large, so this intentionally leaves the middle wildcarded.
"$IMGNEKO" placeholder --id 1234 --rows 10 --cols 20
# CHECK-NEXT: {{^}}== requested dimensions =={{$}}
# CHECK-NEXT: {{^\x1b\[0m\x1b\[38;2;0;4;210m\xf4\x8e\xbb\xae\xcc\x85\xcc\x85.*\xf4\x8e\xbb\xae\xcc\x85\xcd\xa5\x1b\[0m$}}
# CHECK: {{^\x1b\[0m\x1b\[38;2;0;4;210m\xf4\x8e\xbb\xae\xcd\x8a\xcc\x85.*\xf4\x8e\xbb\xae\xcd\x8a\xcd\xa5\x1b\[0m$}}
