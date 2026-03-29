#!/usr/bin/env run-and-check
# RUN: sh %s

# Verify that CHECK-NOT may reuse multiple variables captured by earlier
# positive directives, including literal text between the reused values.

printf '%s\n' 'id 123 tag abc'
printf '%s\n' 'done'
printf '%s\n' 'id 123 middle abc'
# CHECK: id [[number:[0-9]+]] tag [[word:[a-z]+]]
# CHECK-NOT: id [[number]] middle [[word]]
# CHECK: done
