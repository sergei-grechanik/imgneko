#!/usr/bin/env run-and-check
# SPDX-License-Identifier: MIT-0
# XFAIL
# RUN: sh %s

# Verify that CHECK-SAME after a CHECK-DAG group cannot match on the line of the
# last CHECK-DAG directive when an earlier directive matched a later output line.

printf '%s\n' 'early member same'
printf '%s\n' 'same late member'
printf '%s\n' 'same'
# CHECK-DAG: late member
# CHECK-DAG: early member
# CHECK-SAME: same
