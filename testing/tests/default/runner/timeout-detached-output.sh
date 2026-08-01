#!/bin/sh
# SPDX-License-Identifier: MIT-0

# Spawn a detached descendant that inherits the output pipe and keeps it open
# after the direct test process times out. The runner must not block forever
# draining output from that still-open pipe.

set -eu

sleep_seconds=${IMGNEKO_TEST_TIMEOUT_DETACHED_OUTPUT_SLEEP_SECONDS:-0}

printf '%s\n' 'timeout detached output script started'
# GNU/Linux usually provides a setsid command, but macOS does not. When the
# command is missing, use Perl only as a small wrapper around setsid(2) so this
# fixture still creates a detached descendant without a custom helper binary.
if command -v setsid >/dev/null 2>&1; then
    setsid sh -c 'sleep "$1"' sh "$sleep_seconds" &
else
    perl -MPOSIX=setsid -e 'setsid() or die "setsid: $!"; sleep $ARGV[0]' \
        "$sleep_seconds" &
fi
sleep 2
