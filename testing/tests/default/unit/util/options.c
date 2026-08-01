// SPDX-License-Identifier: MIT-0

#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_main.h"
#include "util/common.h"
#include "util/error.h"
#include "util/options.h"

// Local test parser definitions.

// An owned NxM value used to exercise custom parse, copy, and clear hooks.
typedef struct Size2 {
    int width;
    int height;
} Size2;

// An inline START:END range used to exercise POD custom parsing.
typedef struct Range2 {
    int start;
    int end;
} Range2;

// An owning array of START:END ranges used to exercise custom list parsing.
DEFINE_ARRAY_TYPE(Range2Array, Range2)

// Optional owned NxM value plus provenance metadata.
OPT_DEFINE_WRAPPER_STRUCT(OptSize2, Size2 *);

// Optional inline START:END range plus provenance metadata.
OPT_DEFINE_WRAPPER_STRUCT(OptRange2, Range2);

// Optional START:END list plus provenance metadata.
OPT_DEFINE_WRAPPER_STRUCT(OptRange2List, Range2Array);

// Parse a START:END value.
static bool parse_range2_value(const char *text, size_t text_len, Range2 *out) {
    const char *separator = NULL;
    int start = 0;
    int end = 0;
    size_t start_len = 0;
    size_t end_len = 0;

    if (text == NULL || text_len == 0)
        return false;

    separator = memchr(text, ':', text_len);
    if (separator == NULL)
        return false;
    start_len = (size_t)(separator - text);
    end_len = text_len - start_len - 1;
    if (memchr(separator + 1, ':', end_len) != NULL)
        return false;
    if (!opt_parse_int_option(&start, text, start_len, NULL))
        return false;
    if (!opt_validate_positive_int(&start, NULL))
        return false;
    if (!opt_parse_int_option(&end, separator + 1, end_len, NULL))
        return false;
    if (!opt_validate_positive_int(&end, NULL))
        return false;
    if (start > end)
        return false;

    *out = (Range2){.start = start, .end = end};
    return true;
}

// Parse a START:END string into a POD OptRange2 wrapper.
static bool parse_range2_option(void *value_ptr, const char *text,
                                size_t text_len, String *error_out) {
    if (!parse_range2_value(text, text_len, value_ptr))
        return opt_parse_error(
            error_out,
            "expected START:END with positive integers and START <= END");

    return true;
}

// Clear a START:END list.
static void clear_range2_list_option(void *value_ptr) {
    Range2Array *value = value_ptr;

    arr_free(*value);
}

// Deep-copy a START:END list.
static void copy_range2_list_option(void *dst_value, const void *src_value) {
    const Range2Array *src = src_value;
    Range2Array *dst = dst_value;

    *dst = copy_Range2Array(*src);
}

// Parse a START:END value and append it to a list.
static bool parse_range2_list_option(void *value_ptr, const char *text,
                                     size_t text_len, String *error_out) {
    Range2Array *value = value_ptr;
    Range2 parsed = {0};

    if (!parse_range2_value(text, text_len, &parsed))
        return opt_parse_error(
            error_out,
            "expected START:END with positive integers and START <= END");

    arr_push(*value, parsed);
    return true;
}

// Release the owned NxM option and reset it to the empty state.
static void clear_size2_option(void *value_ptr) {
    Size2 **value = value_ptr;

    free(*value);
    *value = NULL;
}

// Deep-copy the owned NxM option.
static void copy_size2_option(void *dst_value, const void *src_value) {
    const Size2 *const *src = src_value;
    Size2 **dst = dst_value;
    Size2 *copy = NULL;

    if (*src == NULL) {
        *dst = NULL;
        return;
    }

    copy = calloc(1, sizeof(*copy));
    require(copy != NULL, "failed to allocate test NxM copy: %errno");
    *copy = **src;
    *dst = copy;
}

// Parse an NxM string into an owned OptSize2 wrapper.
static bool parse_size2_option(void *value_ptr, const char *text,
                               size_t text_len, String *error_out) {
    Size2 **value = value_ptr;
    const char *separator = NULL;
    Size2 *parsed = NULL;
    int width = 0;
    int height = 0;
    size_t width_len = 0;
    size_t height_len = 0;

    if (text == NULL || text_len == 0)
        return opt_parse_error(error_out,
                               "expected NxM with positive integers");

    for (size_t i = 0; i < text_len; ++i) {
        if (text[i] != 'x' && text[i] != 'X')
            continue;
        if (separator != NULL)
            return opt_parse_error(error_out,
                                   "expected exactly one x separator");
        separator = text + i;
    }
    if (separator == NULL)
        return opt_parse_error(error_out,
                               "expected NxM with positive integers");
    width_len = (size_t)(separator - text);
    height_len = text_len - width_len - 1;
    if (!opt_parse_int_option(&width, text, width_len, NULL))
        return opt_parse_error(error_out, "width must be a positive integer");
    if (!opt_validate_positive_int(&width, NULL))
        return opt_parse_error(error_out, "width must be a positive integer");
    if (!opt_parse_int_option(&height, separator + 1, height_len, NULL))
        return opt_parse_error(error_out, "height must be a positive integer");
    if (!opt_validate_positive_int(&height, NULL)) {
        return opt_parse_error(error_out, "height must be a positive integer");
    }

    parsed = calloc(1, sizeof(*parsed));
    require(parsed != NULL, "failed to allocate test NxM value: %errno");
    parsed->width = width;
    parsed->height = height;

    clear_size2_option(value);
    *value = parsed;
    return true;
}

#define TEST_COMMON_OPTIONS(X, S)                                              \
    X(S, verbose, OptBool,                                                     \
      OPT_BOOL_FLAG(.cli = "-v --verbose", .descr = "Enable verbose output.")) \
    X(S, count, OptInt,                                                        \
      OPT_INT(.cli = "-n --count N", .descr = "A sample integer option.",      \
              .dflt = "3"))

#define TEST_SHOW_OPTIONS(X, S)                                                \
    TEST_COMMON_OPTIONS(X, S)                                                  \
    X(S, range, OptRange2,                                                     \
      OPT_CUSTOM(.parse = parse_range2_option, .cli = "--range START:END",     \
                 .descr = "A sample START:END option.", .dflt = "1:3"))        \
    X(S, size, OptSize2,                                                       \
      OPT_CUSTOM(.parse = parse_size2_option, .copy = copy_size2_option,       \
                 .clear = clear_size2_option, .cli = "--size NxM",             \
                 .descr = "A sample NxM option.", .dflt = "80x24"))            \
    X(S, theme, OptString,                                                     \
      OPT_STRING(.cli = "--theme NAME", .descr = "A sample string option.",    \
                 .dflt = "auto"))                                              \
    X(S, cache, OptBool,                                                       \
      OPT_BOOL_NEGATABLE(.cli = "-c --cache", .cli_negate = "-C --no-cache",   \
                         .descr = "A sample negatable bool option."))          \
    X(S, images, OptStringList,                                                \
      OPT_STRING_LIST(.cli = "IMAGE", .descr = "Input image paths.",           \
                      .positional = true))

#define TEST_STATUS_OPTIONS(X, S)                                              \
    TEST_COMMON_OPTIONS(X, S)                                                  \
    X(S, format, OptString,                                                    \
      OPT_STRING(.cli = "--format NAME", .descr = "Status output format.",     \
                 .dflt = "summary"))                                           \
    X(S, strict, OptBool,                                                      \
      OPT_BOOL_VALUE(.cli = "--strict BOOL",                                   \
                     .descr = "Explicit boolean value."))                      \
    X(S, windows, OptRange2List,                                               \
      OPT_CUSTOM_LIST(.parse = parse_range2_list_option,                       \
                      .copy = copy_range2_list_option,                         \
                      .clear = clear_range2_list_option,                       \
                      .cli = "--window START:END",                             \
                      .descr = "Repeated range list option."))                 \
    X(S, targets, OptStringList,                                               \
      OPT_STRING_LIST(.cli = "TARGET", .descr = "Status targets.",             \
                      .positional = true))

#define TEST_GLOBAL_OPTIONS(X, S)                                              \
    TEST_COMMON_OPTIONS(X, S)                                                  \
    X(S, range, OptRange2,                                                     \
      OPT_CUSTOM(.parse = parse_range2_option, .cli = "--range START:END",     \
                 .descr = "A sample START:END option.", .dflt = "1:3"))        \
    X(S, size, OptSize2,                                                       \
      OPT_CUSTOM(.parse = parse_size2_option, .copy = copy_size2_option,       \
                 .clear = clear_size2_option, .cli = "--size NxM",             \
                 .descr = "A sample NxM option.", .dflt = "80x24"))            \
    X(S, theme, OptString,                                                     \
      OPT_STRING(.cli = "--theme NAME", .descr = "A sample string option.",    \
                 .dflt = "auto"))                                              \
    X(S, cache, OptBool,                                                       \
      OPT_BOOL_NEGATABLE(.cli = "-c --cache", .cli_negate = "-C --no-cache",   \
                         .descr = "A sample negatable bool option."))          \
    X(S, images, OptStringList,                                                \
      OPT_STRING_LIST(.cli = "IMAGE", .descr = "Input image paths.",           \
                      .positional = true))                                     \
    X(S, format, OptString,                                                    \
      OPT_STRING(.cli = "--format NAME", .descr = "Status output format.",     \
                 .dflt = "summary"))                                           \
    X(S, strict, OptBool,                                                      \
      OPT_BOOL_VALUE(.cli = "--strict BOOL",                                   \
                     .descr = "Explicit boolean value."))                      \
    X(S, windows, OptRange2List,                                               \
      OPT_CUSTOM_LIST(.parse = parse_range2_list_option,                       \
                      .copy = copy_range2_list_option,                         \
                      .clear = clear_range2_list_option,                       \
                      .cli = "--window START:END",                             \
                      .descr = "Repeated range list option."))                 \
    X(S, targets, OptStringList,                                               \
      OPT_STRING_LIST(.cli = "TARGET", .descr = "Status targets.",             \
                      .positional = true))

OPT_DEFINE_STRUCT(TestShowOptions, TEST_SHOW_OPTIONS)
OPT_DEFINE_STRUCT(TestStatusOptions, TEST_STATUS_OPTIONS)
OPT_DEFINE_STRUCT(TestGlobalOptions, TEST_GLOBAL_OPTIONS)

// One intentionally incompatible schema used to cover merge type mismatches.
#define TEST_MISMATCH_OPTIONS(X, S)                                            \
    X(S, verbose, OptString,                                                   \
      OPT_STRING(.cli = "--verbose TEXT",                                      \
                 .descr = "An intentionally mismatched field type."))

OPT_DEFINE_STRUCT(TestMismatchOptions, TEST_MISMATCH_OPTIONS)

// One schema with a missing `.parse` hook, used to verify that assignment
// fails cleanly without mutating the destination wrapper.
#define TEST_NO_PARSE_OPTIONS(X, S)                                            \
    X(S, raw, OptInt,                                                          \
      OPT_ATTRS(.cli = "--raw VALUE",                                          \
                .descr = "A field without a parser callback."))

OPT_DEFINE_STRUCT(TestNoParseOptions, TEST_NO_PARSE_OPTIONS)

// Assertions.

// Print a failure message for a subtest and return a failing status code.
static int fail_message(const char *subtest, const char *message) {
    fprintf(stderr, "%s: %s\n", subtest, message);
    return 1;
}

// Verify that an integer option matches the expected value and provenance.
static int expect_int_option(const char *subtest, OptInt option, int value,
                             OptProvenance provenance) {
    if (!option.is_set)
        return fail_message(subtest, "expected integer option to be set");
    if (option.value != value)
        return fail_message(subtest, "unexpected integer option value");
    if (option.provenance != provenance)
        return fail_message(subtest, "unexpected integer option provenance");
    return 0;
}

// Verify that a boolean option matches the expected value and provenance.
static int expect_bool_option(const char *subtest, OptBool option, bool value,
                              OptProvenance provenance) {
    if (!option.is_set)
        return fail_message(subtest, "expected boolean option to be set");
    if (option.value != value)
        return fail_message(subtest, "unexpected boolean option value");
    if (option.provenance != provenance)
        return fail_message(subtest, "unexpected boolean option provenance");
    return 0;
}

// Verify that a string option matches the expected value and provenance.
static int expect_string_option(const char *subtest, OptString option,
                                const char *value, OptProvenance provenance) {
    if (!option.is_set)
        return fail_message(subtest, "expected string option to be set");
    if (strcmp(option.value.cstr, value) != 0)
        return fail_message(subtest, "unexpected string option value");
    if (option.provenance != provenance)
        return fail_message(subtest, "unexpected string option provenance");
    return 0;
}

// Verify that an owned NxM option matches the expected pair and provenance.
static int expect_size2_option(const char *subtest, OptSize2 option, int width,
                               int height, OptProvenance provenance) {
    if (!option.is_set)
        return fail_message(subtest, "expected NxM option to be set");
    if (option.value == NULL)
        return fail_message(subtest,
                            "expected NxM option value to be allocated");
    if (option.value->width != width || option.value->height != height)
        return fail_message(subtest, "unexpected NxM option value");
    if (option.provenance != provenance)
        return fail_message(subtest, "unexpected NxM option provenance");
    return 0;
}

// Verify that a POD START:END option matches the expected pair and provenance.
static int expect_range2_option(const char *subtest, OptRange2 option,
                                int start, int end, OptProvenance provenance) {
    if (!option.is_set)
        return fail_message(subtest, "expected START:END option to be set");
    if (option.value.start != start || option.value.end != end)
        return fail_message(subtest, "unexpected START:END option value");
    if (option.provenance != provenance)
        return fail_message(subtest, "unexpected START:END option provenance");
    return 0;
}

// Verify that a string-list option matches the expected values and provenance.
static int expect_string_list_option(const char *subtest, OptStringList option,
                                     const char **values, size_t value_count,
                                     OptProvenance provenance) {
    if (!option.is_set)
        return fail_message(subtest, "expected string-list option to be set");
    if (option.value.size != value_count)
        return fail_message(subtest, "unexpected string-list length");
    if (option.provenance != provenance)
        return fail_message(subtest, "unexpected string-list provenance");

    for (size_t i = 0; i < value_count; ++i) {
        if (strcmp(option.value.data[i].cstr, values[i]) != 0)
            return fail_message(subtest, "unexpected string-list value");
    }

    return 0;
}

// Tests.

// Defaults should be materialized during init, and merge should deep-copy the
// matching set fields into a wider global schema.
static int test_defaults_and_merge(TestContext *ctx) {
    const char *name = ctx->test_name;
    TestShowOptions show;
    TestGlobalOptions global;
    const char *images[] = {"alpha.png", "beta.png"};
    int status = 0;

    TestShowOptions_init(&show);
    TestGlobalOptions_init(&global);

    status =
        expect_bool_option(name, show.verbose, false, OPT_PROVENANCE_DEFAULT);
    if (status != 0)
        goto cleanup;

    status = expect_int_option(name, show.count, 3, OPT_PROVENANCE_DEFAULT);
    if (status != 0)
        goto cleanup;

    status =
        expect_string_option(name, show.theme, "auto", OPT_PROVENANCE_DEFAULT);
    if (status != 0)
        goto cleanup;

    status =
        expect_range2_option(name, show.range, 1, 3, OPT_PROVENANCE_DEFAULT);
    if (status != 0)
        goto cleanup;

    status =
        expect_size2_option(name, show.size, 80, 24, OPT_PROVENANCE_DEFAULT);
    if (status != 0)
        goto cleanup;

    show.verbose = (OptBool){
        .value = true, .is_set = true, .provenance = OPT_PROVENANCE_CLI};
    show.cache = (OptBool){
        .value = false, .is_set = true, .provenance = OPT_PROVENANCE_CLI};
    if (!parse_range2_option(&show.range.value, "2:5", strlen("2:5"), NULL)) {
        status = fail_message(name, "failed to set test START:END option");
        goto cleanup;
    }
    show.range.is_set = true;
    show.range.provenance = OPT_PROVENANCE_CLI;
    if (!parse_size2_option(&show.size.value, "120x40", strlen("120x40"),
                            NULL)) {
        status = fail_message(name, "failed to set test NxM option");
        goto cleanup;
    }
    show.size.is_set = true;
    show.size.provenance = OPT_PROVENANCE_CLI;
    str_free(show.theme.value);
    show.theme = (OptString){
        .value = str_from_cstr("dark"),
        .is_set = true,
        .provenance = OPT_PROVENANCE_CLI,
    };
    arr_push(show.images.value, str_from_cstr(images[0]));
    arr_push(show.images.value, str_from_cstr(images[1]));
    show.images.is_set = true;
    show.images.provenance = OPT_PROVENANCE_CLI;

    if (!opt_merge_matching(&TestGlobalOptions_schema, &global,
                            &TestShowOptions_schema, &show)) {
        status = fail_message(name, "merge unexpectedly failed");
        goto cleanup;
    }

    status = expect_bool_option(name, global.verbose, true, OPT_PROVENANCE_CLI);
    if (status != 0)
        goto cleanup;

    status = expect_bool_option(name, global.cache, false, OPT_PROVENANCE_CLI);
    if (status != 0)
        goto cleanup;

    status = expect_int_option(name, global.count, 3, OPT_PROVENANCE_DEFAULT);
    if (status != 0)
        goto cleanup;

    status = expect_range2_option(name, global.range, 2, 5, OPT_PROVENANCE_CLI);
    if (status != 0)
        goto cleanup;

    status =
        expect_size2_option(name, global.size, 120, 40, OPT_PROVENANCE_CLI);
    if (status != 0)
        goto cleanup;

    status =
        expect_string_option(name, global.theme, "dark", OPT_PROVENANCE_CLI);
    if (status != 0)
        goto cleanup;

    status = expect_string_list_option(name, global.images, images,
                                       ARRAY_SIZE(images), OPT_PROVENANCE_CLI);

cleanup:
    TestGlobalOptions_deinit(&global);
    TestShowOptions_deinit(&show);
    return status;
}

// Low-level parse helpers should accept documented spellings, reject malformed
// input, parse byte-count suffixes, format named-enum diagnostics, and expose
// the "none" provenance name.
static int test_low_level_parse_helpers(TestContext *ctx) {
    static const OptNamedEnumOption named_enum_options[] = {
        {"alpha", 3},
        {"beta", 5},
        {"gamma", 7},
    };
    static const OptNamedEnumOption two_named_enum_options[] = {
        {"left", 11},
        {"right", 13},
    };
    static const OptNamedEnumOption invalid_named_enum_options[] = {
        {NULL, 17},
    };
    const char *name = ctx->test_name;
    String error = str_from_cstr("stale");
    String string_value = str_from_cstr("existing");
    StringArray string_list = arr_empty;
    bool bool_value = false;
    double double_value = 0.0;
    int int_value = 0;
    int64_t int64_value = 0;
    uint64_t uint64_value = 0;
    size_t byte_count = 0;
    int named_enum_value = 0;
    char huge_double[] = "1e5000";
    char huge_int[63];
    char too_long_double[80];
    char too_long_int[80];
    int status = 0;

    memset(huge_int, '9', sizeof(huge_int) - 1);
    huge_int[sizeof(huge_int) - 1] = '\0';
    memset(too_long_double, '7', sizeof(too_long_double));
    memset(too_long_int, '7', sizeof(too_long_int));

    if (strcmp(opt_provenance_name(OPT_PROVENANCE_NONE), "none") != 0)
        return fail_message(name, "unexpected provenance name for none");
    if (strcmp(opt_provenance_name((OptProvenance)99), "unknown") != 0)
        return fail_message(name,
                            "unexpected provenance name for invalid value");

    if (opt_parse_error(NULL, "ignored")) {
        status = fail_message(name, "opt_parse_error unexpectedly succeeded");
        goto cleanup;
    }

    if (opt_parse_int_span(NULL, 1, &int_value)) {
        status = fail_message(name, "NULL integer input unexpectedly parsed");
        goto cleanup;
    }
    if (opt_parse_int_span("", 0, &int_value)) {
        status = fail_message(name, "empty integer input unexpectedly parsed");
        goto cleanup;
    }
    if (opt_parse_int_span(too_long_int, sizeof(too_long_int), &int_value)) {
        status =
            fail_message(name, "oversized integer input unexpectedly parsed");
        goto cleanup;
    }
    if (opt_parse_int_span(huge_int, strlen(huge_int), &int_value)) {
        status = fail_message(name, "ERANGE integer input unexpectedly parsed");
        goto cleanup;
    }
    if (opt_parse_int_span("2147483648", strlen("2147483648"), &int_value)) {
        status = fail_message(name, "overflowing integer unexpectedly parsed");
        goto cleanup;
    }
    if (opt_parse_int_span("-2147483649", strlen("-2147483649"), &int_value)) {
        status = fail_message(name, "underflowing integer unexpectedly parsed");
        goto cleanup;
    }
    if (opt_parse_int64_span(NULL, 1, &int64_value)) {
        status = fail_message(name, "NULL int64 input unexpectedly parsed");
        goto cleanup;
    }
    if (opt_parse_int64_span("", 0, &int64_value)) {
        status = fail_message(name, "empty int64 input unexpectedly parsed");
        goto cleanup;
    }
    if (opt_parse_int64_span(too_long_int, sizeof(too_long_int),
                             &int64_value)) {
        status =
            fail_message(name, "oversized int64 input unexpectedly parsed");
        goto cleanup;
    }
    if (opt_parse_int64_span(huge_int, strlen(huge_int), &int64_value)) {
        status = fail_message(name, "ERANGE int64 input unexpectedly parsed");
        goto cleanup;
    }
    if (opt_parse_int64_span("9223372036854775808",
                             strlen("9223372036854775808"), &int64_value)) {
        status = fail_message(name, "overflowing int64 unexpectedly parsed");
        goto cleanup;
    }
    if (opt_parse_int64_span("-9223372036854775809",
                             strlen("-9223372036854775809"), &int64_value)) {
        status = fail_message(name, "underflowing int64 unexpectedly parsed");
        goto cleanup;
    }
    if (!opt_parse_int64_span("9223372036854775807",
                              strlen("9223372036854775807"), &int64_value) ||
        int64_value != INT64_MAX) {
        status = fail_message(name, "maximum int64 did not parse correctly");
        goto cleanup;
    }
    if (!opt_parse_int64_span("-9223372036854775808",
                              strlen("-9223372036854775808"), &int64_value) ||
        int64_value != INT64_MIN) {
        status = fail_message(name, "minimum int64 did not parse correctly");
        goto cleanup;
    }
    if (opt_parse_uint64_hex_or_decimal_span(NULL, 1, &uint64_value)) {
        status = fail_message(name, "NULL uint64 input unexpectedly parsed");
        goto cleanup;
    }
    if (opt_parse_uint64_hex_or_decimal_span("", 0, &uint64_value)) {
        status = fail_message(name, "empty uint64 input unexpectedly parsed");
        goto cleanup;
    }
    if (opt_parse_uint64_hex_or_decimal_span("-1", strlen("-1"),
                                             &uint64_value)) {
        status = fail_message(name, "negative uint64 unexpectedly parsed");
        goto cleanup;
    }
    if (opt_parse_uint64_hex_or_decimal_span("+1", strlen("+1"),
                                             &uint64_value)) {
        status = fail_message(name, "signed uint64 unexpectedly parsed");
        goto cleanup;
    }
    if (opt_parse_uint64_hex_or_decimal_span(too_long_int, sizeof(too_long_int),
                                             &uint64_value)) {
        status =
            fail_message(name, "oversized uint64 input unexpectedly parsed");
        goto cleanup;
    }
    if (opt_parse_uint64_hex_or_decimal_span(huge_int, strlen(huge_int),
                                             &uint64_value)) {
        status = fail_message(name, "ERANGE uint64 input unexpectedly parsed");
        goto cleanup;
    }
    if (opt_parse_uint64_hex_or_decimal_span("12x", strlen("12x"),
                                             &uint64_value)) {
        status = fail_message(name, "junk decimal uint64 unexpectedly parsed");
        goto cleanup;
    }
    if (opt_parse_uint64_hex_or_decimal_span("0z", strlen("0z"),
                                             &uint64_value)) {
        status =
            fail_message(name, "zero-prefixed junk uint64 unexpectedly parsed");
        goto cleanup;
    }
    if (opt_parse_uint64_hex_or_decimal_span("0x", strlen("0x"),
                                             &uint64_value)) {
        status = fail_message(name, "hex uint64 prefix without digits parsed");
        goto cleanup;
    }
    if (opt_parse_uint64_hex_or_decimal_span("0x12x", strlen("0x12x"),
                                             &uint64_value)) {
        status = fail_message(name, "junk hex uint64 unexpectedly parsed");
        goto cleanup;
    }
    if (opt_parse_uint64_hex_or_decimal_span("18446744073709551616",
                                             strlen("18446744073709551616"),
                                             &uint64_value)) {
        status = fail_message(name, "overflowing decimal uint64 parsed");
        goto cleanup;
    }
    if (opt_parse_uint64_hex_or_decimal_span("0x10000000000000000",
                                             strlen("0x10000000000000000"),
                                             &uint64_value)) {
        status = fail_message(name, "overflowing hex uint64 parsed");
        goto cleanup;
    }
    if (!opt_parse_uint64_hex_or_decimal_span("123", strlen("123"),
                                              &uint64_value) ||
        uint64_value != 123) {
        status = fail_message(name, "decimal uint64 did not parse correctly");
        goto cleanup;
    }
    if (!opt_parse_uint64_hex_or_decimal_span(
            "4294967295", strlen("4294967295"), &uint64_value) ||
        uint64_value != UINT32_MAX) {
        status =
            fail_message(name, "maximum uint32-sized decimal did not parse");
        goto cleanup;
    }
    if (!opt_parse_uint64_hex_or_decimal_span(
            "0xffffffff", strlen("0xffffffff"), &uint64_value) ||
        uint64_value != UINT32_MAX) {
        status = fail_message(name, "maximum uint32-sized hex did not parse");
        goto cleanup;
    }
    if (!opt_parse_uint64_hex_or_decimal_span(
            "4294967296", strlen("4294967296"), &uint64_value) ||
        uint64_value != (uint64_t)UINT32_MAX + 1) {
        status = fail_message(name, "large decimal uint64 did not parse");
        goto cleanup;
    }
    if (!opt_parse_uint64_hex_or_decimal_span(
            "0x100000000", strlen("0x100000000"), &uint64_value) ||
        uint64_value != (uint64_t)UINT32_MAX + 1) {
        status = fail_message(name, "large hex uint64 did not parse");
        goto cleanup;
    }
    if (!opt_parse_uint64_hex_or_decimal_span("18446744073709551615",
                                              strlen("18446744073709551615"),
                                              &uint64_value) ||
        uint64_value != UINT64_MAX) {
        status = fail_message(name,
                              "maximum decimal uint64 did not parse correctly");
        goto cleanup;
    }
    if (!opt_parse_uint64_hex_or_decimal_span("0xffffffffffffffff",
                                              strlen("0xffffffffffffffff"),
                                              &uint64_value) ||
        uint64_value != UINT64_MAX) {
        status =
            fail_message(name, "maximum hex uint64 did not parse correctly");
        goto cleanup;
    }
    if (!opt_parse_uint64_hex_or_decimal_span("0X123", strlen("0X123"),
                                              &uint64_value) ||
        uint64_value != 0x123) {
        status =
            fail_message(name, "uppercase-prefix hex uint64 did not parse");
        goto cleanup;
    }
    if (opt_parse_double_span(NULL, 1, &double_value)) {
        status = fail_message(name, "NULL double input unexpectedly parsed");
        goto cleanup;
    }
    if (opt_parse_double_span("", 0, &double_value)) {
        status = fail_message(name, "empty double input unexpectedly parsed");
        goto cleanup;
    }
    if (opt_parse_double_span(too_long_double, sizeof(too_long_double),
                              &double_value)) {
        status =
            fail_message(name, "oversized double input unexpectedly parsed");
        goto cleanup;
    }
    if (opt_parse_double_span(huge_double, strlen(huge_double),
                              &double_value)) {
        status = fail_message(name, "ERANGE double input unexpectedly parsed");
        goto cleanup;
    }
    if (!opt_parse_double_span("nan", strlen("nan"), &double_value) ||
        !isnan(double_value)) {
        status = fail_message(name, "failed to parse NaN");
        goto cleanup;
    }
    if (!opt_parse_double_span("inf", strlen("inf"), &double_value) ||
        !isinf(double_value) || signbit(double_value)) {
        status = fail_message(name, "failed to parse positive infinity");
        goto cleanup;
    }
    if (!opt_parse_double_span("-inf", strlen("-inf"), &double_value) ||
        !isinf(double_value) || !signbit(double_value)) {
        status = fail_message(name, "failed to parse negative infinity");
        goto cleanup;
    }
    if (!opt_parse_double_span("1.25", strlen("1.25"), &double_value) ||
        double_value != 1.25) {
        status = fail_message(name, "failed to parse finite double");
        goto cleanup;
    }

    if (!opt_parse_bool_option(&bool_value, "yes", strlen("yes"), &error) ||
        !bool_value) {
        status = fail_message(name, "failed to parse yes as true");
        goto cleanup;
    }
    if (!opt_parse_bool_option(&bool_value, "on", strlen("on"), &error) ||
        !bool_value) {
        status = fail_message(name, "failed to parse on as true");
        goto cleanup;
    }
    if (!opt_parse_bool_option(&bool_value, "no", strlen("no"), &error) ||
        bool_value) {
        status = fail_message(name, "failed to parse no as false");
        goto cleanup;
    }
    if (opt_parse_bool_option(&bool_value, NULL, 0, &error)) {
        status = fail_message(name, "missing bool value unexpectedly parsed");
        goto cleanup;
    }
    if (!opt_parse_int_option(&int_value, "17", strlen("17"), &error) ||
        !opt_validate_positive_int(&int_value, &error) || int_value != 17) {
        status = fail_message(name, "failed to parse positive integer");
        goto cleanup;
    }
    if (opt_parse_byte_count_span(NULL, 1, &byte_count)) {
        status = fail_message(name, "NULL byte count unexpectedly parsed");
        goto cleanup;
    }
    if (opt_parse_byte_count_span("", 0, &byte_count)) {
        status = fail_message(name, "empty byte count unexpectedly parsed");
        goto cleanup;
    }
    if (opt_parse_byte_count_span("K", strlen("K"), &byte_count)) {
        status =
            fail_message(name, "suffix-only byte count unexpectedly parsed");
        goto cleanup;
    }
    if (opt_parse_byte_count_span("/1", strlen("/1"), &byte_count)) {
        status =
            fail_message(name, "punctuated byte count unexpectedly parsed");
        goto cleanup;
    }
    if (opt_parse_byte_count_span(
            "999999999999999999999999999999999999999999999",
            strlen("999999999999999999999999999999999999999999999"),
            &byte_count)) {
        status =
            fail_message(name, "overflowing byte count unexpectedly parsed");
        goto cleanup;
    }
    char scaled_byte_count[64];
    int scaled_byte_count_len =
        snprintf(scaled_byte_count, sizeof(scaled_byte_count), "%zuK",
                 SIZE_MAX / 1024 + 1);
    if (scaled_byte_count_len < 0 ||
        (size_t)scaled_byte_count_len >= sizeof(scaled_byte_count)) {
        status = fail_message(name, "failed to format scaled byte count");
        goto cleanup;
    }
    if (opt_parse_byte_count_span(scaled_byte_count,
                                  (size_t)scaled_byte_count_len, &byte_count)) {
        status = fail_message(
            name, "overflowing scaled byte count unexpectedly parsed");
        goto cleanup;
    }
    if (!opt_parse_byte_count_span("17", strlen("17"), &byte_count) ||
        byte_count != 17) {
        status = fail_message(name, "failed to span-parse decimal byte count");
        goto cleanup;
    }
    if (!opt_parse_byte_count_option(&byte_count, "16K", strlen("16K"),
                                     &error) ||
        byte_count != 16 * 1024) {
        status = fail_message(name, "failed to parse kibibyte byte count");
        goto cleanup;
    }
    if (!opt_parse_byte_count_option(&byte_count, "2M", strlen("2M"), &error) ||
        byte_count != 2 * 1024 * 1024) {
        status = fail_message(name, "failed to parse mebibyte byte count");
        goto cleanup;
    }
    if (!opt_parse_byte_count_option(&byte_count, "1G", strlen("1G"), &error) ||
        byte_count != (size_t)1024 * 1024 * 1024) {
        status = fail_message(name, "failed to parse gibibyte byte count");
        goto cleanup;
    }
    if (opt_parse_byte_count_option(&byte_count, "2m", strlen("2m"), &error)) {
        status =
            fail_message(name, "lowercase byte suffix unexpectedly parsed");
        goto cleanup;
    }
    if (opt_parse_byte_count_option(&byte_count, "0K", strlen("0K"), &error)) {
        status = fail_message(name, "zero byte count unexpectedly parsed");
        goto cleanup;
    }
    if (byte_count != (size_t)1024 * 1024 * 1024) {
        status = fail_message(name, "failed byte-count parse changed output");
        goto cleanup;
    }
    if (strcmp(error.cstr, "expected a positive byte count optionally followed "
                           "by K, M, or G") != 0) {
        status = fail_message(name, "unexpected byte-count parse error");
        goto cleanup;
    }
    if (opt_parse_byte_count_span("1k", strlen("1k"), &byte_count)) {
        status =
            fail_message(name, "lowercase span suffix unexpectedly parsed");
        goto cleanup;
    }
    if (!opt_parse_int_option(&int_value, "0", strlen("0"), &error)) {
        status = fail_message(name, "failed to parse zero as an integer");
        goto cleanup;
    }
    if (opt_validate_positive_int(&int_value, &error)) {
        status = fail_message(name, "non-positive integer unexpectedly parsed");
        goto cleanup;
    }
    if (strcmp(error.cstr, "must be positive") != 0) {
        status = fail_message(name, "unexpected positive-integer parse error");
        goto cleanup;
    }
    if (!opt_parse_double_option(&double_value, "2.5", strlen("2.5"), &error) ||
        double_value != 2.5) {
        status = fail_message(name, "failed to parse generic double option");
        goto cleanup;
    }
    if (opt_parse_double_option(&double_value, "bogus", strlen("bogus"),
                                &error)) {
        status = fail_message(name, "invalid double unexpectedly parsed");
        goto cleanup;
    }
    if (strcmp(error.cstr, "expected a number") != 0) {
        status = fail_message(name, "unexpected generic double parse error");
        goto cleanup;
    }
    if (!opt_parse_double_option(&double_value, "0", strlen("0"), &error) ||
        !opt_validate_non_negative_double(&double_value, &error) ||
        double_value != 0.0) {
        status =
            fail_message(name, "failed to parse non-negative double option");
        goto cleanup;
    }
    if (!opt_parse_double_option(&double_value, "-0.5", strlen("-0.5"),
                                 &error)) {
        status = fail_message(name, "failed to parse negative double");
        goto cleanup;
    }
    if (opt_validate_non_negative_double(&double_value, &error)) {
        status = fail_message(
            name, "negative double unexpectedly parsed as non-negative");
        goto cleanup;
    }
    if (strcmp(error.cstr, "must be non-negative") != 0) {
        status =
            fail_message(name, "unexpected non-negative double range error");
        goto cleanup;
    }
    if (!opt_parse_double_option(&double_value, "inf", strlen("inf"), &error)) {
        status = fail_message(name, "failed to parse infinity as a double");
        goto cleanup;
    }
    if (opt_validate_non_negative_double(&double_value, &error)) {
        status = fail_message(
            name, "infinite double unexpectedly parsed as non-negative");
        goto cleanup;
    }
    if (strcmp(error.cstr, "must be finite") != 0) {
        status = fail_message(name, "unexpected finite-double error");
        goto cleanup;
    }
    if (!opt_parse_double_option(&double_value, "nan", strlen("nan"), &error)) {
        status = fail_message(name, "failed to parse NaN as a double");
        goto cleanup;
    }
    if (opt_validate_non_negative_double(&double_value, &error)) {
        status = fail_message(name, "NaN unexpectedly parsed as non-negative");
        goto cleanup;
    }
    if (strcmp(error.cstr, "must be finite") != 0) {
        status = fail_message(name, "unexpected NaN finite-double error");
        goto cleanup;
    }
    if (!opt_parse_double_option(&double_value, "0.5", strlen("0.5"), &error) ||
        !opt_validate_probability(&double_value, &error) ||
        double_value != 0.5) {
        status = fail_message(name, "failed to parse probability option");
        goto cleanup;
    }
    if (!opt_parse_double_option(&double_value, "1.5", strlen("1.5"), &error)) {
        status = fail_message(name, "failed to parse out-of-range probability");
        goto cleanup;
    }
    if (opt_validate_probability(&double_value, &error)) {
        status =
            fail_message(name, "out-of-range probability unexpectedly parsed");
        goto cleanup;
    }
    if (strcmp(error.cstr, "expected a number in the range 0-1") != 0) {
        status = fail_message(name, "unexpected probability range error");
        goto cleanup;
    }
    if (!opt_parse_double_option(&double_value, "inf", strlen("inf"), &error)) {
        status = fail_message(
            name, "failed to parse infinite probability as a double");
        goto cleanup;
    }
    if (opt_validate_probability(&double_value, &error)) {
        status = fail_message(name, "infinite probability unexpectedly parsed");
        goto cleanup;
    }
    if (strcmp(error.cstr, "must be finite") != 0) {
        status = fail_message(name, "unexpected infinite probability error");
        goto cleanup;
    }
    if (!opt_parse_double_option(&double_value, "nan", strlen("nan"), &error)) {
        status = fail_message(name, "failed to parse NaN probability");
        goto cleanup;
    }
    if (opt_validate_probability(&double_value, &error)) {
        status = fail_message(name, "NaN probability unexpectedly parsed");
        goto cleanup;
    }
    if (strcmp(error.cstr, "must be finite") != 0) {
        status = fail_message(name, "unexpected NaN probability error");
        goto cleanup;
    }

    if (!opt_parse_named_enum_option(
            named_enum_options, ARRAY_SIZE(named_enum_options), "beta",
            strlen("beta"), &named_enum_value, &error) ||
        named_enum_value != 5) {
        status = fail_message(name, "failed to parse named enum value");
        goto cleanup;
    }
    if (opt_parse_named_enum_option(NULL, ARRAY_SIZE(named_enum_options),
                                    "alpha", strlen("alpha"), &named_enum_value,
                                    &error)) {
        status =
            fail_message(name, "NULL named-enum table unexpectedly parsed");
        goto cleanup;
    }
    if (opt_parse_named_enum_option(named_enum_options, 0, "alpha",
                                    strlen("alpha"), &named_enum_value,
                                    &error)) {
        status =
            fail_message(name, "empty named-enum table unexpectedly parsed");
        goto cleanup;
    }
    if (opt_parse_named_enum_option(named_enum_options,
                                    ARRAY_SIZE(named_enum_options), "alpha",
                                    strlen("alpha"), NULL, &error)) {
        status =
            fail_message(name, "NULL named-enum output unexpectedly parsed");
        goto cleanup;
    }
    if (opt_parse_named_enum_option(named_enum_options,
                                    ARRAY_SIZE(named_enum_options), NULL, 0,
                                    &named_enum_value, &error)) {
        status =
            fail_message(name, "NULL named-enum value unexpectedly parsed");
        goto cleanup;
    }
    if (opt_parse_named_enum_option(
            invalid_named_enum_options, ARRAY_SIZE(invalid_named_enum_options),
            "alpha", strlen("alpha"), &named_enum_value, &error)) {
        status =
            fail_message(name, "invalid named-enum table unexpectedly parsed");
        goto cleanup;
    }
    if (opt_parse_named_enum_option(
            named_enum_options, ARRAY_SIZE(named_enum_options), "other",
            strlen("other"), &named_enum_value, &error)) {
        status = fail_message(name, "unknown named enum value parsed");
        goto cleanup;
    }
    if (named_enum_value != 5) {
        status = fail_message(name, "failed named enum parse changed output");
        goto cleanup;
    }
    if (strcmp(error.cstr, "expected one of alpha, beta, or gamma") != 0) {
        status = fail_message(name, "unexpected named enum parse error");
        goto cleanup;
    }
    if (opt_parse_named_enum_option(
            two_named_enum_options, ARRAY_SIZE(two_named_enum_options), "other",
            strlen("other"), &named_enum_value, &error)) {
        status = fail_message(name, "unknown two-value enum parsed");
        goto cleanup;
    }
    if (strcmp(error.cstr, "expected one of left or right") != 0) {
        status = fail_message(name, "unexpected two-value enum parse error");
        goto cleanup;
    }

    if (opt_parse_string_option(&string_value, NULL, 0, &error)) {
        status = fail_message(name, "missing string value unexpectedly parsed");
        goto cleanup;
    }
    if (opt_parse_string_list_option(&string_list, NULL, 0, &error)) {
        status =
            fail_message(name, "missing string-list value unexpectedly parsed");
        goto cleanup;
    }

cleanup:
    str_array_free(&string_list);
    str_free(string_value);
    str_free(error);
    return status;
}

// Field lookup and merge should tolerate missing destination fields, reject
// same-name type mismatches, and refuse assignments when no parser exists.
static int test_merge_lookup_and_no_parse_paths(TestContext *ctx) {
    const char *name = ctx->test_name;
    TestShowOptions show;
    TestStatusOptions status_options;
    TestMismatchOptions mismatch;
    TestNoParseOptions no_parse;
    const OptFieldSpec *field = NULL;
    String error = str_empty;
    int status = 0;

    TestShowOptions_init(&show);
    TestStatusOptions_init(&status_options);
    TestMismatchOptions_init(&mismatch);
    TestNoParseOptions_init(&no_parse);

    field = opt_find_field_by_name(&TestShowOptions_schema, "verbose");
    if (field == NULL) {
        status = fail_message(name, "failed to find existing field");
        goto cleanup;
    }
    if (opt_find_field_by_name(&TestShowOptions_schema, "missing") != NULL) {
        status = fail_message(name, "unexpectedly found a missing field");
        goto cleanup;
    }

    if (!opt_merge_matching(&TestStatusOptions_schema, &status_options,
                            &TestShowOptions_schema, &show)) {
        status =
            fail_message(name, "merge with missing destination fields failed");
        goto cleanup;
    }

    status = expect_int_option(name, status_options.count, 3,
                               OPT_PROVENANCE_DEFAULT);
    if (status != 0)
        goto cleanup;

    if (opt_merge_matching(&TestMismatchOptions_schema, &mismatch,
                           &TestShowOptions_schema, &show)) {
        status =
            fail_message(name, "merge unexpectedly accepted a type mismatch");
        goto cleanup;
    }

    field = opt_find_field_by_name(&TestNoParseOptions_schema, "raw");
    if (field == NULL) {
        status = fail_message(name, "failed to find parser-less field");
        goto cleanup;
    }

    if (opt_assign_field_value(field, &no_parse, "7", strlen("7"),
                               OPT_PROVENANCE_CLI, &error)) {
        status = fail_message(
            name, "assignment unexpectedly succeeded without a parser");
        goto cleanup;
    }
    if (no_parse.raw.is_set) {
        status = fail_message(name, "parser-less assignment mutated the field");
        goto cleanup;
    }

cleanup:
    str_free(error);
    TestNoParseOptions_deinit(&no_parse);
    TestMismatchOptions_deinit(&mismatch);
    TestStatusOptions_deinit(&status_options);
    TestShowOptions_deinit(&show);
    return status;
}

int main(int argc, char **argv) {
    const Subtest subtests[] = {
        PREFIXED_TEST(test_defaults_and_merge),
        PREFIXED_TEST(test_low_level_parse_helpers),
        PREFIXED_TEST(test_merge_lookup_and_no_parse_paths),
    };

    return run_subtests(argc, argv, subtests, ARRAY_SIZE(subtests));
}
