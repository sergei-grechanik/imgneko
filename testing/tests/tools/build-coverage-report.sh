#!/usr/bin/env run-and-check
# RUN: sh %s

# Verify that coverage quickfix entries use the branch record's source file,
# not the instantiating translation unit. This regressed for inline header
# branches and produced bogus locations such as `sample-cli.c:154:13` that
# pointed at unrelated comments.

set -eu

fail() {
    printf '%s\n' "$1" >&2
    exit 1
}

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
    "$UNCOVERED_PATH"

if grep -F "testing/tools/sample-cli.c:154:13" "$UNCOVERED_PATH" >/dev/null 2>&1; then
    fail "coverage report used the instantiating .c file instead of the header"
fi

if grep -F "ne owned grid-size" "$UNCOVERED_PATH" >/dev/null 2>&1; then
    fail "coverage report printed the stale comment text instead of the branch span"
fi

printf '== uncovered ==\n'
cat "$UNCOVERED_PATH"

# CHECK: == uncovered ==
# CHECK-NEXT: src/util/string.h:154:13: branch not fully covered [true=13 false=0]: if ((str).capacity
