#!/usr/bin/env run-and-check
# SPDX-License-Identifier: MIT-0
# XFAIL
# RUN: sh %s

# Verify that a CHECK following CHECK-DAG directives starts after the latest
# CHECK-DAG match in output order, even when that match came from an earlier
# source directive.

printf '%s\n' 'start'
printf '%s\n' 'beta'
printf '%s\n' 'end'
printf '%s\n' 'alpha'
# CHECK: start
# CHECK-DAG: alpha
# CHECK-DAG: beta
# CHECK: end
