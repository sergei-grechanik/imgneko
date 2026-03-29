#!/usr/bin/env run-and-check
# RUN: sh %s

# Verify the main directive flow, regex fragments, captures, anchors, and that
# stderr is ignored unless the RUN command redirects it into stdout.

echo "Hello, world!"
echo "Next line"
echo "One more line"
echo "Goodbye, world!"
# CHECK:      Hello
# CHECK-SAME: world
# CHECK-NEXT: Next line
# CHECK-NOT:  Two
# CHECK:      Goodbye

echo "Two more lines 123"
echo "123 and 456"
# CHECK: Two {{.*}} lines [[var:[0-9]+]]
# CHECK-NEXT: [[var]] and 456

echo "Line with start and end"
# CHECK: {{^Line with .* end$}}

echo "Line with start and end"
# CHECK: {{^}}Line with {{.*}} end{{$}}

echo "stdout only"
echo "stderr should stay hidden" >&2
# CHECK: stdout only
# CHECK-NOT: stderr should stay hidden
