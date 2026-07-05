// SPDX-License-Identifier: MIT-0

// Implementation of reader-oriented C unit-test helpers.

#include "test_reader.h"

#include <stdio.h>
#include <string.h>

// Reader callback for a source with intentionally awkward maximum chunk sizes.
static int test_chunked_reader_func(void *ctx, char *out, size_t out_cap,
                                    size_t *len_out) {
    TestChunkedReader *source = ctx;

    if (len_out == NULL)
        return IMGNEKO_READER_ERROR;
    *len_out = 0;

    if (source == NULL || (out == NULL && out_cap != 0))
        return IMGNEKO_READER_ERROR;

    if (source->eof)
        return IMGNEKO_READER_EOF;

    ++source->read_count;
    if (out_cap > source->max_out_cap)
        source->max_out_cap = out_cap;

    if (source->offset == source->len) {
        source->eof = true;
        return IMGNEKO_READER_EOF;
    }

    size_t configured = out_cap;
    bool advance_chunk = false;
    if (source->chunk_index < source->chunk_count) {
        configured = source->chunks[source->chunk_index];
        advance_chunk = true;
    }
    if (configured == 0)
        configured = 1;

    if (out_cap == 0) {
        *len_out = 1;
        return IMGNEKO_READER_BUFFER_TOO_SMALL;
    }

    size_t remaining = source->len - source->offset;
    size_t len = configured < remaining ? configured : remaining;
    if (len > out_cap)
        len = out_cap;

    memcpy(out, source->data + source->offset, len);
    source->offset += len;
    if (advance_chunk)
        ++source->chunk_index;

    *len_out = len;
    return IMGNEKO_READER_OK;
}

// Reader callback for a source that rejects buffers smaller than its whole
// logical chunk.
static int test_complete_chunk_reader_func(void *ctx, char *out, size_t out_cap,
                                           size_t *len_out) {
    TestCompleteChunkReader *source = ctx;

    if (len_out == NULL)
        return IMGNEKO_READER_ERROR;
    *len_out = 0;

    if (source == NULL || (out == NULL && out_cap != 0))
        return IMGNEKO_READER_ERROR;

    if (source->eof)
        return IMGNEKO_READER_EOF;

    if (out_cap < source->len) {
        *len_out = source->len;
        return IMGNEKO_READER_BUFFER_TOO_SMALL;
    }

    memcpy(out, source->data, source->len);
    *len_out = source->len;
    source->eof = true;
    return IMGNEKO_READER_OK;
}

int test_fail_message(const TestContext *ctx, const char *message) {
    fprintf(stderr, "%s: %s\n", ctx->test_name, message);
    return 1;
}

int test_expect_status(const TestContext *ctx, int actual, int expected,
                       const char *label) {
    if (actual == expected)
        return 0;

    fprintf(stderr, "%s: %s: expected status %d, got %d\n", ctx->test_name,
            label, expected, actual);
    return 1;
}

int test_expect_size(const TestContext *ctx, size_t actual, size_t expected,
                     const char *label) {
    if (actual == expected)
        return 0;

    fprintf(stderr, "%s: %s: expected size %zu, got %zu\n", ctx->test_name,
            label, expected, actual);
    return 1;
}

int test_expect_data(const TestContext *ctx, const char *actual,
                     size_t actual_len, const char *expected,
                     size_t expected_len, const char *label) {
    if (actual_len == expected_len &&
        memcmp(actual, expected, expected_len) == 0)
        return 0;

    fprintf(stderr, "%s: %s: byte output differs\n", ctx->test_name, label);
    return 1;
}

int test_expect_buffer_output(const TestContext *ctx, const char *actual,
                              size_t actual_len, size_t actual_cap,
                              const char *expected, size_t expected_len,
                              char guard, const char *label) {
    if (actual_len > actual_cap) {
        fprintf(stderr, "%s: %s: output length exceeds capacity\n",
                ctx->test_name, label);
        return 1;
    }

    if (test_expect_data(ctx, actual, actual_len, expected, expected_len,
                         label))
        return 1;

    for (size_t i = actual_len; i < actual_cap; ++i) {
        if (actual[i] == guard)
            continue;

        fprintf(stderr, "%s: %s: output tail modified at byte %zu\n",
                ctx->test_name, label, i);
        return 1;
    }

    return 0;
}

ImgnekoReader test_chunked_reader_as_reader(TestChunkedReader *reader) {
    return (ImgnekoReader){
        .read = test_chunked_reader_func,
        .ctx = reader,
    };
}

ImgnekoReader
test_complete_chunk_reader_as_reader(TestCompleteChunkReader *reader) {
    return (ImgnekoReader){
        .read = test_complete_chunk_reader_func,
        .ctx = reader,
    };
}

int test_drain_reader(const TestContext *ctx, ImgnekoReader reader,
                      size_t chunk_size, String *out) {
    char buffer[8];

    if (chunk_size > sizeof(buffer))
        return test_fail_message(ctx, "test chunk size is too large");

    while (true) {
        size_t len = 0;

        memset(buffer, 'x', sizeof(buffer));
        int status = imgneko_reader_read(reader, buffer, chunk_size, &len);

        if (status == IMGNEKO_READER_EOF)
            return 0;

        if (status != IMGNEKO_READER_OK) {
            fprintf(stderr, "%s: drain failed with reader status %d\n",
                    ctx->test_name, status);
            return 1;
        }

        if (len == 0)
            return test_fail_message(ctx, "reader returned empty OK");

        if (len > chunk_size)
            return test_fail_message(ctx, "reader returned too many bytes");

        for (size_t i = len; i < sizeof(buffer); ++i) {
            if (buffer[i] == 'x')
                continue;

            return test_fail_message(
                ctx, "reader modified bytes beyond returned data");
        }

        str_append_data(*out, buffer, len);
    }
}
