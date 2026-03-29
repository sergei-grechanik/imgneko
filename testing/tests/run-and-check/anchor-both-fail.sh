#!/usr/bin/env run-and-check
# XFAIL
# RUN: sh %s

# Verify that ^ and $ together require the whole line to match.

echo "prefix value suffix"
# CHECK: {{^value$}}
