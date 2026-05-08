#!/usr/bin/env run-and-check
# SPDX-License-Identifier: MIT-0
# XFAIL
# RUN: sh %s

# Verify that CHECK-DAG directives cannot be reordered across a CHECK-NOT
# directive, even when the forbidden pattern is absent.

printf '%s\n' 'after'
printf '%s\n' 'before'
# CHECK-DAG: before
# CHECK-NOT: forbidden
# CHECK-DAG: after
