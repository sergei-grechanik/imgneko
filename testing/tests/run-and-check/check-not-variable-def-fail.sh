#!/usr/bin/env run-and-check
# XFAIL
# RUN: sh %s

# Verify that CHECK-NOT may not define new variables.

printf '%s\n' 'prefix 123 suffix'
# CHECK: prefix
# CHECK-NOT: [[value:[0-9]+]]
# CHECK-SAME: suffix
