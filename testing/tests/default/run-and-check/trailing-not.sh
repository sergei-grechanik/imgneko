#!/usr/bin/env run-and-check
# SPDX-License-Identifier: MIT-0
# RUN: sh %s

# Verify that a trailing CHECK-NOT only inspects the output after the last
# positive CHECK match.

printf '%s\n' 'forbidden early'
printf '%s\n' 'anchor'
# CHECK: anchor
# CHECK-NOT: forbidden
