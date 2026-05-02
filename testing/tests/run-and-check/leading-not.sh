#!/usr/bin/env run-and-check
# SPDX-License-Identifier: MIT-0
# RUN: sh %s

# Verify that a leading CHECK-NOT only inspects the output before the first
# positive CHECK match.

printf '%s\n' 'anchor'
printf '%s\n' 'forbidden later'
# CHECK-NOT: forbidden
# CHECK: anchor
