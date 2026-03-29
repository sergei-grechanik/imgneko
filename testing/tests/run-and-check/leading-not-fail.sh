#!/usr/bin/env run-and-check
# XFAIL
# RUN: sh %s

# Verify that a leading CHECK-NOT rejects forbidden output before the first
# positive CHECK match.

printf '%s\n' 'forbidden first'
printf '%s\n' 'anchor'
# CHECK-NOT: forbidden
# CHECK: anchor
