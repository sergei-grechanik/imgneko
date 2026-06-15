// SPDX-License-Identifier: MIT-0

#include <stdio.h>
#include <string.h>

#include "cli/placeholder_bg.h"
#include "test_main.h"
#include "util/common.h"

// Print a subtest failure message and return a failing status code.
static int fail_message(const char *subtest, const char *message) {
    fprintf(stderr, "%s: %s\n", subtest, message);
    return 1;
}

// Parse a background expression or report the parser diagnostic.
static int parse_bg_or_fail(const char *subtest, const char *text,
                            PlaceholderBg *bg) {
    String error = str_empty;

    if (placeholder_bg_parse_option(bg, text, strlen(text), &error)) {
        str_free(error);
        return 0;
    }

    fprintf(stderr, "%s: parse failed: %s\n", subtest, error.cstr);
    str_free(error);
    return 1;
}

// Verify that direct parser calls can omit the optional diagnostic sink.
static int test_parse_without_error_sink(TestContext *ctx) {
    PlaceholderBg bg = {0};

    if (placeholder_bg_parse_option(&bg, "", 0, NULL))
        return fail_message(ctx->test_name, "empty background parsed");
    if (placeholder_bg_parse_option(&bg, "!", 1, NULL))
        return fail_message(ctx->test_name, "malformed background parsed");

    placeholder_bg_clear_option(&bg);
    return 0;
}

// Empty background values should borrow a no-op placeholder format.
static int test_empty_background_format(TestContext *ctx) {
    PlaceholderBg bg = {0};
    PlaceholderFormat format = placeholder_bg_to_format(&bg);

    if (format.func != NULL)
        return fail_message(ctx->test_name, "empty background has a formatter");

    placeholder_bg_clear_option(&bg);
    return 0;
}

// Copied backgrounds must own their recursive format data independently.
static int test_copy_recursive_background(TestContext *ctx) {
    const char *subtest = ctx->test_name;
    PlaceholderBg src = {0};
    PlaceholderBg dst = {0};
    PlaceholderBg empty = {0};
    Placeholder placeholder = {0};
    char out[32];
    int status = 0;

    status = parse_bg_or_fail(subtest, "hstripes(vstripes(1, 2), 3)", &src);
    if (status != 0)
        goto cleanup;

    placeholder_bg_copy_option(&dst, &src);
    placeholder_bg_clear_option(&src);

    PlaceholderFormat format = placeholder_bg_to_format(&dst);
    if (format.func == NULL || !format.per_cell) {
        status = fail_message(subtest, "copied format shape is wrong");
        goto cleanup;
    }

    const char *expected = "\033[48;5;2m";
    int len = format.func(format.ctx, &placeholder, /*col=*/1, /*row=*/0, out,
                          sizeof(out));
    if (len != (int)strlen(expected) ||
        memcmp(out, expected, strlen(expected)) != 0) {
        status =
            fail_message(subtest, "copied nested format emitted wrong SGR");
        goto cleanup;
    }

    len = format.func(format.ctx, &placeholder, /*col=*/0, /*row=*/1, out,
                      /*out_cap=*/1);
    if (len != (int)strlen("\033[48;5;3m")) {
        status = fail_message(subtest, "small output buffer length is wrong");
        goto cleanup;
    }

    placeholder_bg_copy_option(&dst, &empty);
    if (placeholder_bg_to_format(&dst).func != NULL)
        status = fail_message(subtest, "empty copy did not clear destination");

cleanup:
    placeholder_bg_clear_option(&empty);
    placeholder_bg_clear_option(&dst);
    placeholder_bg_clear_option(&src);
    return status;
}

// Register and run every placeholder background subtest.
int main(int argc, char **argv) {
    const Subtest subtests[] = {
        PREFIXED_TEST(test_parse_without_error_sink),
        PREFIXED_TEST(test_empty_background_format),
        PREFIXED_TEST(test_copy_recursive_background),
    };

    return run_subtests(argc, argv, subtests, ARRAY_SIZE(subtests));
}
