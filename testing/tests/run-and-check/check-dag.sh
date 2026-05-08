#!/usr/bin/env run-and-check
# SPDX-License-Identifier: MIT-0
# RUN: sh %s

# Verify that CHECK-DAG directives in the same group can match output in a
# different order from the source directives.

printf '%s\n' '== unordered group =='
printf '%s\n' 'beta 17'
printf '%s\n' 'alpha 17'
printf '%s\n' 'after unordered group'
# CHECK: == unordered group ==
# CHECK-DAG: alpha [[id:[0-9]+]]
# CHECK-DAG: beta [[id]]
# CHECK: after unordered group

# Verify that variables captured by CHECK-DAG are visible to later DAG
# directives and to the first CHECK after the DAG group.

printf '%s\n' '== capture propagation =='
printf '%s\n' 'summary color=green id=42'
printf '%s\n' 'detail id=42 color=green'
printf '%s\n' 'after captured id=42 color=green'
# CHECK: == capture propagation ==
# CHECK-DAG: detail id=[[capture_id:[0-9]+]] color=[[color:[a-z]+]]
# CHECK-DAG: summary color=[[color]] id=[[capture_id]]
# CHECK: after captured id=[[capture_id]] color=[[color]]

# Verify that CHECK-DAG captures follow directive order, not output order, when
# a later directive shadows an earlier variable with the same name.

printf '%s\n' '== capture shadow =='
printf '%s\n' 'second value=200'
printf '%s\n' 'first value=100'
printf '%s\n' 'after shadow value=200'
# CHECK: == capture shadow ==
# CHECK-DAG: first value=[[dag_value:[0-9]+]]
# CHECK-DAG: second value=[[dag_value:[0-9]+]]
# CHECK: after shadow value=[[dag_value]]

# Verify that CHECK-NOT can use variables captured by the CHECK-DAG group above
# it when checking the region before the next positive match.

printf '%s\n' '== not with capture =='
printf '%s\n' 'token abc'
printf '%s\n' 'safe middle'
printf '%s\n' 'after token abc'
# CHECK: == not with capture ==
# CHECK-DAG: token [[not_token:[a-z]+]]
# CHECK-NOT: forbidden [[not_token]]
# CHECK: after token [[not_token]]

# Verify that CHECK-NOT uses variables visible when it appears, not values
# captured by the following CHECK-DAG group.

printf '%s\n' '== not snapshot before dag =='
printf '%s\n' 'seed old'
printf '%s\n' 'forbidden new'
printf '%s\n' 'dag new'
printf '%s\n' 'after dag snapshot new'
# CHECK: == not snapshot before dag ==
# CHECK-DAG: seed [[dag_snapshot:[a-z]+]]
# CHECK-NOT: forbidden [[dag_snapshot]]
# CHECK-DAG: dag [[dag_snapshot:[a-z]+]]
# CHECK: after dag snapshot [[dag_snapshot]]

# Verify that CHECK-NOT before a CHECK-DAG group ends at the earliest output
# match in the group, even when later DAG directives match earlier bytes on the
# same line.

printf '%s\n' '== same-line not boundary =='
printf '%s\n' 'clean middle'
printf '%s\n' 'first forbidden second'
printf '%s\n' 'forbidden'
printf '%s\n' 'after same-line not boundary'
# CHECK: == same-line not boundary ==
# CHECK-NOT: forbidden
# CHECK-DAG: second
# CHECK-DAG: first
# CHECK: after same-line not boundary

# Verify that CHECK-NEXT after a CHECK-DAG group uses the latest match in
# output order, not the match of the last CHECK-DAG directive in source order.

printf '%s\n' '== next after group =='
printf '%s\n' 'early member'
printf '%s\n' 'late member'
printf '%s\n' 'next after late member'
# CHECK: == next after group ==
# CHECK-DAG: late member
# CHECK-DAG: early member
# CHECK-NEXT: next after late member

# Verify that CHECK-SAME after a CHECK-DAG group uses the latest match in
# output order, not the match of the last CHECK-DAG directive in source order.

printf '%s\n' '== same after group =='
printf '%s\n' 'early same member'
printf '%s\n' 'late same member same suffix'
# CHECK: == same after group ==
# CHECK-DAG: late same member
# CHECK-DAG: early same member
# CHECK-SAME: same suffix

# Verify that CHECK-NOT splits CHECK-DAG groups. The directives below the
# CHECK-NOT can still be reordered with each other, but not with the directives
# above the CHECK-NOT.

printf '%s\n' '== not barrier =='
printf '%s\n' 'forbidden'
printf '%s\n' 'upper c'
printf '%s\n' 'forbidden'
printf '%s\n' 'upper a'
printf '%s\n' 'forbidden'
printf '%s\n' 'upper b'
printf '%s\n' 'clean middle'
printf '%s\n' 'lower b'
printf '%s\n' 'forbidden'
printf '%s\n' 'lower a'
printf '%s\n' 'forbidden'
printf '%s\n' 'lower c'
printf '%s\n' 'forbidden'
printf '%s\n' 'after not barrier'
# CHECK: == not barrier ==
# CHECK-DAG: upper b
# CHECK-DAG: upper c
# CHECK-DAG: upper a
# CHECK-NOT: forbidden
# CHECK-DAG: lower a
# CHECK-DAG: lower c
# CHECK-DAG: lower b
# CHECK: after not barrier

# Verify that CHECK-DAG matches may share a line but cannot overlap.

printf '%s\n' '== non-overlap =='
printf '%s\n' 'aaaa'
printf '%s\n' 'after non-overlap'
# CHECK: == non-overlap ==
# CHECK-DAG: aa
# CHECK-DAG: aa
# CHECK: after non-overlap

# Verify that an empty CHECK-DAG match does not consume bytes and can share a
# line with another CHECK-DAG match.

printf '%s\n' '== empty non-overlap =='
printf '%s\n' 'empty-marker'
printf '%s\n' 'after empty non-overlap'
# CHECK: == empty non-overlap ==
# CHECK-DAG: empty-marker
# CHECK-DAG: {{}}
# CHECK: after empty non-overlap

# Verify the same empty-match rule when the empty CHECK-DAG is matched before
# the non-empty CHECK-DAG on the same line.

printf '%s\n' '== empty first non-overlap =='
printf '%s\n' 'empty-first'
printf '%s\n' 'after empty first non-overlap'
# CHECK: == empty first non-overlap ==
# CHECK-DAG: {{}}
# CHECK-DAG: empty-first
# CHECK: after empty first non-overlap

# Verify that a later source CHECK-DAG may match earlier non-overlapping bytes
# on the same output line.

printf '%s\n' '== same-line reorder =='
printf '%s\n' 'aabb'
printf '%s\n' 'after same-line reorder'
# CHECK: == same-line reorder ==
# CHECK-DAG: bb
# CHECK-DAG: aa
# CHECK: after same-line reorder

# Verify a trailing CHECK-DAG group after CHECK-NOT can finish the file after
# checking the region before the earliest DAG match.

printf '%s\n' '== trailing not dag =='
printf '%s\n' 'safe tail'
printf '%s\n' 'tail b'
printf '%s\n' 'tail a'
# CHECK: == trailing not dag ==
# CHECK-NOT: forbidden
# CHECK-DAG: tail a
# CHECK-DAG: tail b
