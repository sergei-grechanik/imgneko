#!/bin/sh

# Spawn a detached descendant that inherits the output pipe and keeps it open
# after the direct test process times out. The runner must not block forever
# draining output from that still-open pipe.

set -eu

sleep_seconds=${IMGNEKO_TEST_TIMEOUT_DETACHED_OUTPUT_SLEEP_SECONDS:-0}

printf '%s\n' 'timeout detached output script started'
setsid sh -c 'sleep "$1"' sh "$sleep_seconds" &
sleep 2
