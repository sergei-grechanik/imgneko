#!/usr/bin/env run-and-check
# SPDX-License-Identifier: MIT-0
# XFAIL
# RUN: sh %s

# Verify that unresolved variable references fail with a direct diagnostic.

echo "value"
# CHECK: [[missing]]
