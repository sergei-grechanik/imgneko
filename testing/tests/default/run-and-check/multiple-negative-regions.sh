#!/usr/bin/env run-and-check
# SPDX-License-Identifier: MIT-0
# RUN: sh %s

# Verify that separate groups of CHECK-NOT directives apply to distinct output
# regions between successive positive CHECK matches.

printf '%s\n' 'middle one'
printf '%s\n' 'middle two'
printf '%s\n' 'alpha'
printf '%s\n' 'middle one'
printf '%s\n' 'beta'
printf '%s\n' 'middle two'
printf '%s\n' 'gamma'
printf '%s\n' 'middle one'
printf '%s\n' 'middle two'
# CHECK: alpha
# CHECK-NOT: middle two
# CHECK: beta
# CHECK-NOT: middle one
# CHECK: gamma
