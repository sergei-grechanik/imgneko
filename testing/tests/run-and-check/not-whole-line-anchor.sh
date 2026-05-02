#!/usr/bin/env run-and-check
# SPDX-License-Identifier: MIT-0
# RUN: sh %s

# Verify that CHECK-NOT applies ^ and $ to the full output line, not just to a
# partial region between surrounding positive matches.

printf '%s\n' 'ab'
printf '%s\n' 'bc'
# CHECK: a
# CHECK-NOT: {{^b$}}
# CHECK: c

printf '%s\n' 'ab'
printf '%s\n' 'xb'
printf '%s\n' 'dbc'
# CHECK: a
# CHECK-NOT: {{^b}}
# CHECK: c

printf '%s\n' 'abx'
printf '%s\n' 'bx'
printf '%s\n' 'bc'
# CHECK: a
# CHECK-NOT: {{b$}}
# CHECK: c

printf '%s\n' 'ab'
printf '%s\n' 'xb'
printf '%s\n' 'dbc'
# CHECK: a
# CHECK-NOT: {{(^b|qq)}}
# CHECK: c

printf '%s\n' 'abx'
printf '%s\n' 'bx'
printf '%s\n' 'bc'
# CHECK: a
# CHECK-NOT: {{(qq|b$)}}
# CHECK: c
