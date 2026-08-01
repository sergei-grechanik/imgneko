#!/usr/bin/env run-and-check
# SPDX-License-Identifier: MIT-0
# XFAIL
# RUN: sh %s

# Verify that CHECK-DAG reports undefined variables while matching a grouped
# directive, rather than treating the group as a plain match failure.

printf '%s\n' 'anything'
# CHECK-DAG: [[missing]]
