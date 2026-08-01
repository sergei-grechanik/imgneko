#!/bin/sh
# SPDX-License-Identifier: MIT-0

# Collect per-test benchmark rows and render a compact comparison table. Each
# imgneko row is compared with the matching reference implementation row.
set -eu
if [ "$#" -ne 1 ]; then
    printf '%s\n' "usage: $0 BENCHMARK_OUTPUT_DIR" >&2
    exit 2
fi
output_dir="$1"
summary="$output_dir/summary.txt"
version_file="$(find "$output_dir" -type f -name benchmark-version.txt -print |
    LC_ALL=C sort | sed -n '1p')"
if [ -z "$version_file" ]; then
    printf '%s\n' "error: no benchmark version output found" >&2
    exit 1
fi

{
    printf '%s\n' 'Benchmark build:'
    awk -F ': ' '
        $1 == "version" || $1 == "compiled" || $1 == "profile" ||
        $1 == "cc" || $1 == "cflags" || $1 == "zlib" {
            print "  " $0
        }
    ' "$version_file"
    printf '\n'
} > "$summary"

if ! find "$output_dir" -type f -name benchmark-results.tsv -print |
    LC_ALL=C sort |
    while IFS= read -r result_file; do
        cat "$result_file"
    done |
    awk -F '\t' '
        function key(data, bytes, operation) {
            return data SUBSEP bytes SUBSEP operation
        }
        function human_bytes(bytes) {
            if (bytes % 1073741824 == 0) return sprintf("%g GiB", bytes / 1073741824)
            if (bytes % 1048576 == 0) return sprintf("%g MiB", bytes / 1048576)
            if (bytes % 1024 == 0) return sprintf("%g KiB", bytes / 1024)
            return bytes " B"
        }
        NF == 12 {
            rows++
            data[rows] = $1; bytes[rows] = $2; operation[rows] = $3
            implementation[rows] = $4; buffer[rows] = $5
            wall_seconds[rows] = $6; wall_error[rows] = $7
            user_seconds[rows] = $8; system_seconds[rows] = $9
            operation_warmups[$3] = $10; operation_samples[$3] = $11
            batch_size[rows] = $12
            if (!seen_operation[$3]++)
                operation_order[++operation_count] = $3
            if ($4 == "reference")
                reference_wall_seconds[key($1, $2, $3)] = $6
            next
        }
        NF != 0 { print "error: malformed benchmark result row" > "/dev/stderr"; invalid = 1 }
        END {
            if (invalid || rows == 0) {
                print "error: no benchmark result rows found" > "/dev/stderr"
                exit 1
            }
            separator = "+------------------------------+----------------+------------" \
                        "+-------+-----------+----------+-----------------------" \
                        "+----------------------+"
            for (op_index = 1; op_index <= operation_count; op_index++) {
                target_operation = operation_order[op_index]
                if (op_index > 1)
                    print ""
                printf "| operation: %s | warmups: %s | samples: %s |\n\n", \
                       target_operation, operation_warmups[target_operation], \
                       operation_samples[target_operation]
                print separator
                printf "| %-28s | %-14s | %10s | %5s | %9s | %8s | %-21s | %-20s |\n", \
                       "dataset / input", "implementation", \
                       "I/O buffer", "batch", "user (ms)", "sys (ms)", \
                       "wall mean +/- SE (ms)", "vs reference"
                print separator
                previous_dataset = ""
                for (row = 1; row <= rows; row++) {
                    if (operation[row] != target_operation)
                        continue
                    result_key = key(data[row], bytes[row], operation[row])
                    dataset_key = data[row] SUBSEP bytes[row]
                    if (previous_dataset != "" && dataset_key != previous_dataset)
                        print separator
                    reference = reference_wall_seconds[result_key]
                    if (implementation[row] == "reference") {
                        comparison = "-"
                    } else if (wall_seconds[row] == 0 || reference == 0) {
                        comparison = "timer resolution"
                    } else if (wall_seconds[row] < reference) {
                        comparison = sprintf("%.2fx faster", \
                                             reference / wall_seconds[row])
                    } else if (wall_seconds[row] > reference) {
                        comparison = sprintf("%.2fx slower", \
                                             wall_seconds[row] / reference)
                    } else {
                        comparison = "1.00x same"
                    }
                    shown_dataset = dataset_key == previous_dataset ? "" : \
                        sprintf("%s / %s", data[row], human_bytes(bytes[row]))
                    shown_buffer = buffer[row] == "-" ? "-" : human_bytes(buffer[row])
                    printf "| %-28s | %-14s | %10s | %5s | %9.3f | %8.3f | %8.3f +/- %8.3f | %-20s |\n", \
                           shown_dataset, implementation[row], shown_buffer, \
                           batch_size[row], \
                           user_seconds[row] * 1000.0, \
                           system_seconds[row] * 1000.0, \
                           wall_seconds[row] * 1000.0, \
                           wall_error[row] * 1000.0, comparison
                    previous_dataset = dataset_key
                }
                print separator
            }
        }
    ' >> "$summary"; then
    rm -f "$summary"
    exit 1
fi
printf '\nBenchmark comparison (mean times; wall +/- is estimated standard error):\n\n'
cat "$summary"
printf '\nSaved results: %s\n' "$output_dir"
