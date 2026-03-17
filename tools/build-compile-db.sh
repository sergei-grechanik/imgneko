#!/bin/sh

# Merge every existing compiler `-MJ` fragment under MJ_DIR into one
# compile_commands.json file. The Makefile calls this after build targets that
# may have emitted new fragments, so the database always reflects the fragments
# currently present in the build directory.

set -eu

if [ "$#" -ne 2 ]; then
    printf '%s\n' "Usage: $0 MJ_DIR OUTPUT_JSON" >&2
    exit 1
fi

mj_dir=$1
output_json=$2

output_dir=$(dirname "$output_json")
mkdir -p "$output_dir"

tmp_output=$output_json.tmp
trap 'rm -f "$tmp_output"' EXIT HUP INT TERM

{
    printf '[\n'
    first=1

    if [ -d "$mj_dir" ]; then
        find "$mj_dir" -type f -name '*.json' -print | LC_ALL=C sort |
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
