#!/usr/bin/env run-and-check
# SPDX-License-Identifier: MIT-0
# XFAIL
# RUN: sh %s

# Verify that CHECK-NOT still rejects a true whole-line match between two
# surrounding CHECK directives.

printf '%s\n' 'a'
printf '%s\n' 'b'
printf '%s\n' 'c'
# CHECK: a
# CHECK-NOT: {{^b$}}
# CHECK: c
