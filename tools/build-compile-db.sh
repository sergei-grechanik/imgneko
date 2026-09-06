#!/bin/sh
# SPDX-License-Identifier: MIT-0

# Merge every existing compiler `-MJ` fragment under OBJ_DIR into one
# compile_commands.json file. The Makefile calls this after build targets that
# may have emitted new fragments, so the database always reflects the fragments
# currently present in the build directory.

set -eu

if [ "$#" -ne 2 ]; then
    printf '%s\n' "Usage: $0 OBJ_DIR OUTPUT_JSON" >&2
    exit 1
fi

obj_dir=$1
output_json=$2

output_dir=$(dirname "$output_json")
mkdir -p "$output_dir"

# Independent refreshes can run concurrently. Give each writer its own file
# beside the destination so the final rename remains atomic.
tmp_output=$(mktemp "$output_json.tmp.XXXXXX")
trap 'rm -f "$tmp_output"' EXIT HUP INT TERM

{
    printf '[\n'
    first=1

    if [ -d "$obj_dir" ]; then
        find "$obj_dir" -type f -name '*.json' -print | LC_ALL=C sort |
        while IFS= read -r fragment; do
            if [ "$first" -eq 0 ]; then
                printf ',\n'
            fi
            sed '$s/,[[:space:]]*$//' "$fragment"
            first=0
        done
    fi

    printf '\n]\n'
} >"$tmp_output"

mv "$tmp_output" "$output_json"
