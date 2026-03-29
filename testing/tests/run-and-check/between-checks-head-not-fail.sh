#!/usr/bin/env run-and-check
# XFAIL
# RUN: sh %s

# Verify that CHECK-NOT between two CHECK directives inspects the prefix of the
# next matching line before the later CHECK match.

printf 'a\nb c\n'
# CHECK: a
# CHECK-NOT: b
# CHECK: c
