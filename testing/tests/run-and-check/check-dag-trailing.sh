#!/usr/bin/env run-and-check
# SPDX-License-Identifier: MIT-0
# RUN: sh %s

# Verify that a trailing CHECK-DAG group can finish a test without a following
# CHECK directive.

printf '%s\n' 'start'
printf '%s\n' 'tail b'
printf '%s\n' 'tail a'
# CHECK: start
# CHECK-DAG: tail a
# CHECK-DAG: tail b
