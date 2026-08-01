#!/usr/bin/env run-and-check
# SPDX-License-Identifier: MIT-0
# RUN: sh %s

# Verify more complex regex fragments, including alternation, bounded repeats,
# escaped punctuation, and later reuse of a captured value.

echo "kind=(foo|bar) item[7] code=AB-12"
echo "AB-12 matched"
# CHECK: kind={{\(foo\|bar\)}} item{{\[[0-9]+\]}} code=[[code:[A-Z]{2}-[0-9]{2}]]
# CHECK-NEXT: [[code]] matched

echo "list: alpha,beta,gamma"
# CHECK: list: {{([a-z]+,){2}[a-z]+}}
