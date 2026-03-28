#!/bin/sh
# XFAIL

set -eu

if [ "${IMGNEKO_TEST_FORCE_SUCCESS:-0}" = "1" ]; then
    exit 0
fi

printf '%s\n' 'intentional expected failure' >&2
exit 1
