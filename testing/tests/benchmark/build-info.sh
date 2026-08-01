#!/bin/sh
# SPDX-License-Identifier: MIT-0

# Capture the complete benchmark helper version output in a dedicated test so
# every retained benchmark run records the build and linked zlib details.
set -eu

benchmark_readers="$IMGNEKO_BUILD_DIR/bin/benchmark-readers"
version_file="$IMGNEKO_TEST_OUTPUT_DIR/benchmark-version.txt"

"$benchmark_readers" --version > "$version_file"
cat "$version_file"
