#!/usr/bin/env run-and-check
# SPDX-License-Identifier: MIT-0
# XFAIL
# RUN: sh %s

# Verify that CHECK-SAME is rejected when no earlier positive directive has
# established the current line and column.

printf '%s\n' 'alpha'
# CHECK-SAME: alpha
