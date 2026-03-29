#!/usr/bin/env run-and-check
# XFAIL
# RUN: sh %s

# Verify that a trailing CHECK-NOT rejects forbidden output after the last
# positive CHECK match.

printf '%s\n' 'anchor'
printf '%s\n' 'forbidden later'
# CHECK: anchor
# CHECK-NOT: forbidden
