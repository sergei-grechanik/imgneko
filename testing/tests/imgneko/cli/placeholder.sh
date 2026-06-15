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
# CHECK-NEXT: {{^}}  --id ID                   Image ID to encode in the placeholder, as decimal or{{$}}
# CHECK-NEXT: {{^}}                            0x-prefixed hex.{{$}}
# CHECK-NEXT: {{^}}  --placement-id ID         Placement ID to encode in the placeholder, as{{$}}
# CHECK-NEXT: {{^}}                            decimal or 0x-prefixed hex. (default: 0){{$}}
# CHECK-NEXT: {{^}}  -D, --diacritics MODE     Diacritic mode: minimal, default, or complete.{{$}}
# CHECK-NEXT: {{^}}  --grapheme-only           Emit grapheme-only output without SGR colors.{{$}}
# CHECK-NEXT: {{^}}  --bg BG                   Background color or pattern.{{$}}
# CHECK-NEXT: {{^}}  -p, --place CxR           Placeholder size as COLSxROWS terminal cells.{{$}}
# CHECK-NEXT: {{^}}  -r, --rows ROWS           Placeholder height in terminal cells.{{$}}
# CHECK-NEXT: {{^}}  -c, --cols COLS           Placeholder width in terminal cells.{{$}}
# CHECK-NEXT: {{^}}  -h, --help                Show this help message and exit.{{$}}
# CHECK-NEXT: {{^$}}

echo '== placeholder bytes =='
# Verify the stdout bytes for the requested command shape using a compact 3x2
# rectangle so the expected byte sequence remains readable.
"$IMGNEKO" placeholder --id 1234 --rows 2 --cols 3
# CHECK-NEXT: {{^}}== placeholder bytes =={{$}}
# CHECK-NEXT: {{^\x1b\[0m\x1b\[38;2;}}[[rgb(1234)]]m[[ph(0, "0:2")]]{{\x1b\[0m$}}
# CHECK-NEXT: {{^\x1b\[0m\x1b\[38;2;}}[[rgb(1234)]]m[[ph(1, "0:2")]]{{\x1b\[0m$}}

echo '== short dimension options =='
# Verify that -r and -c are aliases for --rows and --cols.
"$IMGNEKO" placeholder --id 1234 -r 1 -c 2
# CHECK-NEXT: {{^}}== short dimension options =={{$}}
# CHECK-NEXT: {{^\x1b\[0m\x1b\[38;2;}}[[rgb(1234)]]m[[ph(0, "0:1")]]{{\x1b\[0m$}}

echo '== place dimensions =='
# Verify that -p accepts CxR, where C is columns and R is rows.
"$IMGNEKO" placeholder --id 1234 -p 3x2
# CHECK-NEXT: {{^}}== place dimensions =={{$}}
# CHECK-NEXT: {{^\x1b\[0m\x1b\[38;2;}}[[rgb(1234)]]m[[ph(0, "0:2")]]{{\x1b\[0m$}}
# CHECK-NEXT: {{^\x1b\[0m\x1b\[38;2;}}[[rgb(1234)]]m[[ph(1, "0:2")]]{{\x1b\[0m$}}

echo '== place dimensions X =='
# Capital X works too.
"$IMGNEKO" placeholder --id 1234 -p 5X2
# CHECK-NEXT: {{^}}== place dimensions X =={{$}}
# CHECK-NEXT: {{^\x1b\[0m\x1b\[38;2;}}[[rgb(1234)]]m[[ph(0, "0:4")]]{{\x1b\[0m$}}
# CHECK-NEXT: {{^\x1b\[0m\x1b\[38;2;}}[[rgb(1234)]]m[[ph(1, "0:4")]]{{\x1b\[0m$}}

echo '== diacritics minimal =='
# Minimal mode keeps full metadata on the first cell in a row and omits
# metadata diacritics from the later cells.
"$IMGNEKO" placeholder --id 1234 --place 3x2 -D minimal
# CHECK-NEXT: {{^}}== diacritics minimal =={{$}}
# CHECK-NEXT: {{^\x1b\[0m\x1b\[38;2;}}[[rgb(1234)]]m[[ph(0, 0)]][[ph()]][[ph()]]{{\x1b\[0m$}}
# CHECK-NEXT: {{^\x1b\[0m\x1b\[38;2;}}[[rgb(1234)]]m[[ph(1, 0)]][[ph()]][[ph()]]{{\x1b\[0m$}}

echo '== diacritics minimal =='
# Minimal mode keeps full metadata on the first cell in a row and omits
# metadata diacritics from the later cells.
"$IMGNEKO" placeholder --id 0x12345678 --place 3x2 -D minimal
# CHECK-NEXT: {{^}}== diacritics minimal =={{$}}
# CHECK-NEXT: {{^\x1b\[0m\x1b\[38;2;}}[[rgb(0x12345678)]]m[[ph(0, 0, 0x12345678)]][[ph()]][[ph()]]{{\x1b\[0m$}}
# CHECK-NEXT: {{^\x1b\[0m\x1b\[38;2;}}[[rgb(0x12345678)]]m[[ph(1, 0, 0x12345678)]][[ph()]][[ph()]]{{\x1b\[0m$}}

echo '== diacritics default =='
# The explicit default mode matches the default command behavior.
"$IMGNEKO" placeholder --id 1234 --place 2x1 --diacritics default
# CHECK-NEXT: {{^}}== diacritics default =={{$}}
# CHECK-NEXT: {{^\x1b\[0m\x1b\[38;2;}}[[rgb(1234)]]m[[ph(0, "0:1")]]{{\x1b\[0m$}}

echo '== diacritics complete =='
# Complete mode always emits the high image-ID byte diacritic, even when that
# byte is zero.
"$IMGNEKO" placeholder --id 1234 --place 2x1 --diacritics complete
# CHECK-NEXT: {{^}}== diacritics complete =={{$}}
# CHECK-NEXT: {{^\x1b\[0m\x1b\[38;2;}}[[rgb(1234)]]m[[ph(0, "0:1", 1234)]]{{\x1b\[0m$}}

echo '== grapheme-only output =='
# Grapheme-only output keeps the placeholder graphemes and linefeeds but drops
# the automatic color and reset sequences.
"$IMGNEKO" placeholder --id 0x12345678 --place 20x2 --grapheme-only
# CHECK-NEXT: {{^}}== grapheme-only output =={{$}}
# CHECK-NEXT: {{^}}[[ph(0, "0:19", 0x12345678)]]{{$}}
# CHECK-NEXT: {{^}}[[ph(1, "0:19", 0x12345678)]]{{$}}

echo '== image id high byte =='
# Verify that the optional third placeholder diacritic is based only on the
# high image ID byte while the SGR color still uses the low 24 bits.
# 16777216 = 0x1000000
"$IMGNEKO" placeholder --id 16777216 --rows 1 --cols 1
# CHECK-NEXT: {{^}}== image id high byte =={{$}}
# CHECK-NEXT: {{^\x1b\[0m\x1b\[38;5;0m}}[[ph(0, 0, 16777216)]]{{\x1b\[0m$}}

echo '== hex ids =='
# Verify that image and placement IDs accept 0x-prefixed hexadecimal values.
"$IMGNEKO" placeholder --id 0x01000000 --placement-id 0x123456 \
    --rows 1 --cols 1
# CHECK-NEXT: {{^}}== hex ids =={{$}}
# CHECK-NEXT: {{^\x1b\[0m\x1b\[38;5;0m\x1b\[58;2;18;52;86m}}[[ph(0, 0, 0x01000000)]]{{\x1b\[0m$}}

echo '== placement id bytes =='
# Verify that --placement-id overrides the default placement ID and emits its
# underline color metadata.
"$IMGNEKO" placeholder --id 7 --placement-id 8 --rows 1 --cols 1
# CHECK-NEXT: {{^}}== placement id bytes =={{$}}
# CHECK-NEXT: {{^\x1b\[0m\x1b\[38;5;7m\x1b\[58;2;}}[[rgb(8)]]m[[ph(0, 0)]]{{\x1b\[0m$}}

echo '== background index =='
# A decimal background value is interpreted as a 256-color palette index.
# Leading and trailing whitespace around the expression is ignored.
"$IMGNEKO" placeholder --id 7 --place 2x1 --bg ' 123 '
# CHECK-NEXT: {{^}}== background index =={{$}}
# CHECK-NEXT: {{^\x1b\[0m\x1b\[48;5;123m\x1b\[38;5;7m}}[[ph(0, "0:1")]]{{\x1b\[0m$}}

echo '== background hex =='
# Web-style #rrggbb colors become true-color background SGR sequences.
"$IMGNEKO" placeholder --id 7 --place 1x1 --bg '#ff0012'
# CHECK-NEXT: {{^}}== background hex =={{$}}
# CHECK-NEXT: {{^\x1b\[0m\x1b\[48;2;255;0;18m\x1b\[38;5;7m}}[[ph(0, 0)]]{{\x1b\[0m$}}

echo '== background rgb =='
# rgb() accepts decimal 8-bit channel values with optional whitespace.
"$IMGNEKO" placeholder --id 7 --place 1x1 --bg 'rgb(10, 20, 30)'
# CHECK-NEXT: {{^}}== background rgb =={{$}}
# CHECK-NEXT: {{^\x1b\[0m\x1b\[48;2;10;20;30m\x1b\[38;5;7m}}[[ph(0, 0)]]{{\x1b\[0m$}}

echo '== background checkerboard =='
# Checkerboard alternates the two colors by cell using (col + row) parity.
"$IMGNEKO" placeholder --id 7 --place 2x2 \
    --bg 'checkerboard(rgb(1, 2, 3), #000405)'
# CHECK-NEXT: {{^}}== background checkerboard =={{$}}
# CHECK-NEXT: {{^\x1b\[0m\x1b\[38;5;7m\x1b\[48;2;1;2;3m}}[[ph(0, 0)]]{{\x1b\[48;2;0;4;5m}}[[ph(0, 1)]]{{\x1b\[0m$}}
# CHECK-NEXT: {{^\x1b\[0m\x1b\[38;5;7m\x1b\[48;2;0;4;5m}}[[ph(1, 0)]]{{\x1b\[48;2;1;2;3m}}[[ph(1, 1)]]{{\x1b\[0m$}}

echo '== background horizontal stripes =='
# hstripes alternates the two colors by row.
"$IMGNEKO" placeholder --id 7 --place 2x2 --bg 'hstripes(#010203, 5 )'
# CHECK-NEXT: {{^}}== background horizontal stripes =={{$}}
# CHECK-NEXT: {{^\x1b\[0m\x1b\[48;2;1;2;3m\x1b\[38;5;7m}}[[ph(0, "0:1")]]{{\x1b\[0m$}}
# CHECK-NEXT: {{^\x1b\[0m\x1b\[48;5;5m\x1b\[38;5;7m}}[[ph(1, "0:1")]]{{\x1b\[0m$}}

echo '== background vertical stripes =='
# vstripes alternates the two colors by column.
"$IMGNEKO" placeholder --id 7 --place 2x1 --bg 'vstripes(#010203, 5)'
# CHECK-NEXT: {{^}}== background vertical stripes =={{$}}
# CHECK-NEXT: {{^\x1b\[0m\x1b\[38;5;7m\x1b\[48;2;1;2;3m}}[[ph(0, 0)]]{{\x1b\[48;5;5m}}[[ph(0, 1)]]{{\x1b\[0m$}}

echo '== nested background patterns =='
# Nested patterns are evaluated recursively; hstripes() switches rows while
# each nested vstripes() still switches columns within the selected row.
"$IMGNEKO" placeholder --id 7 --place 4x4 \
    --bg 'hstripes(vstripes(1, 2), vstripes(3, 4))'
# CHECK-NEXT: {{^}}== nested background patterns =={{$}}
# CHECK-NEXT: {{^\x1b\[0m\x1b\[38;5;7m\x1b\[48;5;1m}}[[ph(0, 0)]]{{\x1b\[48;5;2m}}[[ph(0, 1)]]{{\x1b\[48;5;1m}}[[ph(0, 2)]]{{\x1b\[48;5;2m}}[[ph(0, 3)]]{{\x1b\[0m$}}
# CHECK-NEXT: {{^\x1b\[0m\x1b\[38;5;7m\x1b\[48;5;3m}}[[ph(1, 0)]]{{\x1b\[48;5;4m}}[[ph(1, 1)]]{{\x1b\[48;5;3m}}[[ph(1, 2)]]{{\x1b\[48;5;4m}}[[ph(1, 3)]]{{\x1b\[0m$}}
# CHECK-NEXT: {{^\x1b\[0m\x1b\[38;5;7m\x1b\[48;5;1m}}[[ph(2, 0)]]{{\x1b\[48;5;2m}}[[ph(2, 1)]]{{\x1b\[48;5;1m}}[[ph(2, 2)]]{{\x1b\[48;5;2m}}[[ph(2, 3)]]{{\x1b\[0m$}}
# CHECK-NEXT: {{^\x1b\[0m\x1b\[38;5;7m\x1b\[48;5;3m}}[[ph(3, 0)]]{{\x1b\[48;5;4m}}[[ph(3, 1)]]{{\x1b\[48;5;3m}}[[ph(3, 2)]]{{\x1b\[48;5;4m}}[[ph(3, 3)]]{{\x1b\[0m$}}

echo '== nested background with solid stripe =='
# A row-level solid child still emits per cell when its sibling needs per-cell
# evaluation.
"$IMGNEKO" placeholder --id 7 --place 4x4 \
    --bg 'hstripes(vstripes(1, 2), 5)'
# CHECK-NEXT: {{^}}== nested background with solid stripe =={{$}}
# CHECK-NEXT: {{^\x1b\[0m\x1b\[38;5;7m\x1b\[48;5;1m}}[[ph(0, 0)]]{{\x1b\[48;5;2m}}[[ph(0, 1)]]{{\x1b\[48;5;1m}}[[ph(0, 2)]]{{\x1b\[48;5;2m}}[[ph(0, 3)]]{{\x1b\[0m$}}
# CHECK-NEXT: {{^\x1b\[0m\x1b\[38;5;7m\x1b\[48;5;5m}}[[ph(1, 0)]]{{\x1b\[48;5;5m}}[[ph(1, 1)]]{{\x1b\[48;5;5m}}[[ph(1, 2)]]{{\x1b\[48;5;5m}}[[ph(1, 3)]]{{\x1b\[0m$}}
# CHECK-NEXT: {{^\x1b\[0m\x1b\[38;5;7m\x1b\[48;5;1m}}[[ph(2, 0)]]{{\x1b\[48;5;2m}}[[ph(2, 1)]]{{\x1b\[48;5;1m}}[[ph(2, 2)]]{{\x1b\[48;5;2m}}[[ph(2, 3)]]{{\x1b\[0m$}}
# CHECK-NEXT: {{^\x1b\[0m\x1b\[38;5;7m\x1b\[48;5;5m}}[[ph(3, 0)]]{{\x1b\[48;5;5m}}[[ph(3, 1)]]{{\x1b\[48;5;5m}}[[ph(3, 2)]]{{\x1b\[48;5;5m}}[[ph(3, 3)]]{{\x1b\[0m$}}

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
