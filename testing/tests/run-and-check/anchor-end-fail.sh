#!/usr/bin/env run-and-check
# SPDX-License-Identifier: MIT-0
# XFAIL
# RUN: sh %s

# Verify that $ does not match before extra trailing text on the same line.

echo "value suffix"
# CHECK: {{value$}}
