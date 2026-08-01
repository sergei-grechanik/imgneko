// SPDX-License-Identifier: MIT-0

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "imgneko/rowcolumn_diacritics.h"
#include "test_main.h"
#include "util/common.h"

// Decode the small subset of UTF-8 forms used by the generated row/column
// diacritics. The caller only passes valid generated strings.
static uint32_t decode_generated_utf8(const char *bytes, uint8_t len) {
    const unsigned char *data = (const unsigned char *)bytes;

    if (len == 2)
        return ((uint32_t)(data[0] & 0x1F) << 6) | (uint32_t)(data[1] & 0x3F);
    if (len == 3)
        return ((uint32_t)(data[0] & 0x0F) << 12) |
               ((uint32_t)(data[1] & 0x3F) << 6) | (uint32_t)(data[2] & 0x3F);

    return ((uint32_t)(data[0] & 0x07) << 18) |
           ((uint32_t)(data[1] & 0x3F) << 12) |
           ((uint32_t)(data[2] & 0x3F) << 6) | (uint32_t)(data[3] & 0x3F);
}

// Check that every number promised by ROWCOLUMN_DIACRITIC_MAX has a generated
// diacritic and that converting it back preserves the number.
static int test_all_numbers_round_trip(TestContext *ctx) {
    const char *name = ctx->test_name;

    for (uint32_t num = 1; num <= ROWCOLUMN_DIACRITIC_MAX; ++num) {
        uint32_t code = rowcolumn_num_to_diacritic(num);
        if (code == 0) {
            fprintf(stderr, "%s: number %u is not representable\n", name,
                    (unsigned)num);
            return 1;
        }

        uint16_t round_tripped_num = rowcolumn_diacritic_to_num(code);
        if (round_tripped_num != num) {
            fprintf(stderr,
                    "%s: number %u round-tripped through U+%04X as %u\n", name,
                    (unsigned)num, (unsigned)code, (unsigned)round_tripped_num);
            return 1;
        }
    }

    return 0;
}

// Check that the generated UTF-8 strings encode the same code points as the
// generated numeric lookup table.
static int test_utf8_strings_match_code_points(TestContext *ctx) {
    const char *name = ctx->test_name;

    for (uint32_t num = 1; num <= ROWCOLUMN_DIACRITIC_MAX; ++num) {
        uint8_t len = 0;
        const char *bytes = rowcolumn_num_to_diacritic_utf8(num, &len);
        if (bytes == NULL) {
            fprintf(stderr, "%s: number %u has no UTF-8 string\n", name,
                    (unsigned)num);
            return 1;
        }
        if (len < 2 || len > 4 || bytes[len] != '\0') {
            fprintf(stderr, "%s: number %u has invalid UTF-8 length %u\n", name,
                    (unsigned)num, (unsigned)len);
            return 1;
        }
        if (strlen(bytes) != len) {
            fprintf(stderr, "%s: number %u has mismatched UTF-8 strlen\n", name,
                    (unsigned)num);
            return 1;
        }

        const char *bytes_without_len =
            rowcolumn_num_to_diacritic_utf8(num, NULL);
        if (bytes_without_len != bytes) {
            fprintf(stderr,
                    "%s: number %u returned different UTF-8 strings with NULL "
                    "len_out\n",
                    name, (unsigned)num);
            return 1;
        }

        uint32_t decoded_code = decode_generated_utf8(bytes, len);
        uint32_t expected_code = rowcolumn_num_to_diacritic(num);
        if (decoded_code != expected_code) {
            fprintf(stderr,
                    "%s: number %u UTF-8 decoded to U+%04X, expected U+%04X\n",
                    name, (unsigned)num, (unsigned)decoded_code,
                    (unsigned)expected_code);
            return 1;
        }
    }

    return 0;
}

// Check the documented sentinel value for numbers outside the representable
// range.
static int test_unrepresentable_numbers_return_zero(TestContext *ctx) {
    const char *name = ctx->test_name;

    if (rowcolumn_num_to_diacritic(0) != 0) {
        fprintf(stderr, "%s: zero was unexpectedly representable\n", name);
        return 1;
    }

    // Check a small range after the documented maximum so the test catches an
    // implementation table that is longer than the public constant.
    for (uint32_t num = ROWCOLUMN_DIACRITIC_MAX + 1;
         num <= ROWCOLUMN_DIACRITIC_MAX + 4096; ++num) {
        if (rowcolumn_num_to_diacritic(num) != 0) {
            fprintf(stderr,
                    "%s: number %u after ROWCOLUMN_DIACRITIC_MAX was "
                    "representable\n",
                    name, (unsigned)num);
            return 1;
        }

        uint8_t len = 42;
        if (rowcolumn_num_to_diacritic_utf8(num, &len) != NULL || len != 0) {
            fprintf(stderr,
                    "%s: number %u after ROWCOLUMN_DIACRITIC_MAX had a UTF-8 "
                    "representation\n",
                    name, (unsigned)num);
            return 1;
        }
    }

    if (rowcolumn_num_to_diacritic(UINT16_MAX) != 0) {
        fprintf(stderr, "%s: UINT16_MAX was unexpectedly representable\n",
                name);
        return 1;
    }

    if (rowcolumn_num_to_diacritic(UINT32_MAX) != 0) {
        fprintf(stderr, "%s: UINT32_MAX was unexpectedly representable\n",
                name);
        return 1;
    }

    uint8_t len = 42;
    if (rowcolumn_num_to_diacritic_utf8(0, &len) != NULL || len != 0) {
        fprintf(stderr, "%s: zero had a UTF-8 representation\n", name);
        return 1;
    }

    len = 42;
    if (rowcolumn_num_to_diacritic_utf8(ROWCOLUMN_DIACRITIC_MAX + 1, &len) !=
            NULL ||
        len != 0) {
        fprintf(stderr,
                "%s: number after ROWCOLUMN_DIACRITIC_MAX had a UTF-8 "
                "representation\n",
                name);
        return 1;
    }

    len = 42;
    if (rowcolumn_num_to_diacritic_utf8(UINT32_MAX, &len) != NULL || len != 0) {
        fprintf(stderr, "%s: UINT32_MAX had a UTF-8 representation\n", name);
        return 1;
    }

    if (rowcolumn_num_to_diacritic_utf8(0, NULL) != NULL) {
        fprintf(stderr,
                "%s: zero with NULL len_out had a UTF-8 representation\n",
                name);
        return 1;
    }

    return 0;
}

// Check that values that are not row/column diacritics use the documented
// sentinel value instead of being mistaken for row/column numbers.
static int test_non_diacritic_codes_return_zero(TestContext *ctx) {
    const char *name = ctx->test_name;
    const uint32_t codes[] = {
        0, 'A', ' ', 0x10EEEE, 0x110000, UINT32_MAX,
    };

    for (size_t i = 0; i < ARRAY_SIZE(codes); ++i) {
        uint16_t num = rowcolumn_diacritic_to_num(codes[i]);
        if (num != 0) {
            fprintf(stderr, "%s: non-diacritic 0x%08X mapped to %u\n", name,
                    (unsigned)codes[i], (unsigned)num);
            return 1;
        }
    }

    return 0;
}

int main(int argc, char **argv) {
    const Subtest subtests[] = {
        PREFIXED_TEST(test_all_numbers_round_trip),
        PREFIXED_TEST(test_utf8_strings_match_code_points),
        PREFIXED_TEST(test_unrepresentable_numbers_return_zero),
        PREFIXED_TEST(test_non_diacritic_codes_return_zero),
    };

    return run_subtests(argc, argv, subtests, ARRAY_SIZE(subtests));
}
