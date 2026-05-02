#!/usr/bin/env run-and-check
# SPDX-License-Identifier: MIT-0
# XFAIL
# RUN: sh %s

# Verify that CHECK-NEXT starts searching at the beginning of the next line,
# not in the remaining suffix of the current line.

printf '%s\n' 'alpha beta'
printf '%s\n' 'gamma'
# CHECK: alpha
# CHECK-NEXT: beta
