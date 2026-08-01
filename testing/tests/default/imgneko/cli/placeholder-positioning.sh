#!/usr/bin/env run-and-check
# SPDX-License-Identifier: MIT-0
# RUN: run-in-pty -- sh %s

# Exercise placeholder cursor positioning behavior.

set -eu

. "$IMGNEKO_ROOT_DIR/testing/tests/default/imgneko/cli/common.sh"
setup_imgneko_cli

run_placeholder() {
    "$IMGNEKO" placeholder --id 7 --grapheme-only "$@"
    printf 'END\n'
}

run_placeholder_fail() {
    check_exit_code 2 "$IMGNEKO" placeholder --id 7 --grapheme-only "$@"
}

################################################################################

echo '== movement and initial position matrix =='
# CHECK:      {{^}}== movement and initial position matrix =={{$}}
# Use a 3x3 placeholder to exercise both width-sensitive row movement and
# height-sensitive absolute positioning. This matrix covers the meaningful
# movement/start-position combinations without repeating every final cursor.

# Default (auto) cursor movement.
run_placeholder -s 3x3 -C bottom-right
# CHECK-NEXT: {{^}}[[ph(0, "0:2")]]{{$}}
# CHECK-NEXT: {{^}}[[ph(1, "0:2")]]{{$}}
# CHECK-NEXT: {{^}}[[ph(2, "0:2")]]{{END$}}
run_placeholder -s 3x3 --at-cursor -C bottom-right
# CHECK-NEXT: {{^}}[[ph(0, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[3D\x1bD}}[[ph(1, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[3D\x1bD}}[[ph(2, "0:2")]]{{END$}}
run_placeholder -s 3x3 --at 4,3 -C bottom-right
# CHECK-NEXT: {{^\x1b\[4;5H}}[[ph(0, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[3D\x1bD}}[[ph(1, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[3D\x1bD}}[[ph(2, "0:2")]]{{END$}}
run_placeholder -s 3x3 --at-column 3 -C bottom-right
# CHECK-NEXT: {{^\x1b\[4G}}[[ph(0, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[3D\x1bD}}[[ph(1, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[3D\x1bD}}[[ph(2, "0:2")]]{{END$}}

# Text cursor movement.
run_placeholder -s 3x3 --cursor-movement text -C bottom-right
# CHECK-NEXT: {{^}}[[ph(0, "0:2")]]{{$}}
# CHECK-NEXT: {{^}}[[ph(1, "0:2")]]{{$}}
# CHECK-NEXT: {{^}}[[ph(2, "0:2")]]{{END$}}
run_placeholder_fail -s 3x3 --cursor-movement text --at-cursor -C bottom-right
# CHECK-NEXT: {{^}}error: --cursor-movement text cannot be used with --at-cursor{{$}}
run_placeholder_fail -s 3x3 --cursor-movement text --at 4,3 -C bottom-right
# CHECK-NEXT: {{^}}error: --cursor-movement text requires --at X,Y with X equal to 0{{$}}
run_placeholder_fail -s 3x3 --cursor-movement text --at-column 3 -C bottom-right
# CHECK-NEXT: {{^}}error: --cursor-movement text requires --at-column 0{{$}}
run_placeholder -s 3x3 --cursor-movement text --at 0,3 -C bottom-right
# CHECK-NEXT: {{^\x1b\[4;1H}}[[ph(0, "0:2")]]{{$}}
# CHECK-NEXT: {{^}}[[ph(1, "0:2")]]{{$}}
# CHECK-NEXT: {{^}}[[ph(2, "0:2")]]{{END$}}
run_placeholder -s 3x3 --cursor-movement text --at-column 0 -C bottom-right
# CHECK-NEXT: {{^\x1b\[1G}}[[ph(0, "0:2")]]{{$}}
# CHECK-NEXT: {{^}}[[ph(1, "0:2")]]{{$}}
# CHECK-NEXT: {{^}}[[ph(2, "0:2")]]{{END$}}

# Save-restore cursor movement.
run_placeholder -s 3x3 --cursor-movement save-restore -C bottom-right
# CHECK-NEXT: {{^\x1b\[s}}[[ph(0, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[u\x1bD\x1b\[s}}[[ph(1, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[u\x1bD}}[[ph(2, "0:2")]]{{END$}}
run_placeholder -s 3x3 --cursor-movement save-restore --at-cursor -C bottom-right
# CHECK-NEXT: {{^\x1b\[s}}[[ph(0, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[u\x1bD\x1b\[s}}[[ph(1, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[u\x1bD}}[[ph(2, "0:2")]]{{END$}}
run_placeholder -s 3x3 --cursor-movement save-restore --at 4x3 -C bottom-right
# CHECK-NEXT: {{^\x1b\[4;5H\x1b\[s}}[[ph(0, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[u\x1bD\x1b\[s}}[[ph(1, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[u\x1bD}}[[ph(2, "0:2")]]{{END$}}
run_placeholder -s 3x3 --cursor-movement save-restore --at-column 3 -C bottom-right
# CHECK-NEXT: {{^\x1b\[4G\x1b\[s}}[[ph(0, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[u\x1bD\x1b\[s}}[[ph(1, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[u\x1bD}}[[ph(2, "0:2")]]{{END$}}

# Move left cursor movement.
run_placeholder -s 3x3 --cursor-movement move-left -C bottom-right
# CHECK-NEXT: {{^}}[[ph(0, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[3D\x1bD}}[[ph(1, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[3D\x1bD}}[[ph(2, "0:2")]]{{END$}}
run_placeholder -s 3x3 --cursor-movement move-left --at-cursor -C bottom-right
# CHECK-NEXT: {{^}}[[ph(0, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[3D\x1bD}}[[ph(1, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[3D\x1bD}}[[ph(2, "0:2")]]{{END$}}
run_placeholder -s 3x3 --cursor-movement move-left --at 4,3 -C bottom-right
# CHECK-NEXT: {{^\x1b\[4;5H}}[[ph(0, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[3D\x1bD}}[[ph(1, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[3D\x1bD}}[[ph(2, "0:2")]]{{END$}}
run_placeholder -s 3x3 --cursor-movement move-left --at-column 3 -C bottom-right
# CHECK-NEXT: {{^\x1b\[4G}}[[ph(0, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[3D\x1bD}}[[ph(1, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[3D\x1bD}}[[ph(2, "0:2")]]{{END$}}

# Absolute cursor movement.
run_placeholder -s 3x3 --cursor-movement absolute -C bottom-right
# CHECK-NEXT: {{^\x1b\[1;1H}}[[ph(0, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[2;1H}}[[ph(1, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[3;1H}}[[ph(2, "0:2")]]{{END$}}
run_placeholder_fail -s 3x3 --cursor-movement absolute --at-cursor -C bottom-right
# CHECK-NEXT: {{^}}error: --cursor-movement absolute cannot be used with --at-cursor{{$}}
run_placeholder -s 3x3 --cursor-movement absolute --at 4,3 -C bottom-right
# CHECK-NEXT: {{^\x1b\[4;5H}}[[ph(0, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[5;5H}}[[ph(1, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[6;5H}}[[ph(2, "0:2")]]{{END$}}
run_placeholder_fail -s 3x3 --cursor-movement absolute --at-column 3 -C bottom-right
# CHECK-NEXT: {{^}}error: --cursor-movement absolute cannot be used with --at-column{{$}}

################################################################################

echo '== explicit auto spelling =='
# CHECK-NEXT: {{^}}== explicit auto spelling =={{$}}
# The explicit auto spelling should use the same resolution as the omitted
# default: known start placement selects move-left movement.
run_placeholder -s 3x3 --cursor-movement auto --at-cursor -C bottom-right
# CHECK-NEXT: {{^}}[[ph(0, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[3D\x1bD}}[[ph(1, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[3D\x1bD}}[[ph(2, "0:2")]]{{END$}}

################################################################################

echo '== final cursor: text movement =='
# CHECK-NEXT: {{^}}== final cursor: text movement =={{$}}
# Text movement is the only movement where next-line remains a plain newline
# rather than a newline plus carriage return.
run_placeholder -s 3x3 --cursor-movement text -C next-line
# CHECK-NEXT: {{^}}[[ph(0, "0:2")]]{{$}}
# CHECK-NEXT: {{^}}[[ph(1, "0:2")]]{{$}}
# CHECK-NEXT: {{^}}[[ph(2, "0:2")]]{{$}}
# CHECK-NEXT: {{^}}END{{$}}
run_placeholder -s 3x3 --cursor-movement text -C bottom-left
# CHECK-NEXT: {{^}}[[ph(0, "0:2")]]{{$}}
# CHECK-NEXT: {{^}}[[ph(1, "0:2")]]{{$}}
# CHECK-NEXT: {{^}}[[ph(2, "0:2")]]{{\x1b\[3DEND$}}
run_placeholder -s 3x3 --cursor-movement text -C below-left
# CHECK-NEXT: {{^}}[[ph(0, "0:2")]]{{$}}
# CHECK-NEXT: {{^}}[[ph(1, "0:2")]]{{$}}
# CHECK-NEXT: {{^}}[[ph(2, "0:2")]]{{\x1b\[3D\x1bDEND$}}
run_placeholder -s 3x3 --cursor-movement text -C bottom-right
# CHECK-NEXT: {{^}}[[ph(0, "0:2")]]{{$}}
# CHECK-NEXT: {{^}}[[ph(1, "0:2")]]{{$}}
# CHECK-NEXT: {{^}}[[ph(2, "0:2")]]{{END$}}
run_placeholder -s 3x3 --cursor-movement text -C top-left
# CHECK-NEXT: {{^}}[[ph(0, "0:2")]]{{$}}
# CHECK-NEXT: {{^}}[[ph(1, "0:2")]]{{$}}
# CHECK-NEXT: {{^}}[[ph(2, "0:2")]]{{\x1b\[3D\x1b\[2AEND$}}
run_placeholder -s 3x3 --cursor-movement text -C top-right
# CHECK-NEXT: {{^}}[[ph(0, "0:2")]]{{$}}
# CHECK-NEXT: {{^}}[[ph(1, "0:2")]]{{$}}
# CHECK-NEXT: {{^}}[[ph(2, "0:2")]]{{\x1b\[2AEND$}}

################################################################################

echo '== final cursor: move-left movement =='
# CHECK-NEXT: {{^}}== final cursor: move-left movement =={{$}}
# Move-left covers the non-absolute relative movement path, including the
# non-text next-line sequence.
run_placeholder -s 3x3 --cursor-movement move-left -C next-line
# CHECK-NEXT: {{^}}[[ph(0, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[3D\x1bD}}[[ph(1, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[3D\x1bD}}[[ph(2, "0:2")]]{{$}}
# CHECK-NEXT: {{^\x0dEND$}}
run_placeholder -s 3x3 --cursor-movement move-left -C bottom-left
# CHECK-NEXT: {{^}}[[ph(0, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[3D\x1bD}}[[ph(1, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[3D\x1bD}}[[ph(2, "0:2")]]{{\x1b\[3DEND$}}
run_placeholder -s 3x3 --cursor-movement move-left -C below-left
# CHECK-NEXT: {{^}}[[ph(0, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[3D\x1bD}}[[ph(1, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[3D\x1bD}}[[ph(2, "0:2")]]{{\x1b\[3D\x1bDEND$}}
run_placeholder -s 3x3 --cursor-movement move-left -C bottom-right
# CHECK-NEXT: {{^}}[[ph(0, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[3D\x1bD}}[[ph(1, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[3D\x1bD}}[[ph(2, "0:2")]]{{END$}}
run_placeholder -s 3x3 --cursor-movement move-left -C top-left
# CHECK-NEXT: {{^}}[[ph(0, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[3D\x1bD}}[[ph(1, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[3D\x1bD}}[[ph(2, "0:2")]]{{\x1b\[3D\x1b\[2AEND$}}
run_placeholder -s 3x3 --cursor-movement move-left -C top-right
# CHECK-NEXT: {{^}}[[ph(0, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[3D\x1bD}}[[ph(1, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[3D\x1bD}}[[ph(2, "0:2")]]{{\x1b\[2AEND$}}

################################################################################

echo '== final cursor: save-restore movement =='
# CHECK-NEXT: {{^}}== final cursor: save-restore movement =={{$}}
# Save-restore should still use save/restore row movement while applying every
# final cursor position after the last row.
run_placeholder -s 3x3 --cursor-movement save-restore -C next-line
# CHECK-NEXT: {{^\x1b\[s}}[[ph(0, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[u\x1bD\x1b\[s}}[[ph(1, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[u\x1bD}}[[ph(2, "0:2")]]{{$}}
# CHECK-NEXT: {{^\x0dEND$}}
run_placeholder -s 3x3 --cursor-movement save-restore -C bottom-left
# CHECK-NEXT: {{^\x1b\[s}}[[ph(0, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[u\x1bD\x1b\[s}}[[ph(1, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[u\x1bD\x1b\[s}}[[ph(2, "0:2")]]{{\x1b\[uEND$}}
run_placeholder -s 3x3 --cursor-movement save-restore -C below-left
# CHECK-NEXT: {{^\x1b\[s}}[[ph(0, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[u\x1bD\x1b\[s}}[[ph(1, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[u\x1bD\x1b\[s}}[[ph(2, "0:2")]]{{\x1b\[u\x1bDEND$}}
run_placeholder -s 3x3 --cursor-movement save-restore -C bottom-right
# CHECK-NEXT: {{^\x1b\[s}}[[ph(0, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[u\x1bD\x1b\[s}}[[ph(1, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[u\x1bD}}[[ph(2, "0:2")]]{{END$}}
run_placeholder -s 3x3 --cursor-movement save-restore -C top-left
# CHECK-NEXT: {{^\x1b\[s}}[[ph(0, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[u\x1bD\x1b\[s}}[[ph(1, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[u\x1bD\x1b\[s}}[[ph(2, "0:2")]]{{\x1b\[u\x1b\[2AEND$}}
run_placeholder -s 3x3 --cursor-movement save-restore -C top-right
# CHECK-NEXT: {{^\x1b\[s}}[[ph(0, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[u\x1bD\x1b\[s}}[[ph(1, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[u\x1bD}}[[ph(2, "0:2")]]{{\x1b\[2AEND$}}

################################################################################

echo '== final cursor: absolute movement =='
# CHECK-NEXT: {{^}}== final cursor: absolute movement =={{$}}
# Absolute movement has row-start cursor positioning, so verify every final
# cursor position against that path as well.
run_placeholder -s 3x3 --cursor-movement absolute -C next-line
# CHECK-NEXT: {{^\x1b\[1;1H}}[[ph(0, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[2;1H}}[[ph(1, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[3;1H}}[[ph(2, "0:2")]]{{\x1b\[4;1HEND$}}
run_placeholder -s 3x3 --cursor-movement absolute -C bottom-left
# CHECK-NEXT: {{^\x1b\[1;1H}}[[ph(0, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[2;1H}}[[ph(1, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[3;1H}}[[ph(2, "0:2")]]{{\x1b\[3;1HEND$}}
run_placeholder -s 3x3 --cursor-movement absolute -C below-left
# CHECK-NEXT: {{^\x1b\[1;1H}}[[ph(0, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[2;1H}}[[ph(1, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[3;1H}}[[ph(2, "0:2")]]{{\x1b\[4;1HEND$}}
run_placeholder -s 3x3 --cursor-movement absolute -C bottom-right
# CHECK-NEXT: {{^\x1b\[1;1H}}[[ph(0, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[2;1H}}[[ph(1, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[3;1H}}[[ph(2, "0:2")]]{{END$}}
run_placeholder -s 3x3 --cursor-movement absolute -C top-left
# CHECK-NEXT: {{^\x1b\[1;1H}}[[ph(0, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[2;1H}}[[ph(1, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[3;1H}}[[ph(2, "0:2")]]{{\x1b\[1;1HEND$}}
run_placeholder -s 3x3 --cursor-movement absolute -C top-right
# CHECK-NEXT: {{^\x1b\[1;1H}}[[ph(0, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[2;1H}}[[ph(1, "0:2")]]
# CHECK-SAME: {{^}}{{\x1b\[3;1H}}[[ph(2, "0:2")]]{{\x1b\[1;4HEND$}}

################################################################################

echo '== size-sensitive final cursor cases =='
# CHECK-NEXT: {{^}}== size-sensitive final cursor cases =={{$}}
# These focused cases cover the width and height boundaries not represented by
# the 3x3 matrix: single row, single column, and single cell placeholders.
run_placeholder -s 3x1 --cursor-movement absolute -C top-left
# CHECK-NEXT: {{^\x1b\[1;1H}}[[ph(0, "0:2")]]{{\x1b\[1;1HEND$}}
run_placeholder -s 3x1 --cursor-movement absolute -C top-right
# CHECK-NEXT: {{^\x1b\[1;1H}}[[ph(0, "0:2")]]{{\x1b\[1;4HEND$}}
run_placeholder -s 1x3 --cursor-movement absolute -C bottom-left
# CHECK-NEXT: {{^\x1b\[1;1H}}[[ph(0, 0)]]
# CHECK-SAME: {{^}}{{\x1b\[2;1H}}[[ph(1, 0)]]
# CHECK-SAME: {{^}}{{\x1b\[3;1H}}[[ph(2, 0)]]{{\x1b\[3;1HEND$}}
run_placeholder -s 1x3 --cursor-movement absolute -C below-left
# CHECK-NEXT: {{^\x1b\[1;1H}}[[ph(0, 0)]]
# CHECK-SAME: {{^}}{{\x1b\[2;1H}}[[ph(1, 0)]]
# CHECK-SAME: {{^}}{{\x1b\[3;1H}}[[ph(2, 0)]]{{\x1b\[4;1HEND$}}
run_placeholder -s 1x3 --cursor-movement absolute -C top-left
# CHECK-NEXT: {{^\x1b\[1;1H}}[[ph(0, 0)]]
# CHECK-SAME: {{^}}{{\x1b\[2;1H}}[[ph(1, 0)]]
# CHECK-SAME: {{^}}{{\x1b\[3;1H}}[[ph(2, 0)]]{{\x1b\[1;1HEND$}}
run_placeholder -s 1x1 --cursor-movement absolute -C top-left
# CHECK-NEXT: {{^\x1b\[1;1H}}[[ph(0, 0)]]{{\x1b\[1;1HEND$}}
run_placeholder -s 1x1 --cursor-movement absolute -C top-right
# CHECK-NEXT: {{^\x1b\[1;1H}}[[ph(0, 0)]]{{\x1b\[1;2HEND$}}
run_placeholder -s 1x1 --cursor-movement absolute -C next-line
# CHECK-NEXT: {{^\x1b\[1;1H}}[[ph(0, 0)]]{{\x1b\[2;1HEND$}}
run_placeholder -s 3x1 --cursor-movement move-left -C top-left
# CHECK-NEXT: {{^}}[[ph(0, "0:2")]]{{\x1b\[3DEND$}}
run_placeholder -s 3x1 --cursor-movement move-left -C top-right
# CHECK-NEXT: {{^}}[[ph(0, "0:2")]]{{END$}}
run_placeholder -s 1x3 --cursor-movement move-left -C bottom-right
# CHECK-NEXT: {{^}}[[ph(0, 0)]]
# CHECK-SAME: {{^}}{{\x1b\[1D\x1bD}}[[ph(1, 0)]]
# CHECK-SAME: {{^}}{{\x1b\[1D\x1bD}}[[ph(2, 0)]]{{END$}}
run_placeholder -s 3x1 --cursor-movement save-restore -C top-left
# CHECK-NEXT: {{^\x1b\[s}}[[ph(0, "0:2")]]{{\x1b\[uEND$}}
run_placeholder -s 1x1 --cursor-movement save-restore --at-column 3 -C bottom-right
# CHECK-NEXT: {{^\x1b\[4G}}[[ph(0, 0)]]{{END$}}
