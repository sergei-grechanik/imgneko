#!/usr/bin/env run-and-check
# SPDX-License-Identifier: MIT-0
# RUN: sh %s

# Verify that CHECK-NOT may reuse multiple variables captured by earlier
# positive directives, including literal text between the reused values.

printf '%s\n' 'id 123 tag abc'
printf '%s\n' 'done'
printf '%s\n' 'id 123 middle abc'
# CHECK: id [[number:[0-9]+]] tag [[word:[a-z]+]]
# CHECK-NOT: id [[number]] middle [[word]]
# CHECK: done

# Verify that CHECK-NOT uses the variable values visible when the CHECK-NOT
# appears, not values captured by the later positive CHECK that closes the
# negative region.

printf '%s\n' 'seed old'
printf '%s\n' 'forbidden new'
printf '%s\n' 'next new'
# CHECK: seed [[snapshot:[a-z]+]]
# CHECK-NOT: forbidden [[snapshot]]
# CHECK: next [[snapshot:[a-z]+]]
