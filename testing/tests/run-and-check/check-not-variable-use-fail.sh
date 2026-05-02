#!/usr/bin/env run-and-check
# SPDX-License-Identifier: MIT-0
# XFAIL
# RUN: sh %s

# Verify that CHECK-NOT with reused variables is triggered when the forbidden
# text appears before the next positive CHECK match.

printf '%s\n' 'id 123 tag abc'
printf '%s\n' 'id 123 middle abc'
printf '%s\n' 'done'
# CHECK: id [[number:[0-9]+]] tag [[word:[a-z]+]]
# CHECK-NOT: id [[number]] middle [[word]]
# CHECK: done
