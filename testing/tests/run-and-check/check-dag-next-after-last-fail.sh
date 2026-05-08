#!/usr/bin/env run-and-check
# SPDX-License-Identifier: MIT-0
# XFAIL
# RUN: sh %s

# Verify that CHECK-NEXT after a CHECK-DAG group cannot match after the last
# CHECK-DAG directive when an earlier directive matched a later output line.

printf '%s\n' 'early member'
printf '%s\n' 'next after early member'
printf '%s\n' 'late member'
# CHECK-DAG: late member
# CHECK-DAG: early member
# CHECK-NEXT: next after early member
