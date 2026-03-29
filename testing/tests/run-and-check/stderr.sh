#!/usr/bin/env run-and-check
# RUN: sh %s 2>&1

# Verify that stderr participates in matching if the shell redirects it into
# stdout.

echo "stdout line"
echo "stderr line" >&2
# CHECK: stdout line
# CHECK-NEXT: stderr line
