#!/bin/sh
# SPDX-License-Identifier: MIT-0

# Shared setup, timing, correctness checks, and result recording for reader
# benchmarks. Benchmark tests source this file after enabling `set -eu`.

BENCHMARK_READERS="$IMGNEKO_BUILD_DIR/bin/benchmark-readers"
BENCHMARK_BATCH_RUNNER="$IMGNEKO_ROOT_DIR/testing/scripts/run-benchmark-batch.sh"
BENCHMARK_RESULTS="$IMGNEKO_TEST_OUTPUT_DIR/benchmark-results.tsv"
LC_ALL=C
QPDF_ZOPFLI=disabled
export LC_ALL QPDF_ZOPFLI

# Each benchmark test sources this file once and owns its result file.
: > "$BENCHMARK_RESULTS"

benchmark_fail() {
    printf '%s\n' "error: $*" >&2
    exit 1
}

# Generate deterministic random or repetitive input of an exact size and
# report the selected benchmark data set before doing the work.
benchmark_generate() {
    arg_generate_data_set="$1"
    arg_generate_bytes="$2"
    arg_generate_output="$3"

    printf 'Generating %s input (%s bytes).\n' \
        "$BENCHMARK_DATA_SET" "$arg_generate_bytes"
    "$BENCHMARK_READERS" generate --data-set "$arg_generate_data_set" \
        --bytes "$arg_generate_bytes" > "$arg_generate_output"
}

# Run warmups, time batches of repeated operations, and retain total
# wall/user/system batch times for each sample. Print the per-operation wall
# mean and estimated standard error followed by per-operation mean user and
# system time as tab-separated seconds.
#
# input
#     File supplied to every warmup and measured command on standard input.
# label
#     Unique filename-safe label for progress output and the retained `.time`
#     file.
# command [argument...]
#     Remaining arguments forming the command to measure.
#
# IMGNEKO_TEST_OUTPUT_DIR
#     Directory where timing samples are retained.
# BENCHMARK_WARMUPS
#     Number of unmeasured command invocations before collecting samples.
# BENCHMARK_BATCH_SIZE
#     Number of repeated command invocations in each measured sample.
# BENCHMARK_ROUNDS
#     Number of measured batches.
#
# Example:
#     benchmark_measure \
#         "/tmp/imgneko-benchmark/small.raw" \
#         "small-base64-encode-reference" \
#         base64 --wrap=0
benchmark_measure() {
    arg_input="$1"
    arg_label="$2"
    shift 2
    local_times="$IMGNEKO_TEST_OUTPUT_DIR/$arg_label.time"
    local_sample="$local_times.sample"
    local_warmup=0

    printf 'Measuring %s (warmups: %s, samples: %s, batch size: %s).\n' \
        "$arg_label" "$BENCHMARK_WARMUPS" "$BENCHMARK_ROUNDS" \
        "$BENCHMARK_BATCH_SIZE" >&2
    printf 'Warmup, %s iterations:' "$BENCHMARK_WARMUPS" >&2
    printf ' %s' "$@" >&2
    printf ' < %s > /dev/null\n' "$arg_input" >&2
    while [ "$local_warmup" -lt "$BENCHMARK_WARMUPS" ]; do
        "$@" < "$arg_input" > /dev/null ||
            benchmark_fail "warmup command failed: $arg_label"
        local_warmup=$((local_warmup + 1))
    done
    printf 'sample\tbatch-size\twall-total\tuser-total\tsystem-total\n' \
        > "$local_times"
    local_round=1
    while [ "$local_round" -le "$BENCHMARK_ROUNDS" ]; do
        printf 'Running %s iterations:' "$BENCHMARK_BATCH_SIZE" >&2
        printf ' %s' "$@" >&2
        printf ' < %s > /dev/null\n' "$arg_input" >&2
        if time -p "$BENCHMARK_BATCH_RUNNER" \
            "$arg_input" "$BENCHMARK_BATCH_SIZE" "$@" \
            2> "$local_sample"; then
            awk -v sample="$local_round" \
                -v batch_size="$BENCHMARK_BATCH_SIZE" '
                $1 == "real" { wall = $2; wall_count++ }
                $1 == "user" { user_time = $2; user_count++ }
                $1 == "sys" { system_time = $2; system_count++ }
                END {
                    if (wall_count != 1 || user_count != 1 || system_count != 1)
                        exit 1
                    printf "%d\t%d\t%.9f\t%.9f\t%.9f\n", \
                           sample, batch_size, wall, user_time, system_time
                }
            ' "$local_sample" >> "$local_times" ||
                benchmark_fail "could not parse timing sample: $local_sample"
        else
            cat "$local_sample" >&2
            benchmark_fail "timed command failed: $arg_label"
        fi
        local_round=$((local_round + 1))
    done
    rm -f "$local_sample"

    awk -F '\t' -v expected="$BENCHMARK_ROUNDS" \
        -v expected_batch_size="$BENCHMARK_BATCH_SIZE" 'NR > 1 {
        if (NF != 5 || $2 != expected_batch_size) exit 1
        wall = $3 / $2; user_time = $4 / $2; system_time = $5 / $2
        wall_sum += wall; wall_squares += wall * wall
        user_sum += user_time; system_sum += system_time; count++
    } END {
        if (count != expected) exit 1
        wall_mean = wall_sum / count; wall_error = 0
        if (count > 1) {
            variance = (wall_squares - wall_sum * wall_sum / count) / \
                       (count - 1)
            if (variance < 0) variance = 0
            wall_error = sqrt(variance / count)
        }
        printf "%.9f\t%.9f\t%.9f\t%.9f\n", \
               wall_mean, wall_error, user_sum / count, system_sum / count
    }' "$local_times" ||
        benchmark_fail "could not parse timing data: $local_times"
}

# Compare a command's output byte-for-byte with the expected file.
benchmark_check_equal() {
    cmp -s "$1" "$2"
}

# Verify that a zlib stream expands byte-for-byte to the expected file.
benchmark_check_zlib() {
    zlib-flate -uncompress < "$1" | cmp -s "$2" -
}

# Benchmark an operation's reference and buffered imgneko implementations.
#
# input
#     File supplied to each command on standard input.
# reference_output
#     File receiving the reference implementation's output.
# expected
#     File passed as the expected value to the correctness-check function.
# operation
#     benchmark-readers command and report label.
# check
#     Function accepting actual and expected output paths.
# reference_command [reference_argument...]
#     Remaining arguments forming the reference implementation command.
#
# BENCHMARK_BUFFER_SIZES
#     Space-separated imgneko input/output buffer sizes.
# BENCHMARK_BYTES
#     Unencoded input byte count written to each result row.
# BENCHMARK_DATA_SET
#     Data-set name written to each result row.
# BENCHMARK_READERS
#     Path to the imgneko workload helper.
# BENCHMARK_RESULTS
#     Path to the result TSV receiving reference and imgneko rows.
# BENCHMARK_ROUNDS
#     Number of measured batches forwarded to benchmark_measure.
# BENCHMARK_WARMUPS
#     Number of warmup runs forwarded to benchmark_measure.
# BENCHMARK_BATCH_SIZE
#     Number of repeated operations in each measured batch.
# IMGNEKO_TEST_OUTPUT_DIR
#     Directory where benchmark_measure retains timing samples.
#
# Example:
#     benchmark_operation \
#         "/tmp/imgneko-benchmark/small.reference.base64" \
#         "/tmp/imgneko-benchmark/small.checked" \
#         "/tmp/imgneko-benchmark/small.raw" \
#         base64-decode \
#         benchmark_check_equal \
#         base64 --decode
benchmark_operation() {
    arg_input="$1"
    arg_reference_output="$2"
    arg_expected="$3"
    arg_operation="$4"
    arg_check="$5"
    shift 5
    local_imgneko_output="$arg_reference_output.imgneko"

    # Run the reference implementation.
    printf 'Checking reference %s output.\n' "$arg_operation"
    "$@" < "$arg_input" > "$arg_reference_output"
    "$arg_check" "$arg_reference_output" \
        "$arg_expected" ||
        benchmark_fail \
            "reference $arg_operation produced unexpected output"
    local_measurement="$(benchmark_measure "$arg_input" \
        "$arg_operation-reference" "$@")"
    printf '%s\t%s\t%s\treference\t-\t%s\t%s\t%s\t%s\n' \
        "$BENCHMARK_DATA_SET" "$BENCHMARK_BYTES" "$arg_operation" \
        "$local_measurement" "$BENCHMARK_WARMUPS" "$BENCHMARK_ROUNDS" \
        "$BENCHMARK_BATCH_SIZE" \
        >> "$BENCHMARK_RESULTS"

    # Run the imgneko implementation for each preset buffer size.
    for local_bufsize in $BENCHMARK_BUFFER_SIZES; do
        # Run the imgneko implementation once to verify correctness.
        printf 'Checking imgneko %s output (%s-byte buffers).\n' \
            "$arg_operation" "$local_bufsize"
        "$BENCHMARK_READERS" "$arg_operation" \
            --input-buffer-size "$local_bufsize" \
            --output-buffer-size "$local_bufsize" \
            < "$arg_input" > "$local_imgneko_output"
        "$arg_check" "$local_imgneko_output" \
            "$arg_expected" ||
            benchmark_fail \
                "imgneko $arg_operation produced unexpected output"

        # Run the imgneko implementation multiple times to measure performance.
        local_measurement="$(benchmark_measure "$arg_input" \
            "$arg_operation-imgneko-$local_bufsize" \
            "$BENCHMARK_READERS" "$arg_operation" \
            --input-buffer-size "$local_bufsize" \
            --output-buffer-size "$local_bufsize")"
        printf '%s\t%s\t%s\timgneko\t%s\t%s\t%s\t%s\t%s\n' \
            "$BENCHMARK_DATA_SET" "$BENCHMARK_BYTES" "$arg_operation" \
            "$local_bufsize" "$local_measurement" \
            "$BENCHMARK_WARMUPS" "$BENCHMARK_ROUNDS" \
            "$BENCHMARK_BATCH_SIZE" >> "$BENCHMARK_RESULTS"
    done
}

# Benchmark zlib compression and decompression for the requested data set.
#
# data_set
#     Generator and report data-set name: `random` or `repetitive`.
# compress_batch_size
#     Number of repeated operations in each compression timing sample.
# decompress_batch_size
#     Number of repeated operations in each decompression timing sample.
# BENCHMARK_BYTES
#     Number of uncompressed input bytes to generate.
# IMGNEKO_TEST_OUTPUT_DIR
#     Directory used for temporary input and transform output files.
#
# This function sets BENCHMARK_DATA_SET to data_set for the shared generation
# and operation helpers.
benchmark_zlib_data_set() {
    arg_data_set="$1"
    arg_compress_batch_size="$2"
    arg_decompress_batch_size="$3"
    local_raw="$IMGNEKO_TEST_OUTPUT_DIR/input.raw"
    local_zlib="$IMGNEKO_TEST_OUTPUT_DIR/reference.zlib"
    local_checked="$IMGNEKO_TEST_OUTPUT_DIR/checked.raw"

    BENCHMARK_DATA_SET="$arg_data_set"

    # Always remove generated payloads and intermediate transform outputs. The
    # retained run should contain only measurements, results, and runner logs.
    benchmark_cleanup_zlib_data() {
        rm -f "$local_raw" "$local_zlib" "$local_checked" \
            "${local_zlib}.imgneko" "${local_checked}.imgneko"
    }
    trap benchmark_cleanup_zlib_data 0
    trap 'exit 1' HUP INT TERM

    benchmark_generate "$arg_data_set" "$BENCHMARK_BYTES" "$local_raw"

    # Select each operation's preset-specific batch size immediately before
    # measuring it.
    BENCHMARK_BATCH_SIZE="$arg_compress_batch_size"
    benchmark_operation "$local_raw" "$local_zlib" "$local_raw" \
        zlib-compress benchmark_check_zlib zlib-flate -compress
    BENCHMARK_BATCH_SIZE="$arg_decompress_batch_size"
    benchmark_operation "$local_zlib" "$local_checked" "$local_raw" \
        zlib-decompress benchmark_check_equal zlib-flate -uncompress
    benchmark_cleanup_zlib_data
}
