#!/bin/sh
# SPDX-License-Identifier: MIT-0

# Run the requested command repeatedly against the same input file so a coarse
# external timer measures a batch instead of a single short-lived operation.
set -eu

if [ "$#" -lt 3 ]; then
    printf '%s\n' "usage: $0 INPUT ITERATIONS COMMAND [ARGUMENT ...]" >&2
    exit 2
fi

input=$1
iterations=$2
shift 2

iteration=0
while [ "$iteration" -lt "$iterations" ]; do
    "$@" < "$input" > /dev/null
    iteration=$((iteration + 1))
done
