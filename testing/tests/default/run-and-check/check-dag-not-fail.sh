#!/usr/bin/env run-and-check
# SPDX-License-Identifier: MIT-0
# XFAIL
# RUN: sh %s

# Verify that CHECK-NOT after a CHECK-DAG group inspects the region from the
# latest CHECK-DAG match above it to the first CHECK-DAG below it.

printf '%s\n' 'before'
printf '%s\n' 'forbidden'
printf '%s\n' 'after'
# CHECK-DAG: before
# CHECK-NOT: forbidden
# CHECK-DAG: after
