#!/usr/bin/env run-and-check
# SPDX-License-Identifier: MIT-0
# XFAIL
# RUN: sh %s

# Verify that CHECK-NOT between two CHECK-DAG groups uses the variable captured
# by the group above it, and fails when the forbidden output appears before the
# lower CHECK-DAG group.

printf '%s\n' 'upper old'
printf '%s\n' 'forbidden old'
printf '%s\n' 'lower new'
# CHECK-DAG: upper [[token:[a-z]+]]
# CHECK-NOT: forbidden [[token]]
# CHECK-DAG: lower [[token:[a-z]+]]
