#!/usr/bin/env python3
# SPDX-License-Identifier: MIT-0

# This script generates functions to convert between row/column numbers,
# image-placeholder diacritics, and their UTF-8 byte strings.
# By default, it reads rowcolumn-diacritics.txt next to this script and writes
# src/imgneko/rowcolumn_diacritics.c relative to the repository root.
#
# The script also checks some desirable properties of row/column diacritics,
# e.g. that image placeholders are in normal form.

import argparse
from pathlib import Path
import unicodedata
import sys

SCRIPT_DIR = Path(__file__).resolve().parent
ROOT_DIR = SCRIPT_DIR.parent
DEFAULT_INPUT_PATH = SCRIPT_DIR / "rowcolumn-diacritics.txt"
DEFAULT_C_OUTPUT_PATH = ROOT_DIR / "src" / "imgneko" / "rowcolumn_diacritics.c"


def parse_args():
    parser = argparse.ArgumentParser(
        description="Generate the row/column diacritic C helper source file.",
    )
    parser.add_argument(
        "--input",
        type=Path,
        default=DEFAULT_INPUT_PATH,
        help="input row/column diacritic list",
    )
    parser.add_argument(
        "--output-c",
        type=Path,
        default=DEFAULT_C_OUTPUT_PATH,
        help="output C helper source file",
    )
    parser.add_argument(
        "--skip-checks",
        "--skip-normalization-checks",
        action="store_true",
        dest="skip_checks",
        help="skip expensive Unicode normalization checks",
    )
    return parser.parse_args()


def path_for_comment(path):
    try:
        return str(path.resolve().relative_to(ROOT_DIR))
    except ValueError:
        return str(path)


def c_byte_string(data):
    return "".join("\\x{:02x}".format(byte) for byte in data)


def print_array_fields(file, fields, values_per_line, field_width=None):
    if field_width is None:
        field_width = max(len(field) for field in fields) + 1

    for offset in range(0, len(fields), values_per_line):
        line_fields = fields[offset:offset + values_per_line]
        padded_fields = [field.ljust(field_width) for field in line_fields[:-1]]
        print("    " + "".join(padded_fields) + line_fields[-1], file=file)


args = parse_args()

# Codes of all row/column diacritics.
codes = []

with args.input.open("r", encoding="utf-8") as file:
    for line in file:
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        code = int(line.split(";")[0], 16)
        char = chr(code)
        assert unicodedata.combining(char) == 230
        codes.append(code)

print("Generating " + str(args.output_c))
args.output_c.parent.mkdir(parents=True, exist_ok=True)
with args.output_c.open("w", encoding="utf-8") as file:
    range_start_num = 1
    range_start = 0
    range_end = 0

    def print_range():
        if range_start >= range_end:
            return
        for code in range(range_start, range_end):
            print("    case " + hex(code) + ":", file=file)
        print("        return code - " + hex(range_start) + " + " +
              str(range_start_num) + ";",
              file=file)

    print("// SPDX-License-Identifier: Unicode-3.0", file=file)
    print("", file=file)
    print("// Convert between row/column numbers, the Unicode combining marks", file=file)
    print("// used by image placeholders, and their UTF-8 byte strings.", file=file)
    print("//", file=file)
    print("// This file is generated from " + path_for_comment(args.input) + ",", file=file)
    print("// which is derived from UnicodeData.txt. Regenerate it with", file=file)
    print("// " + path_for_comment(Path(__file__)) + "; do not edit it directly.", file=file)
    print("", file=file)
    print("#include <stdint.h>", file=file)
    print("", file=file)

    print("// clang-format off", file=file)
    print("static const uint32_t rowcolumn_diacritic_codes[] = {", file=file)

    # Print the codes in a nice format, 8 per line, with padding to align the commas.
    print_array_fields(file, [hex(code) + "," for code in codes],
                       values_per_line=8, field_width=9)

    print("};", file=file)
    print("", file=file)

    print("static const char rowcolumn_diacritic_utf8[][5] = {", file=file)
    utf8_fields = [
        '"' + c_byte_string(chr(code).encode("utf-8")) + '",'
        for code in codes
    ]
    print_array_fields(file, utf8_fields, values_per_line=4)
    print("};", file=file)
    print("", file=file)

    print("static const uint8_t rowcolumn_diacritic_utf8_lens[] = {", file=file)
    utf8_len_fields = [
        str(len(chr(code).encode("utf-8"))) + ","
        for code in codes
    ]
    print_array_fields(file, utf8_len_fields, values_per_line=24)
    print("};", file=file)
    print("// clang-format on", file=file)
    print("", file=file)

    print("static const uint16_t rowcolumn_diacritic_count =", file=file)
    print("    sizeof(rowcolumn_diacritic_codes) / sizeof(rowcolumn_diacritic_codes[0]);", file=file)
    print("", file=file)

    print("uint16_t rowcolumn_diacritic_to_num(uint32_t code) {", file=file)
    print("    switch (code) {", file=file)

    for code in codes:
        if range_end == code:
            range_end += 1
        else:
            print_range()
            range_start_num += range_end - range_start
            range_start = code
            range_end = code + 1
    print_range()

    print("    }", file=file)
    print("    return 0;", file=file)
    print("}", file=file)
    print("", file=file)

    print("uint32_t rowcolumn_num_to_diacritic(uint32_t num) {", file=file)
    print("    if (num == 0 || num > rowcolumn_diacritic_count)", file=file)
    print("        return 0;", file=file)
    print("", file=file)
    print("    return rowcolumn_diacritic_codes[num - 1];", file=file)
    print("}", file=file)
    print("", file=file)

    print("const char *rowcolumn_num_to_diacritic_utf8(uint32_t num,",
          "uint8_t *len_out) {", file=file)
    print("    if (len_out != 0)", file=file)
    print("        *len_out = 0;", file=file)
    print("", file=file)
    print("    if (num == 0 || num > rowcolumn_diacritic_count)", file=file)
    print("        return 0;", file=file)
    print("", file=file)
    print("    if (len_out != 0)", file=file)
    print("        *len_out = rowcolumn_diacritic_utf8_lens[num - 1];", file=file)
    print("    return rowcolumn_diacritic_utf8[num - 1];", file=file)
    print("}", file=file)

if args.skip_checks:
    print("Skipping normalization checks")
    sys.exit(0)

print("Checking that image placeholder cannot be normalized further")

img_char = chr(0x10EEEE)
for row_code in codes:
    row_char = chr(row_code)
    for col_code in codes:
        col_char = chr(col_code)
        cell = img_char + row_char + col_char
        for nf in ["NFC", "NFKC", "NFD", "NFKD"]:
            if not unicodedata.is_normalized(nf, cell):
                print(cell)
                print("unnormalized!", nf, [hex(ord(img_char)), hex(row_code), hex(col_code)])
                normalized = unicodedata.normalize(nf, cell)
                print("normalized:", [hex(ord(c)) for c in normalized])
                exit(1)

print("Checking that the row/column marks are not fused with anything "
      "letter-like during normalization")

# Collect somewhat normal characters.
normal_symbols = []
for i in range(sys.maxunicode):
    string = chr(i)
    if unicodedata.category(string)[0] not in ['L', 'P', 'N', 'S']:
        continue
    is_normalized = True
    for nf in ["NFC", "NFKC", "NFD", "NFKD"]:
        if not unicodedata.is_normalized(nf, string):
            is_normalized = False
    if is_normalized:
        normal_symbols.append(i)

for code in codes:
    print("Checking " + hex(code), end="\r")
    for num in normal_symbols:
        string = chr(num) + chr(code)
        for nf in ["NFC", "NFKC", "NFD", "NFKD"]:
            if not unicodedata.is_normalized(nf, string):
                normalized = unicodedata.normalize(nf, string)
                print("WARNING: " + hex(num) + " + " + hex(code) +
                      " is normalized to " + normalized,
                      " ".join(hex(ord(c)) for c in normalized))
