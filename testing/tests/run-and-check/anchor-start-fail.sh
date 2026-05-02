#!/usr/bin/env run-and-check
# SPDX-License-Identifier: MIT-0
# XFAIL
# RUN: sh %s

# Verify that ^ does not match after extra leading text on the same line.

echo "prefix value"
# CHECK: {{^value}}
