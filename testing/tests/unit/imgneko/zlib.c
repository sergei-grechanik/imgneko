// SPDX-License-Identifier: MIT-0

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "imgneko/zlib.h"
#include "test_main.h"
#include "test_reader.h"
#include "util/common.h"
#include "util/string.h"

#define STR(text) (text), (sizeof(text) - 1)

static const char RAW_HELLO[] = "hello world";
static const char ZLIB_HELLO[] =
    "\x78\x9c\xcb\x48\xcd\xc9\xc9\x57\x28\xcf\x2f\xca\x49\x01\x00"
    "\x1a\x0b\x04\x5d";
static const char ZLIB_EMPTY[] = "\x78\x9c\x03\x00\x00\x00\x00\x01";

// Create a small RFC 1950 stream that requires a preset dictionary.
//
// A compression dictionary is shared byte history that lets the compressor
// encode matching input without first emitting that history. A preset
// dictionary is agreed on before processing and identified in the RFC 1950
// header. Create a stream requiring one to verify that a reader without a
// dictionary API reports IMGNEKO_ZLIB_DICTIONARY_REQUIRED.
//
// `out` receives the encoded stream.
// `out_cap` is the number of bytes available in `out`.
// `len_out` receives the number of stream bytes written to `out`.
static int make_dictionary_stream(char *out, size_t out_cap, size_t *len_out) {
    static const char dictionary[] = "dictionary";
    static const char data[] = "dictionary data";
    z_stream stream = {0};
    bool initialized = false;
    int result = 1;

    *len_out = 0;

    if (out_cap > UINT_MAX)
        out_cap = UINT_MAX;

    if (deflateInit(&stream, Z_DEFAULT_COMPRESSION) != Z_OK)
        goto cleanup;
    initialized = true;

    if (deflateSetDictionary(&stream, (const Bytef *)dictionary,
                             sizeof(dictionary) - 1) != Z_OK) {
        goto cleanup;
    }

    stream.next_in = (Bytef *)data;
    stream.avail_in = sizeof(data) - 1;
    stream.next_out = (Bytef *)out;
    stream.avail_out = (uInt)out_cap;

    if (deflate(&stream, Z_FINISH) != Z_STREAM_END)
        goto cleanup;

    *len_out = out_cap - stream.avail_out;
    result = 0;

cleanup:
    if (initialized)
        deflateEnd(&stream);
    return result;
}

// Test source that emits `data` once, then returns `final_status` and
// `final_len`. The configurable final read covers normal EOF, source errors,
// and invalid source-reader results during trailing-input verification.
typedef struct StatusAfterDataReader {
    const char *data;
    size_t len;
    bool emitted;
    ImgnekoReaderStatus final_status;
    size_t final_len;
} StatusAfterDataReader;

// Malformed test source that reports an OK read of `reported_len` bytes
// without writing them, used to verify reader-contract validation.
typedef struct BadOkReader {
    size_t reported_len;
} BadOkReader;

// Verify a zlib reader reports a generic reader failure while preserving the
// detailed zlib status in its transformer state.
//
// `ctx`
//     Test context used for failure diagnostics.
// `reader_status`
//     Status returned through the generic reader view.
// `actual_zlib_status`
//     Detailed status stored by the transformer.
// `expected_zlib_status`
//     Detailed zlib status expected by the test.
static int
test_expect_zlib_reader_error(const TestContext *ctx, int reader_status,
                              ImgnekoZlibStatus actual_zlib_status,
                              ImgnekoZlibStatus expected_zlib_status) {
    if (test_expect_status(ctx, reader_status, IMGNEKO_READER_ERROR,
                           "generic reader error")) {
        return 1;
    }

    return test_expect_status(ctx, actual_zlib_status, expected_zlib_status,
                              "stored zlib error status");
}

// Generate deterministic pseudo-random values for repeatable stream tests.
static uint32_t next_test_random(uint32_t *state) {
    uint32_t value = *state;

    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;

    *state = value;
    return value;
}

// Reader callback that always reports a standard source failure.
static ImgnekoReaderStatus error_reader_func(void *ctx, char *out,
                                             size_t out_cap, size_t *len_out) {
    (void)ctx;
    (void)out;
    (void)out_cap;

    *len_out = 0;
    return IMGNEKO_READER_ERROR;
}

// Reader callback that emits a complete chunk and then returns a configured
// status. It exercises source-EOF verification after a complete zlib stream.
static ImgnekoReaderStatus status_after_data_reader_func(void *ctx, char *out,
                                                         size_t out_cap,
                                                         size_t *len_out) {
    StatusAfterDataReader *reader = ctx;

    if (len_out == NULL)
        return IMGNEKO_READER_ERROR;
    *len_out = 0;

    if (reader == NULL || (out == NULL && out_cap != 0))
        return IMGNEKO_READER_ERROR;

    if (reader->emitted) {
        *len_out = reader->final_len;
        return reader->final_status;
    }

    if (out_cap < reader->len) {
        *len_out = reader->len;
        return IMGNEKO_READER_BUFFER_TOO_SMALL;
    }

    memcpy(out, reader->data, reader->len);
    *len_out = reader->len;
    reader->emitted = true;
    return IMGNEKO_READER_OK;
}

// Reader callback that violates the reader contract by reporting an OK read
// with a caller-selected length while writing no bytes.
static ImgnekoReaderStatus bad_ok_reader_func(void *ctx, char *out,
                                              size_t out_cap, size_t *len_out) {
    BadOkReader *reader = ctx;
    (void)out;
    (void)out_cap;

    if (len_out == NULL)
        return IMGNEKO_READER_ERROR;

    *len_out = reader->reported_len;
    return IMGNEKO_READER_OK;
}

// Verify compression creates a valid RFC 1950 stream even when the caller
// takes only a byte at a time.
static int test_compress_reader_rfc1950(TestContext *ctx) {
    ImgnekoMemoryReader source = {0};
    ImgnekoZlibCompressReader compressor = {0};
    char workspace[5];
    String compressed = str_empty;
    int result = 0;

    imgneko_memory_reader_init(&source, STR(RAW_HELLO));
    ImgnekoZlibStatus init_status = imgneko_zlib_compress_reader_init(
        &compressor, imgneko_memory_reader_as_reader(&source), workspace,
        sizeof(workspace));
    if (test_expect_status(ctx, init_status, IMGNEKO_ZLIB_OK,
                           "compression reader initialization")) {
        result = 1;
        goto cleanup;
    }

    result = test_drain_reader(
        ctx, imgneko_zlib_compress_reader_as_reader(&compressor),
        /*chunk_size=*/1, &compressed);
    if (result != 0)
        goto cleanup;
    if (compressed.len < 6) {
        result = test_fail_message(ctx, "compressed stream is too short");
        goto cleanup;
    }

    // Validate the fields that distinguish an RFC 1950 zlib stream from raw
    // DEFLATE: the compression method, header check bits, absence of a preset
    // dictionary, and the big-endian Adler-32 trailer.
    unsigned char cmf = (unsigned char)compressed.cstr[0];
    unsigned char flg = (unsigned char)compressed.cstr[1];
    uint32_t stream_header = ((uint32_t)cmf << 8) | flg;
    uint32_t expected_adler = adler32(0L, Z_NULL, 0);
    uint32_t actual_adler =
        ((uint32_t)(unsigned char)compressed.cstr[compressed.len - 4] << 24) |
        ((uint32_t)(unsigned char)compressed.cstr[compressed.len - 3] << 16) |
        ((uint32_t)(unsigned char)compressed.cstr[compressed.len - 2] << 8) |
        (unsigned char)compressed.cstr[compressed.len - 1];

    expected_adler = adler32(expected_adler, (const Bytef *)RAW_HELLO,
                             sizeof(RAW_HELLO) - 1);
    if ((cmf & 0x0fu) != Z_DEFLATED || stream_header % 31 != 0 ||
        (flg & 0x20u) != 0 || actual_adler != expected_adler) {
        result = test_fail_message(ctx, "invalid RFC 1950 wrapper");
        goto cleanup;
    }

    // Decode with zlib's independent convenience API to verify that the
    // transformer output is interoperable, not merely self-consistent.
    char recovered[sizeof(RAW_HELLO)];
    uLongf recovered_len = sizeof(recovered) - 1;
    int zstatus = uncompress((Bytef *)recovered, &recovered_len,
                             (const Bytef *)compressed.cstr, compressed.len);

    if (zstatus != Z_OK || recovered_len != sizeof(RAW_HELLO) - 1 ||
        memcmp(recovered, RAW_HELLO, sizeof(RAW_HELLO) - 1) != 0) {
        result = test_fail_message(ctx, "zlib could not decode output");
    }

cleanup:
    imgneko_zlib_compress_reader_deinit(&compressor);
    str_free(compressed);
    return result;
}

// Verify decompression handles zlib input and caller output in small chunks.
static int test_decompress_reader_streaming(TestContext *ctx) {
    const size_t chunks[] = {1, 2, 1, 3, 2, 1, 4};
    TestChunkedReader source = {
        .data = ZLIB_HELLO,
        .len = sizeof(ZLIB_HELLO) - 1,
        .chunks = chunks,
        .chunk_count = ARRAY_SIZE(chunks),
    };
    ImgnekoZlibDecompressReader decompressor = {0};
    char workspace[5];
    char eof_out[2];
    String output = str_empty;
    int result = 0;

    ImgnekoZlibStatus init_status = imgneko_zlib_decompress_reader_init(
        &decompressor, test_chunked_reader_as_reader(&source), workspace,
        sizeof(workspace));
    if (test_expect_status(ctx, init_status, IMGNEKO_ZLIB_OK,
                           "decompression reader initialization")) {
        result = 1;
        goto cleanup;
    }

    // Single-byte output requests force repeated inflate calls across the
    // deliberately irregular compressed-source chunks.
    result = test_drain_reader(
        ctx, imgneko_zlib_decompress_reader_as_reader(&decompressor),
        /*chunk_size=*/1, &output);
    if (result != 0)
        goto cleanup;
    result = test_expect_data(ctx, output.cstr, output.len, STR(RAW_HELLO),
                              "decompression reader output");
    if (result != 0)
        goto cleanup;

    // EOF must remain sticky after the stream and its trailing checksum have
    // been fully validated.
    size_t len = 0;
    int status = imgneko_reader_read(
        imgneko_zlib_decompress_reader_as_reader(&decompressor), eof_out,
        sizeof(eof_out), &len);
    if (test_expect_status(ctx, status, IMGNEKO_READER_EOF,
                           "decompression reader sticky EOF") ||
        test_expect_size(ctx, len, 0, "decompression reader sticky EOF size")) {
        result = 1;
    }

cleanup:
    imgneko_zlib_decompress_reader_deinit(&decompressor);
    str_free(output);
    return result;
}

// Verify compression and decompression correctly represent an empty RFC 1950
// stream, including EOF on the first decompressor read.
static int test_empty_streams(TestContext *ctx) {
    ImgnekoMemoryReader raw_source = {0};
    ImgnekoMemoryReader compressed_source = {0};
    ImgnekoZlibCompressReader compressor = {0};
    ImgnekoZlibDecompressReader decompressor = {0};
    char compress_workspace[8];
    char decompress_workspace[8];
    char out[1];
    String compressed = str_empty;
    int result = 0;

    imgneko_memory_reader_init(&raw_source, NULL, 0);
    ImgnekoZlibStatus init_status = imgneko_zlib_compress_reader_init(
        &compressor, imgneko_memory_reader_as_reader(&raw_source),
        compress_workspace, sizeof(compress_workspace));
    if (test_expect_status(ctx, init_status, IMGNEKO_ZLIB_OK,
                           "empty compression initialization")) {
        result = 1;
        goto cleanup;
    }

    result = test_drain_reader(
        ctx, imgneko_zlib_compress_reader_as_reader(&compressor),
        /*chunk_size=*/1, &compressed);
    if (result != 0)
        goto cleanup;

    imgneko_memory_reader_init(&compressed_source, compressed.cstr,
                               compressed.len);
    init_status = imgneko_zlib_decompress_reader_init(
        &decompressor, imgneko_memory_reader_as_reader(&compressed_source),
        decompress_workspace, sizeof(decompress_workspace));
    if (test_expect_status(ctx, init_status, IMGNEKO_ZLIB_OK,
                           "empty decompression initialization")) {
        result = 1;
        goto cleanup;
    }

    for (size_t i = 0; i < 2; ++i) {
        size_t len = 0;
        int status = imgneko_reader_read(
            imgneko_zlib_decompress_reader_as_reader(&decompressor), out,
            sizeof(out), &len);

        if (test_expect_status(ctx, status, IMGNEKO_READER_EOF,
                               "empty decompression EOF") ||
            test_expect_size(ctx, len, 0, "empty decompression EOF size")) {
            result = 1;
            break;
        }
    }

cleanup:
    str_free(compressed);
    imgneko_zlib_decompress_reader_deinit(&decompressor);
    imgneko_zlib_compress_reader_deinit(&compressor);
    return result;
}

// Verify stacked zlib readers preserve binary data through irregular source
// and output chunk boundaries.
static int test_stacked_reader_round_trip(TestContext *ctx) {
    enum { INPUT_LEN = 1021 };
    const size_t chunks[] = {1, 7, 2, 13, 3, 5, 11};
    char input[INPUT_LEN];
    TestChunkedReader source = {
        .data = input,
        .len = sizeof(input),
        .chunks = chunks,
        .chunk_count = ARRAY_SIZE(chunks),
    };
    ImgnekoZlibCompressReader compressor = {0};
    ImgnekoZlibDecompressReader decompressor = {0};
    char compress_workspace[31];
    char decompress_workspace[17];
    String output = str_empty;
    int result = 0;

    for (size_t i = 0; i < sizeof(input); ++i)
        input[i] = (char)(i * 37u + i / 7u);
    input[0] = '\0';
    input[sizeof(input) / 2] = '\0';

    ImgnekoZlibStatus status = imgneko_zlib_compress_reader_init(
        &compressor, test_chunked_reader_as_reader(&source), compress_workspace,
        sizeof(compress_workspace));
    if (test_expect_status(ctx, status, IMGNEKO_ZLIB_OK,
                           "stacked compression initialization")) {
        result = 1;
        goto cleanup;
    }

    status = imgneko_zlib_decompress_reader_init(
        &decompressor, imgneko_zlib_compress_reader_as_reader(&compressor),
        decompress_workspace, sizeof(decompress_workspace));
    if (test_expect_status(ctx, status, IMGNEKO_ZLIB_OK,
                           "stacked decompression initialization")) {
        result = 1;
        goto cleanup;
    }

    result = test_drain_reader(
        ctx, imgneko_zlib_decompress_reader_as_reader(&decompressor),
        /*chunk_size=*/3, &output);
    if (result != 0)
        goto cleanup;
    result = test_expect_data(ctx, output.cstr, output.len, input,
                              sizeof(input), "stacked reader output");

cleanup:
    imgneko_zlib_decompress_reader_deinit(&decompressor);
    imgneko_zlib_compress_reader_deinit(&compressor);
    str_free(output);
    return result;
}

// Run a stacked round trip with caller capacities and source chunks derived
// from `seed`. This exercises transitions where zlib buffers data internally.
static int run_stacked_reader_pseudorandom_round_trip(const TestContext *ctx,
                                                      uint32_t seed) {
    enum {
        INPUT_LEN = 4097,
        SOURCE_CHUNK_COUNT = 4097,
    };

    uint32_t random = seed;
    char input[INPUT_LEN];
    size_t source_chunks[SOURCE_CHUNK_COUNT];
    TestChunkedReader source = {
        .data = input,
        .len = sizeof(input),
        .chunks = source_chunks,
        .chunk_count = ARRAY_SIZE(source_chunks),
    };
    ImgnekoZlibCompressReader compressor = {0};
    ImgnekoZlibDecompressReader decompressor = {0};
    char compress_workspace[96];
    char decompress_workspace[128];
    char out[257];
    String output = str_empty;
    int result = 0;

    // Use deterministic binary input, including embedded NULs, so failures can
    // be reproduced from the seed while still exercising non-text data.
    for (size_t i = 0; i < sizeof(input); ++i) {
        input[i] = (char)next_test_random(&random);
        if (i % 257 == 0)
            input[i] = '\0';
    }

    // Vary every source read boundary independently from output capacities.
    for (size_t i = 0; i < ARRAY_SIZE(source_chunks); ++i)
        source_chunks[i] = next_test_random(&random) % 97 + 1;

    ImgnekoZlibStatus status = imgneko_zlib_compress_reader_init(
        &compressor, test_chunked_reader_as_reader(&source), compress_workspace,
        sizeof(compress_workspace));
    if (status != IMGNEKO_ZLIB_OK) {
        result = test_expect_status(ctx, status, IMGNEKO_ZLIB_OK,
                                    "pseudorandom compression initialization");
        goto cleanup;
    }

    status = imgneko_zlib_decompress_reader_init(
        &decompressor, imgneko_zlib_compress_reader_as_reader(&compressor),
        decompress_workspace, sizeof(decompress_workspace));
    if (status != IMGNEKO_ZLIB_OK) {
        result =
            test_expect_status(ctx, status, IMGNEKO_ZLIB_OK,
                               "pseudorandom decompression initialization");
        goto cleanup;
    }

    // Include zero-capacity reads. When the reader requests a retry size,
    // verify that it preserves the output buffer and succeeds with that size.
    while (true) {
        size_t cap = next_test_random(&random) % sizeof(out);
        size_t len = 0;

        memset(out, 'x', sizeof(out));
        int read_status = imgneko_reader_read(
            imgneko_zlib_decompress_reader_as_reader(&decompressor), out, cap,
            &len);

        if (read_status == IMGNEKO_READER_BUFFER_TOO_SMALL) {
            if (len == 0 || len > sizeof(out)) {
                result = test_fail_message(ctx, "invalid retry size");
                break;
            }

            if (test_expect_buffer_output(ctx, out, 0, sizeof(out), "", 0, 'x',
                                          "small output modifies buffer")) {
                result = 1;
                break;
            }

            cap = len;
            memset(out, 'x', sizeof(out));
            read_status = imgneko_reader_read(
                imgneko_zlib_decompress_reader_as_reader(&decompressor), out,
                cap, &len);
        }

        if (read_status == IMGNEKO_READER_EOF) {
            result =
                test_expect_size(ctx, len, 0, "pseudorandom stacked EOF size");
            break;
        }

        if (read_status != IMGNEKO_READER_OK) {
            fprintf(stderr,
                    "%s: pseudorandom stacked reader failed with "
                    "status %d\n",
                    ctx->test_name, read_status);
            result = 1;
            break;
        }

        if (len == 0 || len > cap) {
            result = test_fail_message(ctx, "invalid stacked reader output");
            break;
        }

        if (test_expect_buffer_output(ctx, out, len, sizeof(out), out, len, 'x',
                                      "stacked output bounds")) {
            result = 1;
            break;
        }

        str_append_data(output, out, len);
    }

    if (result != 0)
        goto cleanup;
    result = test_expect_data(ctx, output.cstr, output.len, input,
                              sizeof(input), "pseudorandom stacked output");

cleanup:
    str_free(output);
    imgneko_zlib_decompress_reader_deinit(&decompressor);
    imgneko_zlib_compress_reader_deinit(&compressor);
    return result;
}

// Verify multiple randomized stacked-reader round trips are lossless and keep
// output bounds intact for irregular caller capacities.
static int test_stacked_reader_pseudorandom_round_trip(TestContext *ctx) {
    const uint32_t seeds[] = {
        0x1f2e3d4cu,
        0x12345678u,
        0x9e3779b9u,
        0xa5a5c3d2u,
    };

    for (size_t i = 0; i < ARRAY_SIZE(seeds); ++i) {
        int result = run_stacked_reader_pseudorandom_round_trip(ctx, seeds[i]);
        if (result != 0)
            return result;
    }

    return 0;
}

// Verify a zero-capacity caller read does not consume compressor or
// decompressor state before a retry with usable output storage.
static int test_reader_zero_capacity_retry(TestContext *ctx) {
    ImgnekoMemoryReader raw_source = {0};
    ImgnekoMemoryReader compressed_source = {0};
    ImgnekoZlibCompressReader compressor = {0};
    ImgnekoZlibDecompressReader decompressor = {0};
    char compress_workspace[8];
    char decompress_workspace[8];
    size_t len = 0;
    String compressed = str_empty;
    String decompressed = str_empty;
    int result = 0;

    imgneko_memory_reader_init(&raw_source, STR(RAW_HELLO));
    ImgnekoZlibStatus init_status = imgneko_zlib_compress_reader_init(
        &compressor, imgneko_memory_reader_as_reader(&raw_source),
        compress_workspace, sizeof(compress_workspace));
    if (test_expect_status(ctx, init_status, IMGNEKO_ZLIB_OK,
                           "retry compression initialization")) {
        result = 1;
        goto cleanup;
    }

    // The first call cannot hold even the zlib header. It must request a retry
    // without advancing either the source or compressor state.
    int status = imgneko_reader_read(
        imgneko_zlib_compress_reader_as_reader(&compressor), NULL, 0, &len);
    if (test_expect_status(ctx, status, IMGNEKO_READER_BUFFER_TOO_SMALL,
                           "compression zero-capacity read") ||
        test_expect_size(ctx, len, 1, "compression zero-capacity retry size")) {
        result = 1;
        goto cleanup;
    }

    result = test_drain_reader(
        ctx, imgneko_zlib_compress_reader_as_reader(&compressor),
        /*chunk_size=*/2, &compressed);
    if (result != 0)
        goto cleanup;

    imgneko_memory_reader_init(&compressed_source, compressed.cstr,
                               compressed.len);
    init_status = imgneko_zlib_decompress_reader_init(
        &decompressor, imgneko_memory_reader_as_reader(&compressed_source),
        decompress_workspace, sizeof(decompress_workspace));
    if (test_expect_status(ctx, init_status, IMGNEKO_ZLIB_OK,
                           "retry decompression initialization")) {
        result = 1;
        goto cleanup;
    }

    // Repeat the same contract check for decompression, then drain the reader
    // to prove the zero-capacity attempt consumed no compressed input.
    status = imgneko_reader_read(
        imgneko_zlib_decompress_reader_as_reader(&decompressor), NULL, 0, &len);
    if (test_expect_status(ctx, status, IMGNEKO_READER_BUFFER_TOO_SMALL,
                           "decompression zero-capacity read") ||
        test_expect_size(ctx, len, 1,
                         "decompression zero-capacity retry size")) {
        result = 1;
        goto cleanup;
    }

    result = test_drain_reader(
        ctx, imgneko_zlib_decompress_reader_as_reader(&decompressor),
        /*chunk_size=*/2, &decompressed);
    if (result != 0)
        goto cleanup;
    result = test_expect_data(ctx, decompressed.cstr, decompressed.len,
                              STR(RAW_HELLO), "decompression retry data");

cleanup:
    str_free(decompressed);
    str_free(compressed);
    imgneko_zlib_decompress_reader_deinit(&decompressor);
    imgneko_zlib_compress_reader_deinit(&compressor);
    return result;
}

// Verify initialization rejects a workspace capacity larger than zlib's uInt
// interface can represent.
static int test_reader_oversized_workspace(TestContext *ctx) {
#if SIZE_MAX <= UINT_MAX
    (void)ctx;
    return 0;
#else
    ImgnekoZlibCompressReader compressor = {0};
    ImgnekoZlibDecompressReader decompressor = {0};
    char workspace[1];
    size_t oversized_cap = (size_t)UINT_MAX + 1u;

    ImgnekoZlibStatus init_status = imgneko_zlib_compress_reader_init(
        &compressor, (ImgnekoReader){0}, workspace, oversized_cap);
    if (test_expect_status(ctx, init_status, IMGNEKO_ZLIB_INVALID_ARGUMENT,
                           "oversized compression workspace") ||
        test_expect_status(ctx, compressor.error_status,
                           IMGNEKO_ZLIB_INVALID_ARGUMENT,
                           "oversized compression error status")) {
        return 1;
    }
    imgneko_zlib_compress_reader_deinit(&compressor);

    init_status = imgneko_zlib_decompress_reader_init(
        &decompressor, (ImgnekoReader){0}, workspace, oversized_cap);
    if (test_expect_status(ctx, init_status, IMGNEKO_ZLIB_INVALID_ARGUMENT,
                           "oversized decompression workspace") ||
        test_expect_status(ctx, decompressor.error_status,
                           IMGNEKO_ZLIB_INVALID_ARGUMENT,
                           "oversized decompression error status")) {
        return 1;
    }
    imgneko_zlib_decompress_reader_deinit(&decompressor);
    return 0;
#endif
}

// Verify malformed, incomplete, and trailing zlib input is reported only
// after valid output preceding the error has been made available.
static int test_decompress_reader_input_errors(TestContext *ctx) {
    char corrupted[sizeof(ZLIB_HELLO) - 1];
    char truncated[sizeof(ZLIB_HELLO) - 2];
    char trailing[sizeof(ZLIB_HELLO)];
    const char *inputs[] = {corrupted, truncated, trailing};
    const ImgnekoZlibStatus expected_statuses[] = {
        IMGNEKO_ZLIB_INVALID_INPUT,
        IMGNEKO_ZLIB_TRUNCATED_INPUT,
        IMGNEKO_ZLIB_TRAILING_INPUT,
    };
    const size_t input_lens[] = {
        sizeof(corrupted),
        sizeof(truncated),
        sizeof(trailing),
    };

    // Derive three failures from the same valid stream: corrupt its checksum,
    // remove the final checksum byte, or append a byte after stream end.
    memcpy(corrupted, ZLIB_HELLO, sizeof(corrupted));
    corrupted[sizeof(corrupted) - 1] ^= 1;
    memcpy(truncated, ZLIB_HELLO, sizeof(truncated));
    memcpy(trailing, ZLIB_HELLO, sizeof(ZLIB_HELLO) - 1);
    trailing[sizeof(trailing) - 1] = 'x';

    for (size_t i = 0; i < ARRAY_SIZE(inputs); ++i) {
        ImgnekoMemoryReader source = {0};
        ImgnekoZlibDecompressReader decompressor = {0};
        char workspace[64];
        char out[64];
        size_t len = 0;

        // Each malformed stream still contains a complete "hello world"
        // payload, so initialization succeeds and validation happens on read.
        imgneko_memory_reader_init(&source, inputs[i], input_lens[i]);
        ImgnekoZlibStatus init_status = imgneko_zlib_decompress_reader_init(
            &decompressor, imgneko_memory_reader_as_reader(&source), workspace,
            sizeof(workspace));
        if (test_expect_status(ctx, init_status, IMGNEKO_ZLIB_OK,
                               "invalid-input initialization"))
            return 1;

        // The reader must not discard valid output merely because it discovers
        // a checksum, truncation, or trailing-input failure in the same call.
        memset(out, 'x', sizeof(out));
        int status = imgneko_reader_read(
            imgneko_zlib_decompress_reader_as_reader(&decompressor), out,
            sizeof(out), &len);
        if (test_expect_status(ctx, status, IMGNEKO_READER_OK,
                               "invalid-input valid prefix") ||
            test_expect_buffer_output(ctx, out, len, sizeof(out),
                                      STR(RAW_HELLO), 'x',
                                      "invalid-input valid prefix data")) {
            imgneko_zlib_decompress_reader_deinit(&decompressor);
            return 1;
        }

        // Once the valid prefix has been delivered, expose the deferred
        // detailed failure without modifying the caller's buffer.
        memset(out, 'x', sizeof(out));
        status = imgneko_reader_read(
            imgneko_zlib_decompress_reader_as_reader(&decompressor), out,
            sizeof(out), &len);
        if (test_expect_zlib_reader_error(
                ctx, status, decompressor.error_status, expected_statuses[i]) ||
            test_expect_size(ctx, len, 0,
                             "invalid-input deferred status size") ||
            test_expect_buffer_output(ctx, out, len, sizeof(out), "", 0, 'x',
                                      "invalid-input deferred status output")) {
            imgneko_zlib_decompress_reader_deinit(&decompressor);
            return 1;
        }

        imgneko_zlib_decompress_reader_deinit(&decompressor);
    }

    return 0;
}

// Verify a malformed zlib header returns a sticky error without consuming or
// modifying the caller's output buffer.
static int test_decompress_reader_immediate_invalid_input(TestContext *ctx) {
    const char invalid_input[] = "\x78\x00";
    ImgnekoMemoryReader source = {0};
    ImgnekoZlibDecompressReader decompressor = {0};
    char workspace[8];
    char out[8];
    size_t len = 0;

    imgneko_memory_reader_init(&source, invalid_input,
                               sizeof(invalid_input) - 1);
    ImgnekoZlibStatus init_status = imgneko_zlib_decompress_reader_init(
        &decompressor, imgneko_memory_reader_as_reader(&source), workspace,
        sizeof(workspace));
    if (test_expect_status(ctx, init_status, IMGNEKO_ZLIB_OK,
                           "immediate-invalid initialization"))
        return 1;

    // Read twice to verify both the initial invalid-header failure and its
    // sticky behavior on subsequent calls.
    for (size_t i = 0; i < 2; ++i) {
        memset(out, 'x', sizeof(out));
        int status = imgneko_reader_read(
            imgneko_zlib_decompress_reader_as_reader(&decompressor), out,
            sizeof(out), &len);

        if (test_expect_zlib_reader_error(ctx, status,
                                          decompressor.error_status,
                                          IMGNEKO_ZLIB_INVALID_INPUT) ||
            test_expect_size(ctx, len, 0, "immediate-invalid output size") ||
            test_expect_buffer_output(ctx, out, len, sizeof(out), "", 0, 'x',
                                      "immediate-invalid output")) {
            imgneko_zlib_decompress_reader_deinit(&decompressor);
            return 1;
        }
    }

    imgneko_zlib_decompress_reader_deinit(&decompressor);
    return 0;
}

// Verify trailing bytes in a later source read are rejected after valid output
// rather than being silently ignored at the zlib stream boundary.
static int test_decompress_reader_trailing_source_data(TestContext *ctx) {
    const size_t chunks[] = {sizeof(ZLIB_HELLO) - 1, 1};
    char input[sizeof(ZLIB_HELLO)];
    TestChunkedReader source = {
        .data = input,
        .len = sizeof(input),
        .chunks = chunks,
        .chunk_count = ARRAY_SIZE(chunks),
    };
    ImgnekoZlibDecompressReader decompressor = {0};
    char workspace[sizeof(ZLIB_HELLO) - 1];
    char out[64];
    size_t len = 0;

    // The chunk boundary hides the extra byte until after inflate reports a
    // complete stream, forcing the reader to query its source once more.
    memcpy(input, ZLIB_HELLO, sizeof(ZLIB_HELLO) - 1);
    input[sizeof(input) - 1] = 'x';

    ImgnekoZlibStatus init_status = imgneko_zlib_decompress_reader_init(
        &decompressor, test_chunked_reader_as_reader(&source), workspace,
        sizeof(workspace));
    if (test_expect_status(ctx, init_status, IMGNEKO_ZLIB_OK,
                           "trailing-source initialization"))
        return 1;

    // Return the complete decoded payload before reporting the trailing byte.
    memset(out, 'x', sizeof(out));
    int status = imgneko_reader_read(
        imgneko_zlib_decompress_reader_as_reader(&decompressor), out,
        sizeof(out), &len);
    if (test_expect_status(ctx, status, IMGNEKO_READER_OK,
                           "trailing-source valid output") ||
        test_expect_buffer_output(ctx, out, len, sizeof(out), STR(RAW_HELLO),
                                  'x', "trailing-source valid data")) {
        imgneko_zlib_decompress_reader_deinit(&decompressor);
        return 1;
    }

    // The next call exposes the deferred trailing-input error and no data.
    memset(out, 'x', sizeof(out));
    status = imgneko_reader_read(
        imgneko_zlib_decompress_reader_as_reader(&decompressor), out,
        sizeof(out), &len);
    if (test_expect_zlib_reader_error(ctx, status, decompressor.error_status,
                                      IMGNEKO_ZLIB_TRAILING_INPUT) ||
        test_expect_size(ctx, len, 0, "trailing-source output size") ||
        test_expect_buffer_output(ctx, out, len, sizeof(out), "", 0, 'x',
                                  "trailing-source output")) {
        imgneko_zlib_decompress_reader_deinit(&decompressor);
        return 1;
    }

    imgneko_zlib_decompress_reader_deinit(&decompressor);
    return 0;
}

// Verify RFC 1950 streams that request a preset dictionary report a generic
// reader error with the documented detailed status.
static int test_decompress_reader_dictionary_required(TestContext *ctx) {
    char compressed[64];
    size_t compressed_len = 0;
    ImgnekoMemoryReader source = {0};
    ImgnekoZlibDecompressReader decompressor = {0};
    char workspace[64];
    char out[8];
    size_t len = 0;

    // Generate this input with zlib itself so the preset-dictionary marker and
    // dictionary checksum are valid rather than hand-crafted test bytes.
    if (make_dictionary_stream(compressed, sizeof(compressed),
                               &compressed_len) != 0) {
        return test_fail_message(ctx, "could not create dictionary stream");
    }

    imgneko_memory_reader_init(&source, compressed, compressed_len);
    ImgnekoZlibStatus init_status = imgneko_zlib_decompress_reader_init(
        &decompressor, imgneko_memory_reader_as_reader(&source), workspace,
        sizeof(workspace));
    if (test_expect_status(ctx, init_status, IMGNEKO_ZLIB_OK,
                           "dictionary initialization"))
        return 1;

    // No payload can be decoded without the missing dictionary, so the first
    // read fails without producing or overwriting output.
    memset(out, 'x', sizeof(out));
    int status = imgneko_reader_read(
        imgneko_zlib_decompress_reader_as_reader(&decompressor), out,
        sizeof(out), &len);
    if (test_expect_zlib_reader_error(ctx, status, decompressor.error_status,
                                      IMGNEKO_ZLIB_DICTIONARY_REQUIRED) ||
        test_expect_size(ctx, len, 0, "dictionary output size") ||
        test_expect_buffer_output(ctx, out, len, sizeof(out), "", 0, 'x',
                                  "dictionary output")) {
        imgneko_zlib_decompress_reader_deinit(&decompressor);
        return 1;
    }

    // The specific dictionary-required status must remain sticky.
    status = imgneko_reader_read(
        imgneko_zlib_decompress_reader_as_reader(&decompressor), out,
        sizeof(out), &len);
    if (test_expect_zlib_reader_error(ctx, status, decompressor.error_status,
                                      IMGNEKO_ZLIB_DICTIONARY_REQUIRED) ||
        test_expect_size(ctx, len, 0, "dictionary sticky output size")) {
        imgneko_zlib_decompress_reader_deinit(&decompressor);
        return 1;
    }

    imgneko_zlib_decompress_reader_deinit(&decompressor);
    return 0;
}

// Verify source workspace failures and ordinary source failures preserve the
// reader transformer contract.
static int test_reader_source_failures(TestContext *ctx) {
    TestCompleteChunkReader raw_source = {
        .data = RAW_HELLO,
        .len = sizeof(RAW_HELLO) - 1,
    };
    TestCompleteChunkReader compressed_source = {
        .data = ZLIB_HELLO,
        .len = sizeof(ZLIB_HELLO) - 1,
    };
    ImgnekoReader error_source = {
        .read = error_reader_func,
    };
    ImgnekoZlibCompressReader compressor = {0};
    ImgnekoZlibDecompressReader decompressor = {0};
    char small_workspace[2];
    char full_workspace[64];
    char out[64];
    size_t len = 0;

    // A complete-chunk source refuses the two-byte workspace. Compression
    // translates that source capacity failure to a workspace failure.
    ImgnekoZlibStatus init_status = imgneko_zlib_compress_reader_init(
        &compressor, test_complete_chunk_reader_as_reader(&raw_source),
        small_workspace, sizeof(small_workspace));
    if (test_expect_status(ctx, init_status, IMGNEKO_ZLIB_OK,
                           "compression workspace initialization"))
        return 1;

    int status =
        imgneko_reader_read(imgneko_zlib_compress_reader_as_reader(&compressor),
                            out, sizeof(out), &len);
    if (test_expect_status(ctx, status, IMGNEKO_READER_WORKSPACE_TOO_SMALL,
                           "compression source workspace failure")) {
        imgneko_zlib_compress_reader_deinit(&compressor);
        return 1;
    }
    imgneko_zlib_compress_reader_deinit(&compressor);

    // Decompression must apply the same translation before calling inflate.
    init_status = imgneko_zlib_decompress_reader_init(
        &decompressor, test_complete_chunk_reader_as_reader(&compressed_source),
        small_workspace, sizeof(small_workspace));
    if (test_expect_status(ctx, init_status, IMGNEKO_ZLIB_OK,
                           "decompression workspace initialization"))
        return 1;

    status = imgneko_reader_read(
        imgneko_zlib_decompress_reader_as_reader(&decompressor), out,
        sizeof(out), &len);
    if (test_expect_status(ctx, status, IMGNEKO_READER_WORKSPACE_TOO_SMALL,
                           "decompression source workspace failure")) {
        imgneko_zlib_decompress_reader_deinit(&decompressor);
        return 1;
    }
    imgneko_zlib_decompress_reader_deinit(&decompressor);

    // A non-capacity source error passes through compression unchanged.
    init_status = imgneko_zlib_compress_reader_init(
        &compressor, error_source, full_workspace, sizeof(full_workspace));
    if (test_expect_status(ctx, init_status, IMGNEKO_ZLIB_OK,
                           "compression error initialization"))
        return 1;

    status =
        imgneko_reader_read(imgneko_zlib_compress_reader_as_reader(&compressor),
                            out, sizeof(out), &len);
    if (test_expect_status(ctx, status, IMGNEKO_READER_ERROR,
                           "compression source error")) {
        imgneko_zlib_compress_reader_deinit(&compressor);
        return 1;
    }
    imgneko_zlib_compress_reader_deinit(&compressor);

    // The same pass-through behavior applies to decompression.
    init_status = imgneko_zlib_decompress_reader_init(
        &decompressor, error_source, full_workspace, sizeof(full_workspace));
    if (test_expect_status(ctx, init_status, IMGNEKO_ZLIB_OK,
                           "decompression error initialization"))
        return 1;

    status = imgneko_reader_read(
        imgneko_zlib_decompress_reader_as_reader(&decompressor), out,
        sizeof(out), &len);
    if (test_expect_status(ctx, status, IMGNEKO_READER_ERROR,
                           "decompression source error")) {
        imgneko_zlib_decompress_reader_deinit(&decompressor);
        return 1;
    }
    imgneko_zlib_decompress_reader_deinit(&decompressor);

    return 0;
}

// Verify malformed successful source reads are rejected before zlib receives
// an empty or out-of-bounds input span.
static int test_reader_source_protocol_errors(TestContext *ctx) {
    // A successful source read is invalid if it reports no bytes or more bytes
    // than fit in the workspace supplied to it.
    const size_t reported_lens[] = {0, 65};

    for (size_t i = 0; i < ARRAY_SIZE(reported_lens); ++i) {
        BadOkReader bad_source = {
            .reported_len = reported_lens[i],
        };
        ImgnekoReader source = {
            .read = bad_ok_reader_func,
            .ctx = &bad_source,
        };
        ImgnekoZlibCompressReader compressor = {0};
        ImgnekoZlibDecompressReader decompressor = {0};
        char workspace[64];
        char out[8];
        size_t len = 0;

        // Reject the malformed source result before deflate sees an invalid
        // input span.
        ImgnekoZlibStatus init_status = imgneko_zlib_compress_reader_init(
            &compressor, source, workspace, sizeof(workspace));
        if (test_expect_status(ctx, init_status, IMGNEKO_ZLIB_OK,
                               "protocol compression initialization"))
            return 1;

        int status = imgneko_reader_read(
            imgneko_zlib_compress_reader_as_reader(&compressor), out,
            sizeof(out), &len);
        if (test_expect_status(ctx, status, IMGNEKO_READER_ERROR,
                               "protocol compression source status") ||
            test_expect_size(ctx, len, 0, "protocol compression output size")) {
            imgneko_zlib_compress_reader_deinit(&compressor);
            return 1;
        }
        imgneko_zlib_compress_reader_deinit(&compressor);

        // Inflate is protected by the same generic reader-contract check.
        init_status = imgneko_zlib_decompress_reader_init(
            &decompressor, source, workspace, sizeof(workspace));
        if (test_expect_status(ctx, init_status, IMGNEKO_ZLIB_OK,
                               "protocol decompression initialization"))
            return 1;

        status = imgneko_reader_read(
            imgneko_zlib_decompress_reader_as_reader(&decompressor), out,
            sizeof(out), &len);
        if (test_expect_status(ctx, status, IMGNEKO_READER_ERROR,
                               "protocol decompression source status") ||
            test_expect_size(ctx, len, 0,
                             "protocol decompression output size")) {
            imgneko_zlib_decompress_reader_deinit(&decompressor);
            return 1;
        }
        imgneko_zlib_decompress_reader_deinit(&decompressor);
    }

    return 0;
}

// Verify a source error observed while verifying stream completion is deferred
// until after the complete uncompressed output has been returned.
static int test_decompress_reader_completion_error(TestContext *ctx) {
    StatusAfterDataReader source = {
        .data = ZLIB_HELLO,
        .len = sizeof(ZLIB_HELLO) - 1,
        .final_status = IMGNEKO_READER_ERROR,
    };
    ImgnekoReader source_reader = {
        .read = status_after_data_reader_func,
        .ctx = &source,
    };
    ImgnekoZlibDecompressReader decompressor = {0};
    char workspace[64];
    char out[64];
    size_t len = 0;

    // The source emits the whole compressed stream, then returns a generic
    // error when the decompressor checks that no trailing bytes remain.
    ImgnekoZlibStatus init_status = imgneko_zlib_decompress_reader_init(
        &decompressor, source_reader, workspace, sizeof(workspace));
    if (test_expect_status(ctx, init_status, IMGNEKO_ZLIB_OK,
                           "completion-error initialization"))
        return 1;

    // Preserve the valid decoded bytes produced before completion checking
    // observes the source error.
    memset(out, 'x', sizeof(out));
    int status = imgneko_reader_read(
        imgneko_zlib_decompress_reader_as_reader(&decompressor), out,
        sizeof(out), &len);
    if (test_expect_status(ctx, status, IMGNEKO_READER_OK,
                           "completion-error valid output") ||
        test_expect_buffer_output(ctx, out, len, sizeof(out), STR(RAW_HELLO),
                                  'x', "completion-error valid data")) {
        imgneko_zlib_decompress_reader_deinit(&decompressor);
        return 1;
    }

    // Report the pending source error only after the payload is consumed.
    status = imgneko_reader_read(
        imgneko_zlib_decompress_reader_as_reader(&decompressor), out,
        sizeof(out), &len);
    if (test_expect_status(ctx, status, IMGNEKO_READER_ERROR,
                           "completion-error deferred status") ||
        test_expect_size(ctx, len, 0, "completion-error deferred size")) {
        imgneko_zlib_decompress_reader_deinit(&decompressor);
        return 1;
    }

    imgneko_zlib_decompress_reader_deinit(&decompressor);
    return 0;
}

// Verify completion verification rejects an OK source result with an invalid
// byte count after returning the completed stream's valid output.
static int test_decompress_reader_completion_protocol_errors(TestContext *ctx) {
    // Completion checking must reject both invalid forms of a nominally
    // successful source read: zero bytes and an out-of-bounds byte count.
    const size_t reported_lens[] = {0, 65};

    for (size_t i = 0; i < ARRAY_SIZE(reported_lens); ++i) {
        StatusAfterDataReader source = {
            .data = ZLIB_HELLO,
            .len = sizeof(ZLIB_HELLO) - 1,
            .final_status = IMGNEKO_READER_OK,
            .final_len = reported_lens[i],
        };
        ImgnekoReader source_reader = {
            .read = status_after_data_reader_func,
            .ctx = &source,
        };
        ImgnekoZlibDecompressReader decompressor = {0};
        char workspace[64];
        char out[64];
        size_t len = 0;

        ImgnekoZlibStatus init_status = imgneko_zlib_decompress_reader_init(
            &decompressor, source_reader, workspace, sizeof(workspace));
        if (test_expect_status(ctx, init_status, IMGNEKO_ZLIB_OK,
                               "completion-protocol initialization"))
            return 1;

        // Deliver the valid stream output before surfacing the malformed final
        // source result.
        memset(out, 'x', sizeof(out));
        int status = imgneko_reader_read(
            imgneko_zlib_decompress_reader_as_reader(&decompressor), out,
            sizeof(out), &len);
        if (test_expect_status(ctx, status, IMGNEKO_READER_OK,
                               "completion-protocol valid output") ||
            test_expect_buffer_output(ctx, out, len, sizeof(out),
                                      STR(RAW_HELLO), 'x',
                                      "completion-protocol valid data")) {
            imgneko_zlib_decompress_reader_deinit(&decompressor);
            return 1;
        }

        // The deferred generic error must not expose the source's invalid
        // reported length or modify output.
        memset(out, 'x', sizeof(out));
        status = imgneko_reader_read(
            imgneko_zlib_decompress_reader_as_reader(&decompressor), out,
            sizeof(out), &len);
        if (test_expect_status(ctx, status, IMGNEKO_READER_ERROR,
                               "completion-protocol deferred status") ||
            test_expect_size(ctx, len, 0,
                             "completion-protocol deferred size") ||
            test_expect_buffer_output(ctx, out, len, sizeof(out), "", 0, 'x',
                                      "completion-protocol deferred output")) {
            imgneko_zlib_decompress_reader_deinit(&decompressor);
            return 1;
        }

        imgneko_zlib_decompress_reader_deinit(&decompressor);
    }

    return 0;
}

// Verify a source completion error returns immediately when an empty stream
// leaves no valid decompressed bytes to defer it behind.
static int test_decompress_reader_empty_completion_error(TestContext *ctx) {
    StatusAfterDataReader source = {
        .data = ZLIB_EMPTY,
        .len = sizeof(ZLIB_EMPTY) - 1,
        .final_status = IMGNEKO_READER_ERROR,
    };
    ImgnekoReader source_reader = {
        .read = status_after_data_reader_func,
        .ctx = &source,
    };
    ImgnekoZlibDecompressReader decompressor = {0};
    char workspace[16];
    char out[16];
    size_t len = 0;

    ImgnekoZlibStatus init_status = imgneko_zlib_decompress_reader_init(
        &decompressor, source_reader, workspace, sizeof(workspace));
    if (test_expect_status(ctx, init_status, IMGNEKO_ZLIB_OK,
                           "empty-completion-error initialization"))
        return 1;

    // With no decoded bytes to preserve, completion verification can return
    // the source error immediately on the first read.
    memset(out, 'x', sizeof(out));
    int status = imgneko_reader_read(
        imgneko_zlib_decompress_reader_as_reader(&decompressor), out,
        sizeof(out), &len);
    if (test_expect_status(ctx, status, IMGNEKO_READER_ERROR,
                           "empty-completion-error status") ||
        test_expect_size(ctx, len, 0, "empty-completion-error size") ||
        test_expect_buffer_output(ctx, out, len, sizeof(out), "", 0, 'x',
                                  "empty-completion-error output")) {
        imgneko_zlib_decompress_reader_deinit(&decompressor);
        return 1;
    }

    imgneko_zlib_decompress_reader_deinit(&decompressor);
    return 0;
}

// Verify compression-reader guards reject inert, corrupted, and internally
// failed state before the underlying zlib state can be used unsafely.
static int test_compress_reader_defensive_states(TestContext *ctx) {
    char workspace[16];
    char out[16];
    size_t len = 0;

    // A zero-initialized reader has no live zlib stream and must fail safely.
    {
        ImgnekoZlibCompressReader compressor = {0};
        int status = imgneko_reader_read(
            imgneko_zlib_compress_reader_as_reader(&compressor), out,
            sizeof(out), &len);

        if (test_expect_zlib_reader_error(ctx, status, compressor.error_status,
                                          IMGNEKO_ZLIB_STREAM_ERROR))
            return 1;
    }

    // Corrupt each workspace field independently to exercise both defensive
    // guards instead of relying only on initialization-time validation.
    {
        ImgnekoMemoryReader source = {0};
        ImgnekoZlibCompressReader compressor = {0};

        imgneko_memory_reader_init(&source, STR(RAW_HELLO));
        ImgnekoZlibStatus init_status = imgneko_zlib_compress_reader_init(
            &compressor, imgneko_memory_reader_as_reader(&source), workspace,
            sizeof(workspace));
        if (test_expect_status(ctx, init_status, IMGNEKO_ZLIB_OK,
                               "null-workspace compression initialization")) {
            imgneko_zlib_compress_reader_deinit(&compressor);
            return 1;
        }

        compressor.buffer = NULL;
        int status = imgneko_reader_read(
            imgneko_zlib_compress_reader_as_reader(&compressor), out,
            sizeof(out), &len);
        if (test_expect_status(ctx, status, IMGNEKO_READER_WORKSPACE_TOO_SMALL,
                               "null compression workspace")) {
            imgneko_zlib_compress_reader_deinit(&compressor);
            return 1;
        }

        imgneko_zlib_compress_reader_deinit(&compressor);
    }

    {
        ImgnekoMemoryReader source = {0};
        ImgnekoZlibCompressReader compressor = {0};

        imgneko_memory_reader_init(&source, STR(RAW_HELLO));
        ImgnekoZlibStatus init_status = imgneko_zlib_compress_reader_init(
            &compressor, imgneko_memory_reader_as_reader(&source), workspace,
            sizeof(workspace));
        if (test_expect_status(ctx, init_status, IMGNEKO_ZLIB_OK,
                               "zero-workspace compression initialization")) {
            imgneko_zlib_compress_reader_deinit(&compressor);
            return 1;
        }

        compressor.buffer_cap = 0;
        int status = imgneko_reader_read(
            imgneko_zlib_compress_reader_as_reader(&compressor), out,
            sizeof(out), &len);
        if (test_expect_status(ctx, status, IMGNEKO_READER_WORKSPACE_TOO_SMALL,
                               "zero compression workspace")) {
            imgneko_zlib_compress_reader_deinit(&compressor);
            return 1;
        }

        imgneko_zlib_compress_reader_deinit(&compressor);
    }

    // Temporarily remove zlib's opaque state to force an internal stream
    // failure, then restore it so deinitialization remains valid.
    {
        ImgnekoMemoryReader source = {0};
        ImgnekoZlibCompressReader compressor = {0};

        imgneko_memory_reader_init(&source, NULL, 0);
        ImgnekoZlibStatus init_status = imgneko_zlib_compress_reader_init(
            &compressor, imgneko_memory_reader_as_reader(&source), workspace,
            sizeof(workspace));
        if (test_expect_status(ctx, init_status, IMGNEKO_ZLIB_OK,
                               "corrupted-state compression initialization")) {
            imgneko_zlib_compress_reader_deinit(&compressor);
            return 1;
        }

        void *stream_state = compressor.stream.state;
        compressor.stream.state = NULL;
        int status = imgneko_reader_read(
            imgneko_zlib_compress_reader_as_reader(&compressor), out,
            sizeof(out), &len);
        compressor.stream.state = stream_state;
        if (test_expect_zlib_reader_error(ctx, status, compressor.error_status,
                                          IMGNEKO_ZLIB_STREAM_ERROR) ||
            test_expect_size(ctx, len, 0, "corrupted compression size")) {
            imgneko_zlib_compress_reader_deinit(&compressor);
            return 1;
        }

        // A zlib stream-state failure is sticky: later reads must report the
        // stored transformer error without touching the invalid state again.
        status = imgneko_reader_read(
            imgneko_zlib_compress_reader_as_reader(&compressor), out,
            sizeof(out), &len);
        if (test_expect_zlib_reader_error(ctx, status, compressor.error_status,
                                          IMGNEKO_ZLIB_STREAM_ERROR) ||
            test_expect_size(ctx, len, 0,
                             "corrupted compression sticky size")) {
            imgneko_zlib_compress_reader_deinit(&compressor);
            return 1;
        }

        imgneko_zlib_compress_reader_deinit(&compressor);
    }

    return 0;
}

// Verify decompression-reader guards reject inert, corrupted, and internally
// failed state before the underlying zlib state can be used unsafely.
static int test_decompress_reader_defensive_states(TestContext *ctx) {
    char workspace[32];
    char out[32];
    size_t len = 0;

    // A zero-initialized reader has no live inflate state and must fail safely.
    {
        ImgnekoZlibDecompressReader decompressor = {0};
        int status = imgneko_reader_read(
            imgneko_zlib_decompress_reader_as_reader(&decompressor), out,
            sizeof(out), &len);

        if (test_expect_zlib_reader_error(ctx, status,
                                          decompressor.error_status,
                                          IMGNEKO_ZLIB_STREAM_ERROR))
            return 1;
    }

    // Exercise the runtime guards for a missing workspace pointer and a zero
    // workspace capacity separately.
    {
        ImgnekoMemoryReader source = {0};
        ImgnekoZlibDecompressReader decompressor = {0};

        imgneko_memory_reader_init(&source, ZLIB_HELLO, sizeof(ZLIB_HELLO) - 1);
        ImgnekoZlibStatus init_status = imgneko_zlib_decompress_reader_init(
            &decompressor, imgneko_memory_reader_as_reader(&source), workspace,
            sizeof(workspace));
        if (test_expect_status(ctx, init_status, IMGNEKO_ZLIB_OK,
                               "null-workspace decompression initialization")) {
            imgneko_zlib_decompress_reader_deinit(&decompressor);
            return 1;
        }

        decompressor.buffer = NULL;
        int status = imgneko_reader_read(
            imgneko_zlib_decompress_reader_as_reader(&decompressor), out,
            sizeof(out), &len);
        if (test_expect_status(ctx, status, IMGNEKO_READER_WORKSPACE_TOO_SMALL,
                               "null decompression workspace")) {
            imgneko_zlib_decompress_reader_deinit(&decompressor);
            return 1;
        }

        imgneko_zlib_decompress_reader_deinit(&decompressor);
    }

    {
        ImgnekoMemoryReader source = {0};
        ImgnekoZlibDecompressReader decompressor = {0};

        imgneko_memory_reader_init(&source, ZLIB_HELLO, sizeof(ZLIB_HELLO) - 1);
        ImgnekoZlibStatus init_status = imgneko_zlib_decompress_reader_init(
            &decompressor, imgneko_memory_reader_as_reader(&source), workspace,
            sizeof(workspace));
        if (test_expect_status(ctx, init_status, IMGNEKO_ZLIB_OK,
                               "zero-workspace decompression initialization")) {
            imgneko_zlib_decompress_reader_deinit(&decompressor);
            return 1;
        }

        decompressor.buffer_cap = 0;
        int status = imgneko_reader_read(
            imgneko_zlib_decompress_reader_as_reader(&decompressor), out,
            sizeof(out), &len);
        if (test_expect_status(ctx, status, IMGNEKO_READER_WORKSPACE_TOO_SMALL,
                               "zero decompression workspace")) {
            imgneko_zlib_decompress_reader_deinit(&decompressor);
            return 1;
        }

        imgneko_zlib_decompress_reader_deinit(&decompressor);
    }

    // Force inflate's internal stream error without leaking its real state.
    {
        ImgnekoMemoryReader source = {0};
        ImgnekoZlibDecompressReader decompressor = {0};

        imgneko_memory_reader_init(&source, ZLIB_HELLO, sizeof(ZLIB_HELLO) - 1);
        ImgnekoZlibStatus init_status = imgneko_zlib_decompress_reader_init(
            &decompressor, imgneko_memory_reader_as_reader(&source), workspace,
            sizeof(workspace));
        if (test_expect_status(
                ctx, init_status, IMGNEKO_ZLIB_OK,
                "corrupted-state decompression initialization")) {
            imgneko_zlib_decompress_reader_deinit(&decompressor);
            return 1;
        }

        void *stream_state = decompressor.stream.state;
        decompressor.stream.state = NULL;
        int status = imgneko_reader_read(
            imgneko_zlib_decompress_reader_as_reader(&decompressor), out,
            sizeof(out), &len);
        decompressor.stream.state = stream_state;
        if (test_expect_zlib_reader_error(ctx, status,
                                          decompressor.error_status,
                                          IMGNEKO_ZLIB_STREAM_ERROR) ||
            test_expect_size(ctx, len, 0, "corrupted decompression size")) {
            imgneko_zlib_decompress_reader_deinit(&decompressor);
            return 1;
        }

        // As with compression, the detailed stream failure must remain sticky
        // without another call into the deliberately corrupted zlib state.
        status = imgneko_reader_read(
            imgneko_zlib_decompress_reader_as_reader(&decompressor), out,
            sizeof(out), &len);
        if (test_expect_zlib_reader_error(ctx, status,
                                          decompressor.error_status,
                                          IMGNEKO_ZLIB_STREAM_ERROR) ||
            test_expect_size(ctx, len, 0,
                             "corrupted decompression sticky size")) {
            imgneko_zlib_decompress_reader_deinit(&decompressor);
            return 1;
        }

        imgneko_zlib_decompress_reader_deinit(&decompressor);
    }

    return 0;
}

// Verify the compressor can finish when source EOF is already known, both with
// no buffered input and with a valid preloaded input byte.
static int test_compress_reader_prebuffered_input(TestContext *ctx) {
    const char raw_byte[] = "x";
    ImgnekoZlibCompressReader compressor = {0};
    char workspace[16];
    char recovered[2];
    String compressed = str_empty;
    int result = 0;

    ImgnekoZlibStatus init_status = imgneko_zlib_compress_reader_init(
        &compressor, (ImgnekoReader){0}, workspace, sizeof(workspace));
    if (test_expect_status(ctx, init_status, IMGNEKO_ZLIB_OK,
                           "prebuffered-empty compression initialization")) {
        result = 1;
        goto cleanup;
    }

    // Mark EOF before any source read. The compressor must still emit a valid
    // empty RFC 1950 stream rather than requiring an underlying reader.
    compressor.source_eof = true;
    result = test_drain_reader(
        ctx, imgneko_zlib_compress_reader_as_reader(&compressor),
        /*chunk_size=*/8, &compressed);
    if (result != 0)
        goto cleanup;

    uLongf recovered_len = sizeof(recovered);
    int zstatus = uncompress((Bytef *)recovered, &recovered_len,
                             (const Bytef *)compressed.cstr, compressed.len);
    if (zstatus != Z_OK || recovered_len != 0) {
        result = test_fail_message(ctx, "prebuffered empty stream output");
        goto cleanup;
    }

    str_free(compressed);
    imgneko_zlib_compress_reader_deinit(&compressor);

    init_status = imgneko_zlib_compress_reader_init(
        &compressor, (ImgnekoReader){0}, workspace, sizeof(workspace));
    if (test_expect_status(ctx, init_status, IMGNEKO_ZLIB_OK,
                           "prebuffered-byte compression initialization")) {
        result = 1;
        goto cleanup;
    }

    // Preload zlib's input fields and mark source EOF to exercise finishing
    // buffered input without consulting the empty source reader.
    compressor.stream.next_in = (Bytef *)raw_byte;
    compressor.stream.avail_in = sizeof(raw_byte) - 1;
    compressor.source_eof = true;
    result = test_drain_reader(
        ctx, imgneko_zlib_compress_reader_as_reader(&compressor),
        /*chunk_size=*/8, &compressed);
    if (result != 0)
        goto cleanup;

    recovered_len = sizeof(recovered);
    zstatus = uncompress((Bytef *)recovered, &recovered_len,
                         (const Bytef *)compressed.cstr, compressed.len);
    if (zstatus != Z_OK || recovered_len != sizeof(raw_byte) - 1 ||
        memcmp(recovered, raw_byte, sizeof(raw_byte) - 1) != 0) {
        result = test_fail_message(ctx, "prebuffered byte stream output");
    }

cleanup:
    str_free(compressed);
    imgneko_zlib_compress_reader_deinit(&compressor);
    return result;
}

// Verify the decompressor accepts a complete stream already held in zlib's
// input fields when the underlying source has already reached EOF.
static int test_decompress_reader_prebuffered_input(TestContext *ctx) {
    ImgnekoZlibDecompressReader decompressor = {0};
    char workspace[32];
    char out[32];
    size_t len = 0;

    ImgnekoZlibStatus init_status = imgneko_zlib_decompress_reader_init(
        &decompressor, (ImgnekoReader){0}, workspace, sizeof(workspace));
    if (test_expect_status(ctx, init_status, IMGNEKO_ZLIB_OK,
                           "prebuffered decompression initialization"))
        return 1;

    // Bypass the source reader by placing a complete stream directly in
    // zlib's input fields and marking the source exhausted.
    decompressor.stream.next_in = (Bytef *)ZLIB_HELLO;
    decompressor.stream.avail_in = sizeof(ZLIB_HELLO) - 1;
    decompressor.source_eof = true;

    // The first call consumes the preloaded stream and returns its payload.
    memset(out, 'x', sizeof(out));
    int status = imgneko_reader_read(
        imgneko_zlib_decompress_reader_as_reader(&decompressor), out,
        sizeof(out), &len);
    if (test_expect_status(ctx, status, IMGNEKO_READER_OK,
                           "prebuffered decompression output") ||
        test_expect_buffer_output(ctx, out, len, sizeof(out), STR(RAW_HELLO),
                                  'x', "prebuffered decompression data")) {
        imgneko_zlib_decompress_reader_deinit(&decompressor);
        return 1;
    }

    // Completion remains sticky after the preloaded input is exhausted.
    status = imgneko_reader_read(
        imgneko_zlib_decompress_reader_as_reader(&decompressor), out,
        sizeof(out), &len);
    if (test_expect_status(ctx, status, IMGNEKO_READER_EOF,
                           "prebuffered decompression EOF") ||
        test_expect_size(ctx, len, 0, "prebuffered decompression EOF size")) {
        imgneko_zlib_decompress_reader_deinit(&decompressor);
        return 1;
    }

    imgneko_zlib_decompress_reader_deinit(&decompressor);
    return 0;
}

// Verify truncated prebuffered input reports an error after any valid output,
// regardless of whether the caller's output storage fills first.
static int
test_decompress_reader_prebuffered_truncated_input(TestContext *ctx) {
    char truncated[sizeof(ZLIB_HELLO) - 2];
    char workspace[32];
    char out[32];
    size_t len = 0;

    // Preserve the payload but remove the final checksum byte.
    memcpy(truncated, ZLIB_HELLO, sizeof(truncated));

    // With ample output space, inflate returns the full payload first and the
    // reader reports truncation on the following call.
    {
        ImgnekoZlibDecompressReader decompressor = {0};
        ImgnekoZlibStatus init_status = imgneko_zlib_decompress_reader_init(
            &decompressor, (ImgnekoReader){0}, workspace, sizeof(workspace));
        if (test_expect_status(ctx, init_status, IMGNEKO_ZLIB_OK,
                               "prebuffered-truncated initialization"))
            return 1;

        decompressor.stream.next_in = (Bytef *)truncated;
        decompressor.stream.avail_in = sizeof(truncated);
        decompressor.source_eof = true;

        memset(out, 'x', sizeof(out));
        int status = imgneko_reader_read(
            imgneko_zlib_decompress_reader_as_reader(&decompressor), out,
            sizeof(out), &len);
        if (test_expect_status(ctx, status, IMGNEKO_READER_OK,
                               "prebuffered-truncated valid output") ||
            test_expect_buffer_output(ctx, out, len, sizeof(out),
                                      STR(RAW_HELLO), 'x',
                                      "prebuffered-truncated valid data")) {
            imgneko_zlib_decompress_reader_deinit(&decompressor);
            return 1;
        }

        status = imgneko_reader_read(
            imgneko_zlib_decompress_reader_as_reader(&decompressor), out,
            sizeof(out), &len);
        if (test_expect_zlib_reader_error(ctx, status,
                                          decompressor.error_status,
                                          IMGNEKO_ZLIB_TRUNCATED_INPUT) ||
            test_expect_size(ctx, len, 0,
                             "prebuffered-truncated status size")) {
            imgneko_zlib_decompress_reader_deinit(&decompressor);
            return 1;
        }

        imgneko_zlib_decompress_reader_deinit(&decompressor);
    }

    // With a one-byte output buffer, output capacity wins before checksum
    // validation, so the first byte is returned without a premature error.
    {
        ImgnekoZlibDecompressReader decompressor = {0};
        ImgnekoZlibStatus init_status = imgneko_zlib_decompress_reader_init(
            &decompressor, (ImgnekoReader){0}, workspace, sizeof(workspace));
        if (test_expect_status(ctx, init_status, IMGNEKO_ZLIB_OK,
                               "prebuffered-small-output initialization"))
            return 1;

        decompressor.stream.next_in = (Bytef *)truncated;
        decompressor.stream.avail_in = sizeof(truncated);
        decompressor.source_eof = true;

        memset(out, 'x', sizeof(out));
        int status = imgneko_reader_read(
            imgneko_zlib_decompress_reader_as_reader(&decompressor), out,
            /*out_cap=*/1, &len);
        if (test_expect_status(ctx, status, IMGNEKO_READER_OK,
                               "prebuffered-small-output status") ||
            test_expect_buffer_output(ctx, out, len, sizeof(out), RAW_HELLO,
                                      /*expected_len=*/1, 'x',
                                      "prebuffered-small-output data")) {
            imgneko_zlib_decompress_reader_deinit(&decompressor);
            return 1;
        }

        imgneko_zlib_decompress_reader_deinit(&decompressor);
    }

    return 0;
}

// Verify a compressor can call zlib with no input after writing its wrapper
// header, then fetch source input and complete the stream.
static int test_compress_reader_no_input_call(TestContext *ctx) {
    ImgnekoMemoryReader source = {0};
    ImgnekoZlibCompressReader compressor = {0};
    char workspace[16];
    char out[16];
    String compressed = str_empty;
    size_t len = 0;
    int result = 0;

    imgneko_memory_reader_init(&source, "x", 1);
    ImgnekoZlibStatus init_status = imgneko_zlib_compress_reader_init(
        &compressor, imgneko_memory_reader_as_reader(&source), workspace,
        sizeof(workspace));
    if (test_expect_status(ctx, init_status, IMGNEKO_ZLIB_OK,
                           "no-input-call compression initialization")) {
        result = 1;
        goto cleanup;
    }

    // Pretend zlib has pending work. The first call should emit only wrapper
    // bytes and leave the source untouched.
    compressor.needs_input = false;
    int status =
        imgneko_reader_read(imgneko_zlib_compress_reader_as_reader(&compressor),
                            out, sizeof(out), &len);
    if (test_expect_status(ctx, status, IMGNEKO_READER_OK,
                           "no-input-call compression header") ||
        test_expect_size(ctx, source.offset, 0,
                         "no-input-call source offset before retry")) {
        result = 1;
        goto cleanup;
    }

    // Repeat the unusual state after the header. With no pending output left,
    // the implementation must recover by reading the source.
    compressor.needs_input = false;
    status =
        imgneko_reader_read(imgneko_zlib_compress_reader_as_reader(&compressor),
                            out, sizeof(out), &len);
    if (test_expect_status(ctx, status, IMGNEKO_READER_OK,
                           "no-input-call compression retry") ||
        test_expect_size(ctx, source.offset, 1,
                         "no-input-call source offset after retry")) {
        result = 1;
        goto cleanup;
    }

    // Preserve bytes returned by the manual calls, then drain the remainder to
    // ensure the recovered state can finish a valid stream.
    str_append_data(compressed, out, len);
    result = test_drain_reader(
        ctx, imgneko_zlib_compress_reader_as_reader(&compressor),
        /*chunk_size=*/8, &compressed);

cleanup:
    str_free(compressed);
    imgneko_zlib_compress_reader_deinit(&compressor);
    return result;
}

// Verify a decompressor recovers from an initial zlib call with no input and
// continues after obtaining the RFC 1950 input stream from its source.
static int test_decompress_reader_no_input_call(TestContext *ctx) {
    ImgnekoMemoryReader source = {0};
    ImgnekoZlibDecompressReader decompressor = {0};
    char workspace[32];
    char out[32];
    size_t len = 0;

    imgneko_memory_reader_init(&source, ZLIB_HELLO, sizeof(ZLIB_HELLO) - 1);
    ImgnekoZlibStatus init_status = imgneko_zlib_decompress_reader_init(
        &decompressor, imgneko_memory_reader_as_reader(&source), workspace,
        sizeof(workspace));
    if (test_expect_status(ctx, init_status, IMGNEKO_ZLIB_OK,
                           "no-input-call decompression initialization"))
        return 1;

    // Force an initial inflate call without buffered input. Z_BUF_ERROR should
    // cause a source refill and retry rather than a truncation failure.
    decompressor.needs_input = false;
    memset(out, 'x', sizeof(out));
    int status = imgneko_reader_read(
        imgneko_zlib_decompress_reader_as_reader(&decompressor), out,
        sizeof(out), &len);
    if (test_expect_status(ctx, status, IMGNEKO_READER_OK,
                           "no-input-call decompression output") ||
        test_expect_buffer_output(ctx, out, len, sizeof(out), STR(RAW_HELLO),
                                  'x', "no-input-call decompression data") ||
        test_expect_size(ctx, source.offset, sizeof(ZLIB_HELLO) - 1,
                         "no-input-call compressed source offset")) {
        imgneko_zlib_decompress_reader_deinit(&decompressor);
        return 1;
    }

    imgneko_zlib_decompress_reader_deinit(&decompressor);
    return 0;
}

// Verify an already-exhausted decompressor with no buffered input reports a
// generic reader error with the truncated-stream status rather than invoking
// its source reader.
static int test_decompress_reader_known_eof_without_input(TestContext *ctx) {
    ImgnekoZlibDecompressReader decompressor = {0};
    char workspace[16];
    char out[16];
    size_t len = 0;

    ImgnekoZlibStatus init_status = imgneko_zlib_decompress_reader_init(
        &decompressor, (ImgnekoReader){0}, workspace, sizeof(workspace));
    if (test_expect_status(ctx, init_status, IMGNEKO_ZLIB_OK,
                           "known-EOF decompression initialization"))
        return 1;

    // Mark EOF without providing buffered bytes. This is the exact state in
    // which inflate can no longer reach a valid stream end.
    decompressor.source_eof = true;
    memset(out, 'x', sizeof(out));
    int status = imgneko_reader_read(
        imgneko_zlib_decompress_reader_as_reader(&decompressor), out,
        sizeof(out), &len);
    if (test_expect_zlib_reader_error(ctx, status, decompressor.error_status,
                                      IMGNEKO_ZLIB_TRUNCATED_INPUT) ||
        test_expect_size(ctx, len, 0, "known-EOF decompression size") ||
        test_expect_buffer_output(ctx, out, len, sizeof(out), "", 0, 'x',
                                  "known-EOF decompression output")) {
        imgneko_zlib_decompress_reader_deinit(&decompressor);
        return 1;
    }

    imgneko_zlib_decompress_reader_deinit(&decompressor);
    return 0;
}

// Verify initialization rejects unusable workspaces and error strings are
// available for every public zlib status.
static int test_initialization_and_error_strings(TestContext *ctx) {
    ImgnekoZlibCompressReader compressor = {0};
    ImgnekoZlibDecompressReader decompressor = {0};
    char workspace[1];
    char out[1];
    size_t len = 0;
    const ImgnekoZlibStatus statuses[] = {
        IMGNEKO_ZLIB_OK,
        IMGNEKO_ZLIB_INVALID_ARGUMENT,
        IMGNEKO_ZLIB_STREAM_ERROR,
        IMGNEKO_ZLIB_INVALID_INPUT,
        IMGNEKO_ZLIB_TRUNCATED_INPUT,
        IMGNEKO_ZLIB_TRAILING_INPUT,
        IMGNEKO_ZLIB_DICTIONARY_REQUIRED,
        (ImgnekoZlibStatus)12345,
    };

    // A NULL workspace makes initialization fail, and the resulting generic
    // reader view must expose the same failure as a sticky reader error.
    ImgnekoZlibStatus status = imgneko_zlib_compress_reader_init(
        &compressor, (ImgnekoReader){0}, NULL, 0);
    if (test_expect_status(ctx, status, IMGNEKO_ZLIB_INVALID_ARGUMENT,
                           "invalid compression workspace") ||
        test_expect_status(ctx, compressor.error_status,
                           IMGNEKO_ZLIB_INVALID_ARGUMENT,
                           "invalid compression error status")) {
        return 1;
    }
    int reader_status =
        imgneko_reader_read(imgneko_zlib_compress_reader_as_reader(&compressor),
                            out, sizeof(out), &len);
    if (test_expect_zlib_reader_error(ctx, reader_status,
                                      compressor.error_status,
                                      IMGNEKO_ZLIB_INVALID_ARGUMENT)) {
        return 1;
    }
    imgneko_zlib_compress_reader_deinit(&compressor);

    // A non-NULL pointer with zero capacity is invalid independently of NULL.
    status = imgneko_zlib_compress_reader_init(&compressor, (ImgnekoReader){0},
                                               workspace, 0);
    if (test_expect_status(ctx, status, IMGNEKO_ZLIB_INVALID_ARGUMENT,
                           "zero-capacity compression workspace") ||
        test_expect_status(ctx, compressor.error_status,
                           IMGNEKO_ZLIB_INVALID_ARGUMENT,
                           "zero-capacity compression error status")) {
        return 1;
    }
    imgneko_zlib_compress_reader_deinit(&compressor);

    // Confirm the smallest representable workspace still initializes normally.
    status = imgneko_zlib_compress_reader_init(&compressor, (ImgnekoReader){0},
                                               workspace, sizeof(workspace));
    if (test_expect_status(ctx, status, IMGNEKO_ZLIB_OK,
                           "valid compression initialization") ||
        test_expect_status(ctx, compressor.error_status, IMGNEKO_ZLIB_OK,
                           "valid compression error status")) {
        return 1;
    }
    imgneko_zlib_compress_reader_deinit(&compressor);

    // Repeat the invalid-pointer, zero-capacity, and minimal-valid cases for
    // decompression, including its failed generic reader view.
    status = imgneko_zlib_decompress_reader_init(&decompressor,
                                                 (ImgnekoReader){0}, NULL, 0);
    if (test_expect_status(ctx, status, IMGNEKO_ZLIB_INVALID_ARGUMENT,
                           "invalid decompression workspace") ||
        test_expect_status(ctx, decompressor.error_status,
                           IMGNEKO_ZLIB_INVALID_ARGUMENT,
                           "invalid decompression error status")) {
        return 1;
    }
    reader_status = imgneko_reader_read(
        imgneko_zlib_decompress_reader_as_reader(&decompressor), out,
        sizeof(out), &len);
    if (test_expect_zlib_reader_error(ctx, reader_status,
                                      decompressor.error_status,
                                      IMGNEKO_ZLIB_INVALID_ARGUMENT)) {
        return 1;
    }
    imgneko_zlib_decompress_reader_deinit(&decompressor);

    status = imgneko_zlib_decompress_reader_init(
        &decompressor, (ImgnekoReader){0}, workspace, 0);
    if (test_expect_status(ctx, status, IMGNEKO_ZLIB_INVALID_ARGUMENT,
                           "zero-capacity decompression workspace") ||
        test_expect_status(ctx, decompressor.error_status,
                           IMGNEKO_ZLIB_INVALID_ARGUMENT,
                           "zero-capacity decompression error status")) {
        return 1;
    }
    imgneko_zlib_decompress_reader_deinit(&decompressor);

    status = imgneko_zlib_decompress_reader_init(
        &decompressor, (ImgnekoReader){0}, workspace, sizeof(workspace));
    if (test_expect_status(ctx, status, IMGNEKO_ZLIB_OK,
                           "valid decompression initialization") ||
        test_expect_status(ctx, decompressor.error_status, IMGNEKO_ZLIB_OK,
                           "valid decompression error status")) {
        return 1;
    }
    imgneko_zlib_decompress_reader_deinit(&decompressor);

    // Public and unknown status values must always produce usable diagnostics.
    for (size_t i = 0; i < ARRAY_SIZE(statuses); ++i) {
        const char *message = imgneko_zlib_error_string(statuses[i]);
        if (message == NULL || message[0] == '\0') {
            fprintf(stderr, "%s: empty error string at %zu\n", ctx->test_name,
                    i);
            return 1;
        }
    }

    return 0;
}

int main(int argc, char **argv) {
    const Subtest subtests[] = {
        PREFIXED_TEST(test_compress_reader_rfc1950),
        PREFIXED_TEST(test_decompress_reader_streaming),
        PREFIXED_TEST(test_empty_streams),
        PREFIXED_TEST(test_stacked_reader_round_trip),
        PREFIXED_TEST(test_stacked_reader_pseudorandom_round_trip),
        PREFIXED_TEST(test_reader_zero_capacity_retry),
        PREFIXED_TEST(test_reader_oversized_workspace),
        PREFIXED_TEST(test_decompress_reader_input_errors),
        PREFIXED_TEST(test_decompress_reader_immediate_invalid_input),
        PREFIXED_TEST(test_decompress_reader_trailing_source_data),
        PREFIXED_TEST(test_decompress_reader_dictionary_required),
        PREFIXED_TEST(test_reader_source_failures),
        PREFIXED_TEST(test_reader_source_protocol_errors),
        PREFIXED_TEST(test_decompress_reader_completion_error),
        PREFIXED_TEST(test_decompress_reader_completion_protocol_errors),
        PREFIXED_TEST(test_decompress_reader_empty_completion_error),
        PREFIXED_TEST(test_compress_reader_defensive_states),
        PREFIXED_TEST(test_decompress_reader_defensive_states),
        PREFIXED_TEST(test_compress_reader_prebuffered_input),
        PREFIXED_TEST(test_decompress_reader_prebuffered_input),
        PREFIXED_TEST(test_decompress_reader_prebuffered_truncated_input),
        PREFIXED_TEST(test_compress_reader_no_input_call),
        PREFIXED_TEST(test_decompress_reader_no_input_call),
        PREFIXED_TEST(test_decompress_reader_known_eof_without_input),
        PREFIXED_TEST(test_initialization_and_error_strings),
    };

    return run_subtests(argc, argv, subtests, ARRAY_SIZE(subtests));
}
