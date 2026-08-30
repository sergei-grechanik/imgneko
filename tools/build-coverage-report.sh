#!/bin/sh
# SPDX-License-Identifier: MIT-0

# Build coverage artifacts from Clang source-based profile data. `make
# coverage` populates OUTPUT_DIR/profiles with raw .profraw files, then calls
# this script to merge them and emit a summary plus quickfix-friendly lists of
# uncovered locations and unused source suppressions.

set -eu

if [ "$#" -lt 6 ]; then
    printf '%s\n' "usage: $0 ROOT_DIR BUILD_DIR OUTPUT_DIR LLVM_PROFDATA LLVM_COV PRIMARY_BINARY [EXTRA_OBJECT ...]" >&2
    exit 1
fi

ROOT_DIR=$1
BUILD_DIR=$2
OUTPUT_DIR=$3
LLVM_PROFDATA=$4
LLVM_COV=$5
# `llvm-cov export` expects one positional binary before any `-object` flags.
# Use the main application binary as that anchor, then attach every other
# instrumented test binary via `-object`.
PRIMARY_BINARY=$6
shift 6

PROFILE_DIR=$OUTPUT_DIR/profiles
PROFDATA=$OUTPUT_DIR/coverage.profdata
SUMMARY=$OUTPUT_DIR/summary.txt
UNCOVERED_QF=$OUTPUT_DIR/uncovered.qf
UNUSED_SUPPRESSIONS_QF=$OUTPUT_DIR/unused-suppressions.qf
TMP_JSON=$(mktemp)
TMP_WARNINGS=$(mktemp)
TMP_OBJECTS=$(mktemp)

cleanup() {
    rm -f "$TMP_JSON" "$TMP_WARNINGS" "$TMP_OBJECTS"
}

trap cleanup EXIT HUP INT TERM

if [ ! -d "$PROFILE_DIR" ]; then
    printf '%s\n' "error: missing raw profile directory: $PROFILE_DIR" >&2
    exit 1
fi

if ! command -v "$LLVM_PROFDATA" >/dev/null 2>&1; then
    printf '%s\n' "error: llvm-profdata executable not found: $LLVM_PROFDATA" >&2
    exit 1
fi

if ! command -v "$LLVM_COV" >/dev/null 2>&1; then
    printf '%s\n' "error: llvm-cov executable not found: $LLVM_COV" >&2
    exit 1
fi

if ! command -v python3 >/dev/null 2>&1; then
    printf '%s\n' "error: python3 is required to generate coverage reports" >&2
    exit 1
fi

printf '%s\n' "$@" >"$TMP_OBJECTS"

set -- "$PROFILE_DIR"/*.profraw
if [ ! -e "$1" ]; then
    printf '%s\n' "error: no raw coverage profiles found under $PROFILE_DIR; rerun the coverage-enabled tests first" >&2
    exit 1
fi

"$LLVM_PROFDATA" merge -sparse "$@" -o "$PROFDATA"

# Build the llvm-cov argv explicitly so each extra object is passed as its own
# `-object <path>` pair without relying on shell word-splitting.
set -- "$PRIMARY_BINARY" "-instr-profile=$PROFDATA"
while IFS= read -r object; do
    [ -n "$object" ] || continue
    set -- "$@" -object "$object"
done <"$TMP_OBJECTS"

"$LLVM_COV" export "$@" >"$TMP_JSON" 2>"$TMP_WARNINGS"

python3 "$ROOT_DIR/tools/build-coverage-report.py" \
    "$ROOT_DIR" "$BUILD_DIR" "$PROFDATA" "$TMP_JSON" "$SUMMARY" \
    "$UNCOVERED_QF" "$UNUSED_SUPPRESSIONS_QF"

if [ -s "$TMP_WARNINGS" ]; then
    printf '%s\n' "coverage tool warnings:" >&2
    sed -n '1,20p' "$TMP_WARNINGS" >&2
fi
