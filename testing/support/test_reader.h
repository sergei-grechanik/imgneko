// SPDX-License-Identifier: MIT-0

// Assertions and reader helpers used by C unit tests.

#ifndef TEST_READER_H
#define TEST_READER_H

#include <stdbool.h>
#include <stddef.h>

#include "imgneko/reader.h"
#include "test_main.h"
#include "util/string.h"

// Test stream reader source that returns at most the caller-selected chunk
// sizes. Fields are public so tests can configure the source and assert how a
// transformer read from it.
typedef struct TestChunkedReader {
    // Source bytes returned by the reader.
    const char *data;
    // Number of bytes available in `data`.
    size_t len;
    // Current byte offset in `data`; tests may inspect it after reads.
    size_t offset;
    // Optional per-read chunk limits. A zero chunk entry is treated as 1 byte
    // so the reader still makes progress.
    const size_t *chunks;
    // Number of entries in `chunks`.
    size_t chunk_count;
    // Index of the next configured chunk. Once this reaches `chunk_count`, the
    // reader uses the caller's full output capacity for all remaining reads.
    size_t chunk_index;
    // Number of source read attempts that reached this reader before EOF.
    size_t read_count;
    // Largest output capacity this reader has seen.
    size_t max_out_cap;
    // True after this reader has reported EOF.
    bool eof;
} TestChunkedReader;

// Test source that requires the caller to provide a buffer large enough for the
// complete logical chunk before it returns any bytes.
typedef struct TestCompleteChunkReader {
    const char *data;
    size_t len;
    bool eof;
} TestCompleteChunkReader;

// Print a failure message for `ctx` and return a failing status code.
int test_fail_message(const TestContext *ctx, const char *message);

// Check that an integer status matches the expected value.
int test_expect_status(const TestContext *ctx, int actual, int expected,
                       const char *label);

// Check that a size value matches the expected value.
int test_expect_size(const TestContext *ctx, size_t actual, size_t expected,
                     const char *label);

// Check that bytes match the expected span exactly.
int test_expect_data(const TestContext *ctx, const char *actual,
                     size_t actual_len, const char *expected,
                     size_t expected_len, const char *label);

// Check returned output and verify the writer did not touch bytes beyond it.
//
// `ctx`
//     Test context used for failure diagnostics.
// `actual`
//     Buffer written by the code under test.
// `actual_len`
//     Number of bytes reported as written.
// `actual_cap`
//     Total number of bytes available in `actual`.
// `expected`
//     Expected bytes in the reported output prefix.
// `expected_len`
//     Number of bytes in `expected`.
// `guard`
//     Byte that should remain in `actual[actual_len..actual_cap)`.
// `label`
//     Human-readable description of the checked output.
int test_expect_buffer_output(const TestContext *ctx, const char *actual,
                              size_t actual_len, size_t actual_cap,
                              const char *expected, size_t expected_len,
                              char guard, const char *label);

// Return a reader view for a chunked test source.
ImgnekoReader test_chunked_reader_as_reader(TestChunkedReader *reader);

// Return a reader view for a complete-chunk test source.
ImgnekoReader
test_complete_chunk_reader_as_reader(TestCompleteChunkReader *reader);

// Drain a reader into an owned string using a fixed-size caller buffer.
//
// `ctx`
//     Test context used for failure diagnostics.
// `reader`
//     Reader to drain until EOF.
// `chunk_size`
//     Number of bytes to request from `reader` on each call.
// `out`
//     Output string that receives drained bytes. The caller owns the string and
//     remains responsible for freeing it.
int test_drain_reader(const TestContext *ctx, ImgnekoReader reader,
                      size_t chunk_size, String *out);

#endif
