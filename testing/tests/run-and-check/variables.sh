#!/usr/bin/env run-and-check
# SPDX-License-Identifier: MIT-0
# RUN: sh %s

# Verify multiple captured variables at once and same-name variable shadowing
# across later successful directives.

echo "A=11 B=22 C=33 D=44 E=55 F=66"
echo "11 22 33 44 55 66"
# CHECK: A=[[a:[0-9]+]] B=[[b:[0-9]+]] C=[[c:[0-9]+]] D=[[d:[0-9]+]] E=[[e:[0-9]+]] F=[[f:[0-9]+]]
# CHECK-NEXT: [[a]] [[b]] [[c]] [[d]] [[e]] [[f]]

echo "shadow 123"
echo "a 123"
echo "shadow 999"
echo "b 999"
# CHECK: shadow [[value:[0-9]+]]
# CHECK-NEXT: a [[value]]
# CHECK: shadow [[value:[0-9]+]]
# CHECK-NEXT: b [[value]]
