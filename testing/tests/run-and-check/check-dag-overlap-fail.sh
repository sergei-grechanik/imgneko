#!/usr/bin/env run-and-check
# SPDX-License-Identifier: MIT-0
# XFAIL
# RUN: sh %s

# Verify that separate CHECK-DAG directives cannot satisfy their patterns with
# overlapping bytes from the same output line.

printf '%s\n' 'aaa'
# CHECK-DAG: aa
# CHECK-DAG: aa
