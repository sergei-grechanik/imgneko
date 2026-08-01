#!/bin/sh
# SPDX-License-Identifier: MIT-0

# Close both output streams before sleeping so the runner must enforce the
# timeout even after the child output pipe reaches EOF. By default this exits
# immediately so regular full-suite runs do not spend time in the long-sleep
# path; expectations.sh opts into that path explicitly.

set -eu

sleep_seconds=${IMGNEKO_TEST_TIMEOUT_CLOSED_FDS_SLEEP_SECONDS:-0}

exec 1>&- 2>&-
sleep "$sleep_seconds"
