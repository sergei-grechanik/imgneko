#!/bin/sh
# SPDX-License-Identifier: MIT-0

# Compare imgneko zlib readers with zlib-flate for deterministic
# repetitive data, including both compression and decompression.

set -eu

. "$IMGNEKO_ROOT_DIR/testing/support/benchmark-common.sh"

# Each preset defines its input size, timing counts, operation batch sizes, and
# matched input/output buffer sizes.
BENCHMARK_PRESET="${BENCHMARK_PRESET:-large}"

case "$BENCHMARK_PRESET" in
    small)
        BENCHMARK_BYTES="${BENCHMARK_BYTES:-16777216}"
        BENCHMARK_ROUNDS="${BENCHMARK_ROUNDS:-3}"
        BENCHMARK_WARMUPS="${BENCHMARK_WARMUPS:-1}"
        BENCHMARK_COMPRESS_BATCH_SIZE="${BENCHMARK_COMPRESS_BATCH_SIZE:-10}"
        BENCHMARK_DECOMPRESS_BATCH_SIZE="${BENCHMARK_DECOMPRESS_BATCH_SIZE:-10}"
        BENCHMARK_BUFFER_SIZES="${BENCHMARK_BUFFER_SIZES:-4096}"
        ;;
    large)
        BENCHMARK_BYTES="${BENCHMARK_BYTES:-33554432}"
        BENCHMARK_ROUNDS="${BENCHMARK_ROUNDS:-5}"
        BENCHMARK_WARMUPS="${BENCHMARK_WARMUPS:-3}"
        BENCHMARK_COMPRESS_BATCH_SIZE="${BENCHMARK_COMPRESS_BATCH_SIZE:-10}"
        BENCHMARK_DECOMPRESS_BATCH_SIZE="${BENCHMARK_DECOMPRESS_BATCH_SIZE:-10}"
        BENCHMARK_BUFFER_SIZES="${BENCHMARK_BUFFER_SIZES:-512 4096 16384}"
        ;;
    *)
        benchmark_fail "BENCHMARK_PRESET must be small or large"
        ;;
esac

benchmark_zlib_data_set repetitive "$BENCHMARK_COMPRESS_BATCH_SIZE" \
    "$BENCHMARK_DECOMPRESS_BATCH_SIZE"
