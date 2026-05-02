#!/usr/bin/env run-and-check
# SPDX-License-Identifier: MIT-0
# XFAIL
# RUN: sh %s

# Verify that ^ and $ together require the whole line to match.

echo "prefix value suffix"
# CHECK: {{^value$}}
