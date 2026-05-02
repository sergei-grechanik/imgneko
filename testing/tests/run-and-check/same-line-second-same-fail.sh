#!/usr/bin/env run-and-check
# SPDX-License-Identifier: MIT-0
# XFAIL
# RUN: sh %s

# Verify that when multiple CHECK-SAME directives appear in a row, each one
# only searches after the end of the immediately previous match.

printf '%s\n' 'alpha beta gamma'
# CHECK: alpha
# CHECK-SAME: gamma
# CHECK-SAME: beta
