#!/usr/bin/env run-and-check
# SPDX-License-Identifier: MIT-0
# XFAIL
# RUN: sh %s

# Verify that `^` in CHECK-SAME anchors at the previous positive match end. The
# expected behavior is that the directive below does not skip the space between
# `alpha` and `beta`.

echo "alpha beta"
# CHECK: alpha
# CHECK-SAME: {{^}}beta
