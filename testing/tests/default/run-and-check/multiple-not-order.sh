#!/usr/bin/env run-and-check
# SPDX-License-Identifier: MIT-0
# RUN: sh %s

# Verify that multiple CHECK-NOT directives apply to the same region without
# any ordering between them, and that matching stops once the next CHECK is
# reached even if the forbidden strings appear later.

printf '%s\n' 'a'
printf '%s\n' 'middle line'
printf '%s\n' 'c'
printf '%s\n' 'second forbidden'
printf '%s\n' 'first forbidden'
# CHECK: a
# CHECK-NOT: second forbidden
# CHECK-NOT: first forbidden
# CHECK: c
