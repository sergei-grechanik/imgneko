#!/usr/bin/env run-and-check
# XFAIL
# RUN: sh %s

# Verify that plain CHECK advances to a later line. Same-line continuation must
# use CHECK-SAME instead.

echo "before after"
# CHECK: before
# CHECK: after
