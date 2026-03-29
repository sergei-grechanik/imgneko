#!/usr/bin/env run-and-check
# RUN: sh %s

# Verify that a trailing CHECK-NOT only inspects the output after the last
# positive CHECK match.

printf '%s\n' 'forbidden early'
printf '%s\n' 'anchor'
# CHECK: anchor
# CHECK-NOT: forbidden
