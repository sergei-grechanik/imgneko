#!/usr/bin/env run-and-check
# XFAIL
# RUN: sh %s

# Verify that CHECK-NOT between two CHECK directives inspects the tail of the
# previous matching line before the later CHECK match.

printf 'a b\nc\n'
# CHECK: a
# CHECK-NOT: b
# CHECK: c
