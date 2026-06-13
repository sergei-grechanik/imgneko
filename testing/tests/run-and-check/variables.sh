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

# Verify variable-use expressions: top-level string and integer literals,
# function calls with literals, and function calls with captured variables.
echo "literal a:b 42"
echo "color 1193046"
echo "rgb 18;52;86"
echo "string rgb 0;4;210"
echo "underscore 77"
echo "underscore again 77"
printf 'placeholder bare \364\216\273\256\n'
printf 'placeholder row \364\216\273\256\314\205\n'
printf 'escapes A " \\ \011\n'
echo 'quote-colon x":y'
# CHECK: literal [["a:b"]] [[42]]
# CHECK-NEXT: color [[rgb_num:[0-9]+]]
# CHECK-NEXT: rgb [[rgb(rgb_num)]]
# CHECK-NEXT: string rgb [[rgb("1234")]]
# CHECK-NEXT: underscore [[_expr:[0-9]+]]
# CHECK-NEXT: underscore again [[_expr]]
# CHECK-NEXT: placeholder bare [[ph()]]
# CHECK-NEXT: placeholder row [[ph(0)]]
# CHECK-NEXT: escapes [["\x41"]] [["\""]] [["\\"]] [["\t"]]
# CHECK-NEXT: quote-colon [["x\":y"]]
