#!/usr/bin/env run-and-check
# XFAIL
# RUN: sh %s

# Document the current behavior: variables become visible only after a complete
# directive match, so same-directive reuse is rejected as undefined.

echo "123 123"
# CHECK: [[value:[0-9]+]] [[value]]
