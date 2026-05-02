#!/usr/bin/env run-and-check
# SPDX-License-Identifier: MIT-0
# XFAIL
# RUN: sh %s

# Verify that a trailing CHECK-NOT rejects forbidden output after the last
# positive CHECK match.

printf '%s\n' 'anchor'
printf '%s\n' 'forbidden later'
# CHECK: anchor
# CHECK-NOT: forbidden
