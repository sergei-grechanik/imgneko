#!/usr/bin/env run-and-check
# SPDX-License-Identifier: MIT-0
# XFAIL
# RUN: sh %s

# Verify that CHECK-NOT inspects the same-line gap between positive matches,
# even when those matches are connected with CHECK-SAME.
echo "before forbidden after"
# CHECK: before
# CHECK-NOT: forbidden
# CHECK-SAME: after
