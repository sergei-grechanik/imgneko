// SPDX-License-Identifier: MIT-0

// Shared assertions for graphics-command unit tests.

#include <stdio.h>
#include <string.h>

#include "test_graphics_command.h"
#include "test_main.h"
#include "util/string.h"

// Return the comma-delimited token at index, or an empty span when absent.
static StrSpan token_at(StrSpan tokens, size_t index) {
    size_t token_start = 0;
    size_t token_index = 0;

    for (size_t i = 0; i <= tokens.len; ++i) {
        if (i != tokens.len && tokens.data[i] != ',')
            continue;

        if (token_index == index)
            return str_span(tokens.data + token_start, i - token_start);

        token_start = i + 1;
        ++token_index;
    }

    return str_span_empty;
}

// Count tokens while rejecting leading, trailing, or adjacent commas.
static int count_tokens(StrSpan tokens, size_t *count_out) {
    *count_out = 0;
    if (tokens.len == 0)
        return 0;

    size_t token_start = 0;
    for (size_t i = 0; i <= tokens.len; ++i) {
        if (i != tokens.len && tokens.data[i] != ',')
            continue;
        if (i == token_start)
            return -1;

        ++*count_out;
        token_start = i + 1;
    }

    return 0;
}

// Compare headers as token sets because serialization order is intentionally
// not part of the public contract.
int test_expect_graphics_command_header_tokens(TestContext *ctx, StrSpan actual,
                                               StrSpan expected,
                                               const char *description) {
    size_t actual_count = 0;
    size_t expected_count = 0;

    if (count_tokens(actual, &actual_count) != 0) {
        fprintf(stderr, "%s: %s: actual header has an empty token\n",
                ctx->test_name, description);
        return 1;
    }
    if (count_tokens(expected, &expected_count) != 0) {
        fprintf(stderr, "%s: %s: expected header has an empty token\n",
                ctx->test_name, description);
        return 1;
    }

    if (actual_count != expected_count) {
        fprintf(stderr, "%s: %s: expected %zu header tokens, got %zu: %.*s\n",
                ctx->test_name, description, expected_count, actual_count,
                (int)actual.len, actual.data);
        return 1;
    }

    for (size_t i = 0; i < expected_count; ++i) {
        StrSpan expected_token = token_at(expected, i);
        size_t matches = 0;

        for (size_t j = 0; j < actual_count; ++j) {
            StrSpan actual_token = token_at(actual, j);
            if (actual_token.len == expected_token.len &&
                memcmp(actual_token.data, expected_token.data,
                       actual_token.len) == 0)
                ++matches;
        }

        if (matches != 1) {
            fprintf(stderr,
                    "%s: %s: expected token %.*s occurs %zu times in %.*s\n",
                    ctx->test_name, description, (int)expected_token.len,
                    expected_token.data, matches, (int)actual.len, actual.data);
            return 1;
        }
    }

    return 0;
}
