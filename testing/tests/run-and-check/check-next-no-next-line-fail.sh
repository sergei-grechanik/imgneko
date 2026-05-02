#!/usr/bin/env run-and-check
# SPDX-License-Identifier: MIT-0
# XFAIL
# RUN: sh %s

# Verify that CHECK-NEXT reports when the previous positive match was already on
# the last output line.

printf '%s\n' 'alpha'
# CHECK: alpha
# CHECK-NEXT: beta
