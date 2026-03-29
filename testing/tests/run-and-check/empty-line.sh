#!/usr/bin/env run-and-check
# RUN: sh %s

# Verify that zero-length anchored matches work on empty output lines.

printf 'before\n\nafter\n'
# CHECK: before
# CHECK-NEXT: {{^$}}
# CHECK-NEXT: after
