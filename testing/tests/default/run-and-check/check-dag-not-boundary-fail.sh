#!/usr/bin/env run-and-check
# SPDX-License-Identifier: MIT-0
# XFAIL
# RUN: sh %s

# Verify that CHECK-NOT before a CHECK-DAG group checks through the earliest
# output match in that group.

printf '%s\n' 'start'
printf '%s\n' 'forbidden'
printf '%s\n' 'after'
printf '%s\n' 'end'
# CHECK: start
# CHECK-NOT: forbidden
# CHECK-DAG: after
# CHECK: end
