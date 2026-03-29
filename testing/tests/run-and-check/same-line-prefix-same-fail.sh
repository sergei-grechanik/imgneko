#!/usr/bin/env run-and-check
# XFAIL
# RUN: sh %s

# Verify that CHECK-SAME only searches after the previous match end, not in
# the already matched prefix of the same line.

printf '%s\n' 'alpha beta'
# CHECK: beta
# CHECK-SAME: alpha
