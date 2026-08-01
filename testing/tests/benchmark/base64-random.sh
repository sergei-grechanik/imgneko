#!/bin/sh
# SPDX-License-Identifier: MIT-0

# Compare imgneko base64 readers with the platform base64 implementation on
# deterministic random input.
set -eu
. "$IMGNEKO_ROOT_DIR/testing/support/benchmark-common.sh"

# Each preset defines its input size, timing counts, batch size, and matched
# input/output buffer sizes.
BENCHMARK_PRESET="${BENCHMARK_PRESET:-large}"

case "$BENCHMARK_PRESET" in
    small)
        BENCHMARK_BYTES="${BENCHMARK_BYTES:-16777216}"
        BENCHMARK_ROUNDS="${BENCHMARK_ROUNDS:-3}"
        BENCHMARK_WARMUPS="${BENCHMARK_WARMUPS:-1}"
        BENCHMARK_BATCH_SIZE="${BENCHMARK_BATCH_SIZE:-10}"
        BENCHMARK_BUFFER_SIZES="${BENCHMARK_BUFFER_SIZES:-4096}"
        ;;
    large)
        BENCHMARK_BYTES="${BENCHMARK_BYTES:-33554432}"
        BENCHMARK_ROUNDS="${BENCHMARK_ROUNDS:-5}"
        BENCHMARK_WARMUPS="${BENCHMARK_WARMUPS:-3}"
        BENCHMARK_BATCH_SIZE="${BENCHMARK_BATCH_SIZE:-10}"
        BENCHMARK_BUFFER_SIZES="${BENCHMARK_BUFFER_SIZES:-512 4096 16384}"
        ;;
    *)
        benchmark_fail "BENCHMARK_PRESET must be small or large"
        ;;
esac

# Find portable no-wrap and decode invocations. This function creates two
# functions: benchmark_base64_encode_operation and
# benchmark_base64_decode_operation, which wrap the platform base64 command.
probe_base64() {
    if [ "$(printf Man | base64 --wrap=0 2>/dev/null)" = TWFu ]; then
        benchmark_base64_encode_operation() {
            benchmark_operation "$@" base64 --wrap=0
        }
    elif [ "$(printf Man | base64 -w 0 2>/dev/null)" = TWFu ]; then
        benchmark_base64_encode_operation() {
            benchmark_operation "$@" base64 -w 0
        }
    elif [ "$(printf Man | base64 -b 0 2>/dev/null)" = TWFu ]; then
        benchmark_base64_encode_operation() {
            benchmark_operation "$@" base64 -b 0
        }
    else
        benchmark_fail "could not find an unwrapped base64 encoder"
    fi

    if [ "$(printf TWFu | base64 --decode 2>/dev/null)" = Man ]; then
        benchmark_base64_decode_operation() {
            benchmark_operation "$@" base64 --decode
        }
    elif [ "$(printf TWFu | base64 -d 2>/dev/null)" = Man ]; then
        benchmark_base64_decode_operation() {
            benchmark_operation "$@" base64 -d
        }
    elif [ "$(printf TWFu | base64 -D 2>/dev/null)" = Man ]; then
        benchmark_base64_decode_operation() {
            benchmark_operation "$@" base64 -D
        }
    else
        benchmark_fail "could not find a base64 decoder"
    fi
}

probe_base64

BENCHMARK_DATA_SET=random
raw="$IMGNEKO_TEST_OUTPUT_DIR/input.raw"
encoded="$IMGNEKO_TEST_OUTPUT_DIR/reference.base64"
checked="$IMGNEKO_TEST_OUTPUT_DIR/checked.raw"

# Always remove generated payloads and intermediate transform outputs. The
# retained run should contain only measurements, results, and runner logs.
cleanup_base64_data() {
    rm -f "$raw" "$encoded" "$checked" \
        "${encoded}.imgneko" "${checked}.imgneko"
}
trap cleanup_base64_data 0
trap 'exit 1' HUP INT TERM

benchmark_generate random "$BENCHMARK_BYTES" "$raw"
benchmark_base64_encode_operation "$raw" "$encoded" "$encoded" \
    base64-encode benchmark_check_equal
benchmark_base64_decode_operation "$encoded" "$checked" "$raw" \
    base64-decode benchmark_check_equal
cleanup_base64_data
