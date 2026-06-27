// SPDX-License-Identifier: MIT-0

// Enable POSIX APIs used in this file (mkstemp).
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cli/placeholder_bg.h"
#include "cli/placeholder_bg_file.h"
#include "test_main.h"
#include "util/common.h"
#include "util/path.h"

// Print a subtest failure message and return a failing status code.
static int fail_message(const char *subtest, const char *message) {
    fprintf(stderr, "%s: %s\n", subtest, message);
    return 1;
}

// Create a temporary file inside the per-test output directory containing
// `contents`. The caller owns the returned path and frees it with str_free
// after unlinking it.
static String write_temp_file(TestContext *ctx, const char *name,
                              const char *contents) {
    const char *output_dir = test_get_output_dir(ctx);
    String path = str_empty;
    int fd = -1;
    size_t len = strlen(contents);
    size_t written = 0;

    if (output_dir == NULL)
        return path;

    path = path_join(output_dir, name);
    str_append_cstr(path, ".XXXXXX");
    fd = mkstemp(path.cstr);
    if (fd < 0) {
        fprintf(stderr, "%s: mkstemp failed: %s\n", ctx->test_name,
                strerror(errno));
        str_free(path);
        return path;
    }

    while (written < len) {
        ssize_t nwrite = write(fd, contents + written, len - written);

        if (nwrite <= 0) {
            fprintf(stderr, "%s: write failed: %s\n", ctx->test_name,
                    strerror(errno));
            close(fd);
            unlink(path.cstr);
            str_free(path);
            return path;
        }

        written += (size_t)nwrite;
    }

    close(fd);
    return path;
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

typedef struct FormatCase {
    uint32_t col;
    uint32_t row;
    const char *expected;
} FormatCase;

// Render a parsed background format at a specific cell and compare its bytes.
static int expect_format_case(const char *subtest, PlaceholderFormat format,
                              const FormatCase *test_case) {
    Placeholder placeholder = {0};
    char out[64];
    size_t expected_len = strlen(test_case->expected);

    if (format.func == NULL)
        return fail_message(subtest, "background has no formatter");

    int len = format.func(format.ctx, &placeholder, test_case->col,
                          test_case->row, out, sizeof(out));
    if (len != (int)expected_len ||
        memcmp(out, test_case->expected, expected_len) != 0) {
        fprintf(stderr, "%s: background at (%u, %u) emitted wrong bytes\n",
                subtest, test_case->row, test_case->col);
        return 1;
    }

    return 0;
}

// Render a parsed background format and compare its exact emitted bytes.
static int expect_format_bytes(const char *subtest, PlaceholderFormat format,
                               const char *expected) {
    Placeholder placeholder = {0};
    char out[64];
    size_t expected_len = strlen(expected);

    if (format.func == NULL)
        return fail_message(subtest, "background has no formatter");

    int len = format.func(format.ctx, &placeholder, /*col=*/0, /*row=*/0, out,
                          sizeof(out));
    if (len != (int)expected_len || memcmp(out, expected, expected_len) != 0)
        return fail_message(subtest, "background emitted wrong bytes");

    return 0;
}

// Verify that direct parser calls can omit the optional diagnostic sink.
static int test_parse_without_error_sink(TestContext *ctx) {
    PlaceholderBg bg = {0};

    if (placeholder_bg_parse_option(&bg, "", 0, NULL))
        return fail_message(ctx->test_name, "empty background parsed");
    if (placeholder_bg_parse_option(&bg, "!", 1, NULL))
        return fail_message(ctx->test_name, "malformed background parsed");
    if (placeholder_bg_parse_option_raw(&bg, NULL, 0, NULL))
        return fail_message(ctx->test_name, "missing raw background parsed");
    if (placeholder_bg_parse_option_file(&bg, NULL, 0, NULL))
        return fail_message(ctx->test_name, "missing file background parsed");

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

// String-literal backgrounds are expression strings, while raw strings keep the
// exact bytes passed by the CLI option parser.
static int test_string_backgrounds(TestContext *ctx) {
    const char *subtest = ctx->test_name;
    PlaceholderBg bg = {0};
    int status = 0;

    status = parse_bg_or_fail(subtest, "\"\\x1b[48;5;42m\"", &bg);
    if (status != 0)
        goto cleanup;

    PlaceholderFormat format = placeholder_bg_to_format(&bg);
    status = expect_format_bytes(subtest, format, "\033[48;5;42m");
    if (status != 0)
        goto cleanup;

    const char *raw = "\\x1b[48;5;42m";
    if (!placeholder_bg_parse_option_raw(&bg, raw, strlen(raw), NULL)) {
        status = fail_message(subtest, "raw background did not parse");
        goto cleanup;
    }

    format = placeholder_bg_to_format(&bg);
    status = expect_format_bytes(subtest, format, raw);

cleanup:
    placeholder_bg_clear_option(&bg);
    return status;
}

// File-backed backgrounds should load raw formatting bytes per cell, repeat
// rows, copy empty cells from the left, and leak the final cell to the right.
static int test_file_backgrounds(TestContext *ctx) {
    const char *subtest = ctx->test_name;
    PlaceholderBg bg = {0};
    PlaceholderBg copy = {0};
    PlaceholderBg expr_bg = {0};
    Placeholder placeholder = {0};
    String path = str_empty;
    String expr = str_empty;
    char out[8];
    int status = 0;

    path = write_temp_file(ctx, "bg-grid", "\n R\nR  B\nG Y \r\nZ");
    if (path.len == 0) {
        status = 1;
        goto cleanup;
    }

    if (!placeholder_bg_parse_option_file(&bg, path.cstr, path.len, NULL)) {
        status = fail_message(subtest, "raw file background did not parse");
        goto cleanup;
    }

    PlaceholderFormat format = placeholder_bg_to_format(&bg);
    if (!format.per_cell) {
        status = fail_message(subtest, "file background is not per-cell");
        goto cleanup;
    }

    const FormatCase cases[] = {
        {.col = 0, .row = 0, .expected = ""},
        {.col = 9, .row = 0, .expected = ""},
        {.col = 0, .row = 1, .expected = ""},
        {.col = 1, .row = 1, .expected = "R"},
        {.col = 9, .row = 1, .expected = "R"},
        {.col = 0, .row = 2, .expected = "R"},
        {.col = 1, .row = 2, .expected = "R"},
        {.col = 2, .row = 2, .expected = "B"},
        {.col = 9, .row = 2, .expected = "B"},
        {.col = 0, .row = 3, .expected = "G"},
        {.col = 1, .row = 3, .expected = "Y"},
        {.col = 2, .row = 3, .expected = "Y"},
        {.col = 0, .row = 4, .expected = "Z"},
        {.col = 9, .row = 4, .expected = "Z"},
        {.col = 0, .row = 5, .expected = ""},
        {.col = 9, .row = 5, .expected = ""},
        {.col = 1, .row = 6, .expected = "R"},
    };
    for (size_t i = 0; i < ARRAY_SIZE(cases); ++i) {
        status = expect_format_case(subtest, format, &cases[i]);
        if (status != 0)
            goto cleanup;
    }

    int len = format.func(format.ctx, &placeholder, /*col=*/2, /*row=*/2, out,
                          /*out_cap=*/0);
    if (len != 1) {
        status = fail_message(subtest, "small file-format buffer length");
        goto cleanup;
    }

    placeholder_bg_copy_option(&copy, &bg);
    placeholder_bg_clear_option(&bg);

    format = placeholder_bg_to_format(&copy);
    const FormatCase copy_case = {.col = 2, .row = 7, .expected = "B"};
    status = expect_format_case(subtest, format, &copy_case);
    if (status != 0)
        goto cleanup;

    expr = str_from_cstr("file(\"");
    str_append_cstr(expr, path.cstr);
    str_append_cstr(expr, "\")");
    status = parse_bg_or_fail(subtest, expr.cstr, &expr_bg);
    if (status != 0)
        goto cleanup;

    format = placeholder_bg_to_format(&expr_bg);
    const FormatCase expr_case = {.col = 1, .row = 3, .expected = "Y"};
    status = expect_format_case(subtest, format, &expr_case);

cleanup:
    placeholder_bg_clear_option(&expr_bg);
    placeholder_bg_clear_option(&copy);
    placeholder_bg_clear_option(&bg);
    if (path.len != 0)
        unlink(path.cstr);
    str_free(expr);
    str_free(path);
    return status;
}

// File-backed backgrounds with one sequence per line are row formats. This
// lets the placeholder writer emit the sequence once per row before the
// automatic ID colors.
static int test_file_background_row_format(TestContext *ctx) {
    const char *subtest = ctx->test_name;
    PlaceholderBg bg = {0};
    String path = str_empty;
    int status = 0;

    path = write_temp_file(ctx, "bg-rows", "R\nG\n");
    if (path.len == 0) {
        status = 1;
        goto cleanup;
    }

    if (!placeholder_bg_parse_option_file(&bg, path.cstr, path.len, NULL)) {
        status = fail_message(subtest, "row file background did not parse");
        goto cleanup;
    }

    PlaceholderFormat format = placeholder_bg_to_format(&bg);
    if (format.per_cell) {
        status = fail_message(subtest, "row file background is per-cell");
        goto cleanup;
    }

    const FormatCase cases[] = {
        {.col = 0, .row = 0, .expected = "R"},
        {.col = 9, .row = 0, .expected = "R"},
        {.col = 0, .row = 1, .expected = "G"},
        {.col = 9, .row = 1, .expected = "G"},
        {.col = 0, .row = 2, .expected = "R"},
    };
    for (size_t i = 0; i < ARRAY_SIZE(cases); ++i) {
        status = expect_format_case(subtest, format, &cases[i]);
        if (status != 0)
            goto cleanup;
    }

cleanup:
    placeholder_bg_clear_option(&bg);
    if (path.len != 0)
        unlink(path.cstr);
    str_free(path);
    return status;
}

// File-backed background loading should report missing and empty input files.
static int test_file_background_errors(TestContext *ctx) {
    const char *subtest = ctx->test_name;
    PlaceholderBg bg = {0};
    Placeholder placeholder = {0};
    String empty_path = str_empty;
    String missing_path = str_empty;
    String error = str_empty;
    char out[1];
    int status = 0;

    if (placeholder_bg_file_copy(NULL) != NULL) {
        status = fail_message(subtest, "copying a NULL file context worked");
        goto cleanup;
    }

    PlaceholderFormat null_format = placeholder_bg_file_format(NULL);
    int len = null_format.func(null_format.ctx, &placeholder, /*col=*/0,
                               /*row=*/0, out, sizeof(out));
    if (len != 0) {
        status = fail_message(subtest, "NULL file format emitted bytes");
        goto cleanup;
    }

    empty_path = write_temp_file(ctx, "empty-bg", "");
    if (empty_path.len == 0) {
        status = 1;
        goto cleanup;
    }

    if (placeholder_bg_parse_option_file(&bg, empty_path.cstr, empty_path.len,
                                         &error)) {
        status = fail_message(subtest, "empty background file parsed");
        goto cleanup;
    }
    if (strcmp(error.cstr, "background file is empty") != 0) {
        status = fail_message(subtest, "empty file error is wrong");
        goto cleanup;
    }

    str_free(error);
    const char *output_dir = test_get_output_dir(ctx);
    if (output_dir == NULL) {
        status = 1;
        goto cleanup;
    }
    missing_path = path_join(output_dir, "missing-bg");
    unlink(missing_path.cstr);
    if (placeholder_bg_parse_option_file(&bg, missing_path.cstr,
                                         missing_path.len, NULL)) {
        status =
            fail_message(subtest, "missing file parsed without error sink");
        goto cleanup;
    }
    if (placeholder_bg_parse_option_file(&bg, missing_path.cstr,
                                         missing_path.len, &error)) {
        status = fail_message(subtest, "missing background file parsed");
        goto cleanup;
    }
    if (strstr(error.cstr, "failed to read background file") == NULL)
        status = fail_message(subtest, "missing file error is wrong");

cleanup:
    placeholder_bg_clear_option(&bg);
    if (empty_path.len != 0)
        unlink(empty_path.cstr);
    str_free(error);
    str_free(missing_path);
    str_free(empty_path);
    return status;
}

// Register and run every placeholder background subtest.
int main(int argc, char **argv) {
    const Subtest subtests[] = {
        PREFIXED_TEST(test_parse_without_error_sink),
        PREFIXED_TEST(test_empty_background_format),
        PREFIXED_TEST(test_copy_recursive_background),
        PREFIXED_TEST(test_string_backgrounds),
        PREFIXED_TEST(test_file_backgrounds),
        PREFIXED_TEST(test_file_background_row_format),
        PREFIXED_TEST(test_file_background_errors),
    };

    return run_subtests(argc, argv, subtests, ARRAY_SIZE(subtests));
}
