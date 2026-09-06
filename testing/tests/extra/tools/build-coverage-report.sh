#!/usr/bin/env run-and-check
# SPDX-License-Identifier: MIT-0
# RUN: sh %s

# Verify coverage-record transformations that require LLVM's structured data.
# This extra test runs the Python report generator on a synthetic LLVM export.
# Branches must use their recorded source file, and executed macro expansions
# must not leave false zero-count wrapper entries behind.

set -eu

fail() {
    printf '%s\n' "$1" >&2
    exit 1
}

if [ -z "${IMGNEKO_TEST_OUTPUT_DIR:-}" ] ||
   [ ! -d "$IMGNEKO_TEST_OUTPUT_DIR" ]; then
    fail "IMGNEKO_TEST_OUTPUT_DIR is not set to an existing directory"
fi

write_filler_lines() {
    path=$1
    last_line=$2
    prefix=$3

    : > "$path"
    i=1
    while [ "$i" -lt "$last_line" ]; do
        printf '%s %d\n' "$prefix" "$i" >> "$path"
        i=$((i + 1))
    done
}

FAKE_ROOT=$IMGNEKO_TEST_OUTPUT_DIR/fake-root
JSON_PATH=$IMGNEKO_TEST_OUTPUT_DIR/export.json
SUMMARY_PATH=$IMGNEKO_TEST_OUTPUT_DIR/summary.txt
UNCOVERED_PATH=$IMGNEKO_TEST_OUTPUT_DIR/uncovered.qf
UNUSED_SUPPRESSIONS_PATH=$IMGNEKO_TEST_OUTPUT_DIR/unused-suppressions.qf
PROFDATA_PATH=$IMGNEKO_TEST_OUTPUT_DIR/fake.profdata

mkdir -p "$FAKE_ROOT/src/util" "$FAKE_ROOT/testing/tools"

# Put a misleading comment at the translation-unit location that used to be
# reported by mistake.
write_filler_lines "$FAKE_ROOT/testing/tools/sample-cli.c" 154 "// sample-cli filler"
printf '%s\n' "// Release one owned grid-size option and reset it to the empty state." \
    >> "$FAKE_ROOT/testing/tools/sample-cli.c"

# Recreate just enough of string.h for the uncovered branch span to resolve to
# the real header location.
write_filler_lines "$FAKE_ROOT/src/util/string.h" 153 "// string filler"
printf '%s\n' "#define str_free(str) do {" >> "$FAKE_ROOT/src/util/string.h"
printf '%s\n' "            if ((str).capacity != 0)" >> "$FAKE_ROOT/src/util/string.h"
printf '%s\n' "                free((str).cstr);" >> "$FAKE_ROOT/src/util/string.h"
printf '%s\n' "        } while (0)" >> "$FAKE_ROOT/src/util/string.h"

# Provide adjacent executed-expansion, unexecuted-expansion, and ordinary
# uncovered lines so the report must distinguish all three zero segments.
write_filler_lines "$FAKE_ROOT/src/util/expansions.c" 9 "// expansion filler"
printf '%s\n' "EXECUTED_FIELDS(HANDLE_FIELD)" >> "$FAKE_ROOT/src/util/expansions.c"
printf '%s\n' "NEVER_EXECUTED_FIELDS(HANDLE_FIELD)" >> "$FAKE_ROOT/src/util/expansions.c"
printf '%s\n' "real_uncovered_statement();" >> "$FAKE_ROOT/src/util/expansions.c"
printf '%s\n' "covered_statement();" >> "$FAKE_ROOT/src/util/expansions.c"

# Mix triggered and unused forms of every suppression directive. The branch and
# function records below exercise findings that do not appear in file segments.
printf '%s\n' \
    "// IMGNEKO_UNCOVERED_OK: triggered standalone" \
    "triggered_standalone();" \
    "// IMGNEKO_UNCOVERED_OK: unused standalone" \
    "covered_standalone();" \
    "// IMGNEKO_UNCOVERED_OK[2 lines]: triggered count" \
    "triggered_count();" \
    "covered_count_tail();" \
    "// IMGNEKO_UNCOVERED_OK[2 lines]: unused count" \
    "covered_count_first();" \
    "covered_count_second();" \
    "// IMGNEKO_UNCOVERED_OK_START: triggered block" \
    "triggered_block();" \
    "// IMGNEKO_UNCOVERED_OK_END" \
    "// IMGNEKO_UNCOVERED_OK_START: unused block" \
    "covered_block();" \
    "// IMGNEKO_UNCOVERED_OK_END" \
    "triggered_inline(); // IMGNEKO_UNCOVERED_OK" \
    "covered_inline(); // IMGNEKO_UNCOVERED_OK" \
    "int never_called(void) { // IMGNEKO_UNCOVERED_OK" \
    "if (flag) // IMGNEKO_UNCOVERED_OK" \
    "covered_sentinel();" \
    > "$FAKE_ROOT/src/util/suppressions.c"

cat > "$JSON_PATH" <<EOF
{
  "data": [
    {
      "files": [
        {
          "filename": "$FAKE_ROOT/testing/tools/sample-cli.c",
          "branches": [],
          "expansions": [],
          "mcdc_records": [],
          "segments": [],
          "summary": {
            "lines": {"count": 1, "covered": 1, "percent": 100.0},
            "regions": {"count": 1, "covered": 1, "percent": 100.0},
            "functions": {"count": 1, "covered": 1, "percent": 100.0},
            "instantiations": {"count": 1, "covered": 1, "percent": 100.0},
            "branches": {"count": 0, "covered": 0, "notcovered": 0, "percent": 100.0},
            "mcdc": {"count": 0, "covered": 0, "notcovered": 0, "percent": 100.0}
          }
        },
        {
          "filename": "$FAKE_ROOT/src/util/string.h",
          "branches": [],
          "expansions": [],
          "mcdc_records": [],
          "segments": [],
          "summary": {
            "lines": {"count": 1, "covered": 1, "percent": 100.0},
            "regions": {"count": 1, "covered": 1, "percent": 100.0},
            "functions": {"count": 0, "covered": 0, "percent": 100.0},
            "instantiations": {"count": 0, "covered": 0, "percent": 100.0},
            "branches": {"count": 1, "covered": 0, "notcovered": 1, "percent": 0.0},
            "mcdc": {"count": 0, "covered": 0, "notcovered": 0, "percent": 100.0}
          }
        },
        {
          "filename": "$FAKE_ROOT/src/util/expansions.c",
          "branches": [],
          "expansions": [
            {
              "filenames": [
                "$FAKE_ROOT/src/util/expansions.c",
                "$FAKE_ROOT/src/util/expansions.c",
                "$FAKE_ROOT/src/util/expansions.c",
                "$FAKE_ROOT/src/util/expansions.c",
                "$FAKE_ROOT/src/util/expansions.c"
              ],
              "source_region": [9, 1, 9, 32, 0, 0, 1, 1],
              "target_regions": [
                [100, 1, 100, 20, 0, 1, 3, 1],
                [200, 1, 200, 2, 4, 3, 0, 0]
              ]
            },
            {
              "filenames": [
                "$FAKE_ROOT/src/util/expansions.c",
                "$FAKE_ROOT/src/util/expansions.c",
                "$FAKE_ROOT/src/util/expansions.c",
                "$FAKE_ROOT/src/util/expansions.c",
                "$FAKE_ROOT/src/util/expansions.c"
              ],
              "source_region": [10, 1, 10, 39, 0, 0, 2, 1],
              "target_regions": [
                [110, 1, 110, 20, 0, 2, 4, 1],
                [210, 1, 210, 2, 0, 4, 0, 0]
              ]
            }
          ],
          "mcdc_records": [],
          "segments": [
            [9, 1, 0, true, true, false],
            [9, 32, 4, true, true, false],
            [10, 1, 0, true, true, false],
            [10, 39, 4, true, true, false],
            [11, 1, 0, true, true, false],
            [11, 28, 4, true, true, false],
            [12, 1, 4, true, true, false]
          ],
          "summary": {
            "lines": {"count": 4, "covered": 2, "percent": 50.0},
            "regions": {"count": 3, "covered": 1, "percent": 33.33},
            "functions": {"count": 1, "covered": 1, "percent": 100.0},
            "instantiations": {"count": 1, "covered": 1, "percent": 100.0},
            "branches": {"count": 0, "covered": 0, "notcovered": 0, "percent": 100.0},
            "mcdc": {"count": 0, "covered": 0, "notcovered": 0, "percent": 100.0}
          }
        },
        {
          "filename": "$FAKE_ROOT/src/util/suppressions.c",
          "branches": [],
          "expansions": [],
          "mcdc_records": [],
          "segments": [
            [2, 1, 0, true, true, false],
            [2, 24, 1, true, true, false],
            [4, 1, 1, true, true, false],
            [6, 1, 0, true, true, false],
            [6, 20, 1, true, true, false],
            [7, 1, 1, true, true, false],
            [9, 1, 1, true, true, false],
            [10, 1, 1, true, true, false],
            [12, 1, 0, true, true, false],
            [12, 20, 1, true, true, false],
            [15, 1, 1, true, true, false],
            [17, 1, 0, true, true, false],
            [17, 20, 1, true, true, false],
            [18, 1, 1, true, true, false],
            [21, 1, 1, true, true, false]
          ],
          "summary": {
            "lines": {"count": 12, "covered": 9, "percent": 75.0},
            "regions": {"count": 12, "covered": 9, "percent": 75.0},
            "functions": {"count": 2, "covered": 1, "percent": 50.0},
            "instantiations": {"count": 2, "covered": 1, "percent": 50.0},
            "branches": {"count": 1, "covered": 0, "notcovered": 1, "percent": 0.0},
            "mcdc": {"count": 0, "covered": 0, "notcovered": 0, "percent": 100.0}
          }
        }
      ],
      "functions": [
        {
          "name": "sample-cli.c:print_quoted_text",
          "count": 13,
          "filenames": [
            "$FAKE_ROOT/testing/tools/sample-cli.c",
            "$FAKE_ROOT/src/util/string.h",
            "$FAKE_ROOT/src/util/string.h"
          ],
          "regions": [
            [412, 49, 417, 2, 13, 0, 0, 0]
          ],
          "branches": [
            [154, 13, 154, 32, 13, 0, 2, 0, 4]
          ],
          "mcdc_records": []
        },
        {
          "name": "never_called",
          "count": 0,
          "filenames": ["$FAKE_ROOT/src/util/suppressions.c"],
          "regions": [[19, 1, 19, 25, 0, 0, 0, 0]],
          "branches": [],
          "mcdc_records": []
        },
        {
          "name": "branch_holder",
          "count": 4,
          "filenames": ["$FAKE_ROOT/src/util/suppressions.c"],
          "regions": [[20, 1, 21, 20, 4, 0, 0, 0]],
          "branches": [[20, 1, 20, 9, 4, 0, 0, 0, 4]],
          "mcdc_records": []
        }
      ],
      "totals": {
        "lines": {"count": 2, "covered": 2, "percent": 100.0},
        "regions": {"count": 2, "covered": 2, "percent": 100.0},
        "functions": {"count": 1, "covered": 1, "percent": 100.0},
        "instantiations": {"count": 1, "covered": 1, "percent": 100.0},
        "branches": {"count": 1, "covered": 0, "notcovered": 1, "percent": 0.0},
        "mcdc": {"count": 0, "covered": 0, "notcovered": 0, "percent": 100.0}
      }
    }
  ],
  "type": "llvm.coverage.json.export",
  "version": "2.0.1"
}
EOF

python3 "$IMGNEKO_ROOT_DIR/tools/build-coverage-report.py" \
    "$FAKE_ROOT" \
    "$IMGNEKO_BUILD_DIR" \
    "$PROFDATA_PATH" \
    "$JSON_PATH" \
    "$SUMMARY_PATH" \
    "$UNCOVERED_PATH" \
    "$UNUSED_SUPPRESSIONS_PATH"

if grep -F "testing/tools/sample-cli.c:154:13" "$UNCOVERED_PATH" >/dev/null 2>&1; then
    fail "coverage report used the instantiating .c file instead of the header"
fi

if grep -F "ne owned grid-size" "$UNCOVERED_PATH" >/dev/null 2>&1; then
    fail "coverage report printed the stale comment text instead of the branch span"
fi

if grep -F ": EXECUTED_FIELDS(HANDLE_FIELD)" "$UNCOVERED_PATH" >/dev/null 2>&1; then
    fail "coverage report included an executed macro expansion wrapper"
fi

if grep -F "src/util/suppressions.c:19:" \
       "$UNUSED_SUPPRESSIONS_PATH" >/dev/null 2>&1 ||
   grep -F "src/util/suppressions.c:20:" \
       "$UNUSED_SUPPRESSIONS_PATH" >/dev/null 2>&1; then
    fail "coverage report treated a branch/function suppression as unused"
fi

printf '== uncovered ==\n'
cat "$UNCOVERED_PATH"
printf '== unused suppressions ==\n'
cat "$UNUSED_SUPPRESSIONS_PATH"

# CHECK: == uncovered ==
# CHECK-NEXT: src/util/expansions.c:10:1: uncovered line: NEVER_EXECUTED_FIELDS(HANDLE_FIELD)
# CHECK-NEXT: src/util/expansions.c:11:1: uncovered line: real_uncovered_statement();
# CHECK-NEXT: src/util/string.h:154:13: branch not fully covered [true=13 false=0]: if ((str).capacity
# CHECK-NEXT: == unused suppressions ==
# CHECK-NEXT: src/util/suppressions.c:3:1: unused coverage suppression: // IMGNEKO_UNCOVERED_OK: unused standalone
# CHECK-NEXT: src/util/suppressions.c:8:1: unused coverage suppression: // IMGNEKO_UNCOVERED_OK[2 lines]: unused count
# CHECK-NEXT: src/util/suppressions.c:14:1: unused coverage suppression: // IMGNEKO_UNCOVERED_OK_START: unused block
# CHECK-NEXT: src/util/suppressions.c:18:1: unused coverage suppression: covered_inline(); // IMGNEKO_UNCOVERED_OK
