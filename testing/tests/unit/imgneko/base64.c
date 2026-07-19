// SPDX-License-Identifier: MIT-0

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "imgneko/base64.h"
#include "test_main.h"
#include "test_reader.h"
#include "util/common.h"
#include "util/string.h"

#define STR(text) (text), (sizeof(text) - 1)

typedef struct Base64Vector {
    const char *raw;
    size_t raw_len;
    const char *encoded;
    size_t encoded_len;
} Base64Vector;

typedef struct StatusAfterDataReader {
    const char *data;
    size_t len;
    bool emitted;
    ImgnekoReaderStatus final_status;
} StatusAfterDataReader;

typedef struct BadOkReader {
    size_t reported_len;
} BadOkReader;

// Reader callback that always reports a standard source failure.
static ImgnekoReaderStatus error_reader_func(void *ctx, char *out,
                                             size_t out_cap, size_t *len_out) {
    (void)ctx;
    (void)out;
    (void)out_cap;

    *len_out = 0;
    return IMGNEKO_READER_ERROR;
}

// Generate deterministic pseudo-random values for repeatable stress tests.
static uint32_t next_test_random(uint32_t *state) {
    uint32_t value = *state;

    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;

    *state = value;
    return value;
}

// Reader callback that emits one data chunk and then returns a configured
// status. This lets tests exercise decoder verification after padded input.
static ImgnekoReaderStatus status_after_data_reader_func(void *ctx, char *out,
                                                         size_t out_cap,
                                                         size_t *len_out) {
    StatusAfterDataReader *reader = ctx;

    if (len_out == NULL)
        return IMGNEKO_READER_ERROR;
    *len_out = 0;

    if (reader == NULL || (out == NULL && out_cap != 0))
        return IMGNEKO_READER_ERROR;

    if (reader->emitted)
        return reader->final_status;

    if (out_cap < reader->len) {
        *len_out = reader->len;
        return IMGNEKO_READER_BUFFER_TOO_SMALL;
    }

    memcpy(out, reader->data, reader->len);
    *len_out = reader->len;
    reader->emitted = true;
    return IMGNEKO_READER_OK;
}

// Reader callback that violates the reader protocol by reporting an OK read
// with a caller-selected length.
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

// Verify standard base64 encode vectors, including padding boundaries.
static int test_encode_vectors(TestContext *ctx) {
    const Base64Vector vectors[] = {
        {STR(""), STR("")},
        {STR("f"), STR("Zg==")},
        {STR("fo"), STR("Zm8=")},
        {STR("foo"), STR("Zm9v")},
        {STR("foob"), STR("Zm9vYg==")},
        {STR("fooba"), STR("Zm9vYmE=")},
        {STR("foobar"), STR("Zm9vYmFy")},
        {STR("hello world"), STR("aGVsbG8gd29ybGQ=")},
    };

    for (size_t i = 0; i < ARRAY_SIZE(vectors); ++i) {
        char out[64];
        size_t len = 0;
        ImgnekoBase64Status status;

        status = imgneko_base64_encoded_len(vectors[i].raw_len, &len);
        if (test_expect_status(ctx, status, IMGNEKO_BASE64_OK,
                               "encoded length") ||
            test_expect_size(ctx, len, vectors[i].encoded_len,
                             "encoded length value"))
            return 1;

        memset(out, 'x', sizeof(out));
        status = imgneko_base64_encode(vectors[i].raw, vectors[i].raw_len, out,
                                       sizeof(out), &len);
        if (test_expect_status(ctx, status, IMGNEKO_BASE64_OK, "encode") ||
            test_expect_size(ctx, len, vectors[i].encoded_len,
                             "encoded size") ||
            test_expect_buffer_output(
                ctx, out, len, sizeof(out), vectors[i].encoded,
                vectors[i].encoded_len, 'x', "encoded data"))
            return 1;
    }

    return 0;
}

// Verify standard base64 decode vectors, including padding boundaries.
static int test_decode_vectors(TestContext *ctx) {
    const Base64Vector vectors[] = {
        {STR(""), STR("")},
        {STR("f"), STR("Zg==")},
        {STR("fo"), STR("Zm8=")},
        {STR("foo"), STR("Zm9v")},
        {STR("foob"), STR("Zm9vYg==")},
        {STR("fooba"), STR("Zm9vYmE=")},
        {STR("foobar"), STR("Zm9vYmFy")},
        {STR("hello world"), STR("aGVsbG8gd29ybGQ=")},
    };

    for (size_t i = 0; i < ARRAY_SIZE(vectors); ++i) {
        char out[64];
        size_t len = 0;
        ImgnekoBase64Status status;

        status = imgneko_base64_decoded_len(vectors[i].encoded,
                                            vectors[i].encoded_len, &len);
        if (test_expect_status(ctx, status, IMGNEKO_BASE64_OK,
                               "decoded length") ||
            test_expect_size(ctx, len, vectors[i].raw_len,
                             "decoded length value"))
            return 1;

        memset(out, 'x', sizeof(out));
        status = imgneko_base64_decode(
            vectors[i].encoded, vectors[i].encoded_len, out, sizeof(out), &len);
        if (test_expect_status(ctx, status, IMGNEKO_BASE64_OK, "decode") ||
            test_expect_size(ctx, len, vectors[i].raw_len, "decoded size") ||
            test_expect_buffer_output(ctx, out, len, sizeof(out),
                                      vectors[i].raw, vectors[i].raw_len, 'x',
                                      "decoded data"))
            return 1;
    }

    return 0;
}

// Verify helper buffer sizing, overflow detection, and strict decode failures.
static int test_helper_failures(TestContext *ctx) {
    const char *invalid_length_inputs[] = {
        "Zg=", "Z===", "Zg=A", "Zm9v=", "Zm 8=",
    };
    const char *invalid_decode_inputs[] = {
        "Zg=",   "Z===", "Zg=A", "Zm9v=", "Zh==", "Zm9=",
        "Zm 8=", "{m9v", "Z!!!", "Zm!v",  "Zm9!",
    };
    char out[4];
    size_t len = 0;
    ImgnekoBase64Status status;

    memset(out, 'x', sizeof(out));

    status = imgneko_base64_encoded_len(SIZE_MAX, &len);
    if (test_expect_status(ctx, status, IMGNEKO_BASE64_OVERFLOW,
                           "encoded length overflow"))
        return 1;

    status = imgneko_base64_encoded_len((SIZE_MAX / 4) * 3 + 1, &len);
    if (test_expect_status(ctx, status, IMGNEKO_BASE64_OVERFLOW,
                           "encoded length remainder overflow"))
        return 1;

    status = imgneko_base64_encode("", SIZE_MAX, out, sizeof(out), &len);
    if (test_expect_status(ctx, status, IMGNEKO_BASE64_OVERFLOW,
                           "encode overflow") ||
        test_expect_data(ctx, out, sizeof(out), STR("xxxx"),
                         "encode overflow leaves output alone"))
        return 1;

    status = imgneko_base64_encode(STR("foo"), out, 3, &len);
    if (test_expect_status(ctx, status, IMGNEKO_BASE64_BUFFER_TOO_SMALL,
                           "encode capacity failure") ||
        test_expect_size(ctx, len, 4, "encode retry size") ||
        test_expect_data(ctx, out, sizeof(out), STR("xxxx"),
                         "encode leaves output alone"))
        return 1;

    status = imgneko_base64_decode(STR("Zm9v"), out, 2, &len);
    if (test_expect_status(ctx, status, IMGNEKO_BASE64_BUFFER_TOO_SMALL,
                           "decode capacity failure") ||
        test_expect_size(ctx, len, 3, "decode retry size") ||
        test_expect_data(ctx, out, sizeof(out), STR("xxxx"),
                         "decode small output leaves output alone"))
        return 1;

    status = imgneko_base64_decode(STR("!!!!"), out, 2, &len);
    if (test_expect_status(ctx, status, IMGNEKO_BASE64_BUFFER_TOO_SMALL,
                           "decode small buffer before validation") ||
        test_expect_size(ctx, len, 3, "decode invalid input retry size") ||
        test_expect_data(ctx, out, sizeof(out), STR("xxxx"),
                         "decode invalid small output leaves output alone"))
        return 1;

    for (size_t i = 0; i < ARRAY_SIZE(invalid_length_inputs); ++i) {
        status = imgneko_base64_decoded_len(
            invalid_length_inputs[i], strlen(invalid_length_inputs[i]), &len);
        if (test_expect_status(ctx, status, IMGNEKO_BASE64_INVALID_INPUT,
                               "invalid decoded length"))
            return 1;
    }

    status = imgneko_base64_decoded_len(STR("Zh=="), &len);
    if (test_expect_status(ctx, status, IMGNEKO_BASE64_OK,
                           "decoded length skips padding-bit validation") ||
        test_expect_size(ctx, len, 1,
                         "decoded length with invalid padding bits"))
        return 1;

    status = imgneko_base64_decoded_len(STR("!!!!"), &len);
    if (test_expect_status(ctx, status, IMGNEKO_BASE64_OK,
                           "decoded length skips alphabet validation") ||
        test_expect_size(ctx, len, 3, "decoded length with invalid alphabet"))
        return 1;

    for (size_t i = 0; i < ARRAY_SIZE(invalid_decode_inputs); ++i) {
        char invalid_out[8];

        memset(invalid_out, 'x', sizeof(invalid_out));
        status = imgneko_base64_decode(invalid_decode_inputs[i],
                                       strlen(invalid_decode_inputs[i]),
                                       invalid_out, sizeof(invalid_out), &len);
        if (test_expect_status(ctx, status, IMGNEKO_BASE64_INVALID_INPUT,
                               "invalid decode") ||
            test_expect_data(ctx, invalid_out, sizeof(invalid_out),
                             STR("xxxxxxxx"),
                             "invalid decode leaves output alone"))
            return 1;
    }

    char padded_then_data_out[8];

    status = imgneko_base64_decode(STR("Zg==AAAA"), padded_then_data_out,
                                   sizeof(padded_then_data_out), &len);
    if (test_expect_status(ctx, status, IMGNEKO_BASE64_INVALID_INPUT,
                           "decode rejects data after padding"))
        return 1;

    return 0;
}

// Verify encode transformer padding and EOF behavior for short inputs.
static int test_encode_reader_padding(TestContext *ctx) {
    const Base64Vector vectors[] = {
        {STR(""), STR("")},
        {STR("f"), STR("Zg==")},
        {STR("fo"), STR("Zm8=")},
        {STR("foo"), STR("Zm9v")},
    };

    for (size_t i = 0; i < ARRAY_SIZE(vectors); ++i) {
        ImgnekoMemoryReader memory = {0};
        ImgnekoBase64EncodeReader encoder = {0};
        char workspace[12];
        String output = str_empty;
        int result = 0;

        imgneko_memory_reader_init(&memory, vectors[i].raw, vectors[i].raw_len);
        imgneko_base64_encode_reader_init(
            &encoder, imgneko_memory_reader_as_reader(&memory), workspace,
            sizeof(workspace));

        result = test_drain_reader(
            ctx, imgneko_base64_encode_reader_as_reader(&encoder), 4, &output);
        if (result == 0)
            result = test_expect_data(
                ctx, output.cstr, output.len, vectors[i].encoded,
                vectors[i].encoded_len, "encode reader output");

        str_free(output);
        if (result != 0)
            return result;
    }

    return 0;
}

// Verify encode transformer retry behavior before it consumes source bytes.
static int test_encode_reader_small_output(TestContext *ctx) {
    ImgnekoMemoryReader memory = {0};
    ImgnekoBase64EncodeReader encoder = {0};
    char workspace[12];
    ImgnekoReader reader;
    char out[4];
    size_t len = 0;
    int status;

    imgneko_memory_reader_init(&memory, STR("foo"));
    imgneko_base64_encode_reader_init(&encoder,
                                      imgneko_memory_reader_as_reader(&memory),
                                      workspace, sizeof(workspace));
    reader = imgneko_base64_encode_reader_as_reader(&encoder);

    memset(out, 'x', sizeof(out));
    status = imgneko_reader_read(reader, out, 3, &len);
    if (test_expect_status(ctx, status, IMGNEKO_READER_BUFFER_TOO_SMALL,
                           "encode reader small output") ||
        test_expect_size(ctx, len, 4, "encode reader retry size") ||
        test_expect_data(ctx, out, sizeof(out), STR("xxxx"),
                         "encode reader small output leaves output alone"))
        return 1;

    memset(out, 'x', sizeof(out));
    status = imgneko_reader_read(reader, out, sizeof(out), &len);
    if (test_expect_status(ctx, status, IMGNEKO_READER_OK,
                           "encode reader retry") ||
        test_expect_buffer_output(ctx, out, len, sizeof(out), STR("Zm9v"), 'x',
                                  "encode reader retry data"))
        return 1;

    status = imgneko_reader_read(reader, out, sizeof(out), &len);
    if (test_expect_status(ctx, status, IMGNEKO_READER_EOF,
                           "encode reader EOF"))
        return 1;

    status = imgneko_reader_read(reader, out, sizeof(out), &len);
    return test_expect_status(ctx, status, IMGNEKO_READER_EOF,
                              "encode reader sticky EOF");
}

// Verify encode transformer carry handling across 1- and 2-byte source chunks.
static int test_encode_reader_awkward_chunks(TestContext *ctx) {
    const size_t chunks[] = {1, 2, 1, 2, 1};
    TestChunkedReader source = {
        .data = "abcdefg",
        .len = 7,
        .chunks = chunks,
        .chunk_count = ARRAY_SIZE(chunks),
    };
    ImgnekoBase64EncodeReader encoder = {0};
    char workspace[9];
    String output = str_empty;
    int result = 0;

    imgneko_base64_encode_reader_init(&encoder,
                                      test_chunked_reader_as_reader(&source),
                                      workspace, sizeof(workspace));

    result = test_drain_reader(
        ctx, imgneko_base64_encode_reader_as_reader(&encoder), 4, &output);
    if (result == 0)
        result =
            test_expect_data(ctx, output.cstr, output.len, STR("YWJjZGVmZw=="),
                             "encode reader awkward output");

    str_free(output);
    return result;
}

// Verify that the encode transformer uses its workspace to request larger
// source chunks when the caller provides enough output capacity.
static int test_encode_reader_uses_workspace(TestContext *ctx) {
    TestChunkedReader source = {
        .data = "abcdefghijklmnopqrstuvwx",
        .len = 24,
    };
    ImgnekoBase64EncodeReader encoder = {0};
    char workspace[12];
    char out[64];
    size_t len = 0;
    int status;

    imgneko_base64_encode_reader_init(&encoder,
                                      test_chunked_reader_as_reader(&source),
                                      workspace, sizeof(workspace));

    memset(out, 'x', sizeof(out));
    status =
        imgneko_reader_read(imgneko_base64_encode_reader_as_reader(&encoder),
                            out, sizeof(out), &len);
    if (test_expect_status(ctx, status, IMGNEKO_READER_OK,
                           "encode reader workspace read") ||
        test_expect_size(ctx, source.read_count, 1,
                         "encode source read count") ||
        test_expect_size(ctx, source.max_out_cap, sizeof(workspace),
                         "encode source read cap") ||
        test_expect_size(ctx, len, 16, "encode workspace output size") ||
        test_expect_buffer_output(ctx, out, len, sizeof(out),
                                  STR("YWJjZGVmZ2hpamts"), 'x',
                                  "encode workspace output"))
        return 1;

    return 0;
}

// Verify decode transformer retry behavior before it consumes source bytes.
static int test_decode_reader_small_output(TestContext *ctx) {
    ImgnekoMemoryReader memory = {0};
    ImgnekoBase64DecodeReader decoder = {0};
    char workspace[12];
    ImgnekoReader reader;
    char out[3];
    size_t len = 0;
    int status;

    imgneko_memory_reader_init(&memory, STR("Zm9v"));
    imgneko_base64_decode_reader_init(&decoder,
                                      imgneko_memory_reader_as_reader(&memory),
                                      workspace, sizeof(workspace));
    reader = imgneko_base64_decode_reader_as_reader(&decoder);

    memset(out, 'x', sizeof(out));
    status = imgneko_reader_read(reader, out, 2, &len);
    if (test_expect_status(ctx, status, IMGNEKO_READER_BUFFER_TOO_SMALL,
                           "decode reader small output") ||
        test_expect_size(ctx, len, 3, "decode reader retry size") ||
        test_expect_data(ctx, out, sizeof(out), STR("xxx"),
                         "decode reader small output leaves output alone"))
        return 1;

    memset(out, 'x', sizeof(out));
    status = imgneko_reader_read(reader, out, sizeof(out), &len);
    if (test_expect_status(ctx, status, IMGNEKO_READER_OK,
                           "decode reader retry") ||
        test_expect_buffer_output(ctx, out, len, sizeof(out), STR("foo"), 'x',
                                  "decode reader retry data"))
        return 1;

    status = imgneko_reader_read(reader, out, sizeof(out), &len);
    return test_expect_status(ctx, status, IMGNEKO_READER_EOF,
                              "decode reader EOF");
}

// Verify decode transformer carry handling across 1- and 2-byte source chunks.
static int test_decode_reader_awkward_chunks(TestContext *ctx) {
    const size_t chunks[] = {1, 2, 1, 2, 1, 2, 1, 2};
    TestChunkedReader source = {
        .data = "YWJjZGVmZw==",
        .len = 12,
        .chunks = chunks,
        .chunk_count = ARRAY_SIZE(chunks),
    };
    ImgnekoBase64DecodeReader decoder = {0};
    char workspace[12];
    String output = str_empty;
    int result = 0;

    imgneko_base64_decode_reader_init(&decoder,
                                      test_chunked_reader_as_reader(&source),
                                      workspace, sizeof(workspace));

    result = test_drain_reader(
        ctx, imgneko_base64_decode_reader_as_reader(&decoder), 3, &output);
    if (result == 0)
        result = test_expect_data(ctx, output.cstr, output.len, STR("abcdefg"),
                                  "decode reader awkward output");

    str_free(output);
    return result;
}

// Verify decoded output can be returned while encoded carry stays available for
// a later call.
static int test_decode_reader_output_with_carry(TestContext *ctx) {
    const size_t chunks[] = {2, 5, 1};
    TestChunkedReader source = {
        .data = "YWJjZGVm",
        .len = 8,
        .chunks = chunks,
        .chunk_count = ARRAY_SIZE(chunks),
    };
    ImgnekoBase64DecodeReader decoder = {0};
    char workspace[8];
    ImgnekoReader reader;
    char out[6];
    size_t len = 0;
    int status;

    imgneko_base64_decode_reader_init(&decoder,
                                      test_chunked_reader_as_reader(&source),
                                      workspace, sizeof(workspace));
    reader = imgneko_base64_decode_reader_as_reader(&decoder);

    memset(out, 'x', sizeof(out));
    status = imgneko_reader_read(reader, out, sizeof(out), &len);
    if (test_expect_status(ctx, status, IMGNEKO_READER_OK,
                           "decode first output with carry") ||
        test_expect_buffer_output(ctx, out, len, sizeof(out), STR("abc"), 'x',
                                  "decode first output data"))
        return 1;

    memset(out, 'x', sizeof(out));
    status = imgneko_reader_read(reader, out, sizeof(out), &len);
    if (test_expect_status(ctx, status, IMGNEKO_READER_OK,
                           "decode carried output") ||
        test_expect_buffer_output(ctx, out, len, sizeof(out), STR("def"), 'x',
                                  "decode carried output data"))
        return 1;

    status = imgneko_reader_read(reader, out, sizeof(out), &len);
    return test_expect_status(ctx, status, IMGNEKO_READER_EOF,
                              "decode carried EOF");
}

// Verify that the decode transformer uses its workspace to request larger
// source chunks when the caller provides enough output capacity.
static int test_decode_reader_uses_workspace(TestContext *ctx) {
    TestChunkedReader source = {
        .data = "YWJjZGVmZ2hpamtsbW5vcHFyc3R1dnd4",
        .len = 32,
    };
    ImgnekoBase64DecodeReader decoder = {0};
    char workspace[16];
    char out[64];
    size_t len = 0;
    int status;

    imgneko_base64_decode_reader_init(&decoder,
                                      test_chunked_reader_as_reader(&source),
                                      workspace, sizeof(workspace));

    memset(out, 'x', sizeof(out));
    status =
        imgneko_reader_read(imgneko_base64_decode_reader_as_reader(&decoder),
                            out, sizeof(out), &len);
    if (test_expect_status(ctx, status, IMGNEKO_READER_OK,
                           "decode reader workspace read") ||
        test_expect_size(ctx, source.read_count, 1,
                         "decode source read count") ||
        test_expect_size(ctx, source.max_out_cap, sizeof(workspace),
                         "decode source read cap") ||
        test_expect_size(ctx, len, 12, "decode workspace output size") ||
        test_expect_buffer_output(ctx, out, len, sizeof(out),
                                  STR("abcdefghijkl"), 'x',
                                  "decode workspace output"))
        return 1;

    return 0;
}

// Verify padded final groups return their decoded bytes and then sticky EOF.
static int test_decode_reader_padded_eof(TestContext *ctx) {
    ImgnekoMemoryReader memory = {0};
    ImgnekoBase64DecodeReader decoder = {0};
    char workspace[8];
    ImgnekoReader reader;
    char out[3];
    size_t len = 0;
    int status;

    imgneko_memory_reader_init(&memory, STR("Zg=="));
    imgneko_base64_decode_reader_init(&decoder,
                                      imgneko_memory_reader_as_reader(&memory),
                                      workspace, sizeof(workspace));
    reader = imgneko_base64_decode_reader_as_reader(&decoder);

    memset(out, 'x', sizeof(out));
    status = imgneko_reader_read(reader, out, sizeof(out), &len);
    if (test_expect_status(ctx, status, IMGNEKO_READER_OK,
                           "decode padded final") ||
        test_expect_buffer_output(ctx, out, len, sizeof(out), STR("f"), 'x',
                                  "decode padded final data"))
        return 1;

    status = imgneko_reader_read(reader, out, sizeof(out), &len);
    if (test_expect_status(ctx, status, IMGNEKO_READER_EOF,
                           "decode padded EOF"))
        return 1;

    status = imgneko_reader_read(reader, out, sizeof(out), &len);
    return test_expect_status(ctx, status, IMGNEKO_READER_EOF,
                              "decode padded sticky EOF");
}

// Verify that transformers fail when initialized with workspaces smaller than
// the documented minimum sizes.
static int test_reader_invalid_workspace(TestContext *ctx) {
    ImgnekoMemoryReader memory = {0};
    ImgnekoBase64EncodeReader encoder = {0};
    ImgnekoBase64DecodeReader decoder = {0};
    char encode_workspace[2];
    char decode_workspace[3];
    char out[4];
    size_t len = 0;
    int status;

    imgneko_memory_reader_init(&memory, STR("foo"));
    imgneko_base64_encode_reader_init(
        &encoder, imgneko_memory_reader_as_reader(&memory), encode_workspace,
        sizeof(encode_workspace));
    memset(out, 'x', sizeof(out));
    status =
        imgneko_reader_read(imgneko_base64_encode_reader_as_reader(&encoder),
                            out, sizeof(out), &len);
    if (test_expect_status(ctx, status, IMGNEKO_READER_WORKSPACE_TOO_SMALL,
                           "encode reader small workspace") ||
        test_expect_data(ctx, out, sizeof(out), STR("xxxx"),
                         "encode small workspace leaves output alone"))
        return 1;

    imgneko_memory_reader_init(&memory, STR("foo"));
    imgneko_base64_encode_reader_init(
        &encoder, imgneko_memory_reader_as_reader(&memory), NULL, 0);
    memset(out, 'x', sizeof(out));
    status =
        imgneko_reader_read(imgneko_base64_encode_reader_as_reader(&encoder),
                            out, sizeof(out), &len);
    if (test_expect_status(ctx, status, IMGNEKO_READER_WORKSPACE_TOO_SMALL,
                           "encode reader null workspace") ||
        test_expect_data(ctx, out, sizeof(out), STR("xxxx"),
                         "encode null workspace leaves output alone"))
        return 1;

    imgneko_memory_reader_init(&memory, STR("Zm9v"));
    imgneko_base64_decode_reader_init(
        &decoder, imgneko_memory_reader_as_reader(&memory), decode_workspace,
        sizeof(decode_workspace));
    memset(out, 'x', sizeof(out));
    status =
        imgneko_reader_read(imgneko_base64_decode_reader_as_reader(&decoder),
                            out, sizeof(out), &len);
    if (test_expect_status(ctx, status, IMGNEKO_READER_WORKSPACE_TOO_SMALL,
                           "decode reader small workspace") ||
        test_expect_data(ctx, out, sizeof(out), STR("xxxx"),
                         "decode small workspace leaves output alone"))
        return 1;

    imgneko_memory_reader_init(&memory, STR("Zm9v"));
    imgneko_base64_decode_reader_init(
        &decoder, imgneko_memory_reader_as_reader(&memory), NULL, 0);
    memset(out, 'x', sizeof(out));
    status =
        imgneko_reader_read(imgneko_base64_decode_reader_as_reader(&decoder),
                            out, sizeof(out), &len);
    if (test_expect_status(ctx, status, IMGNEKO_READER_WORKSPACE_TOO_SMALL,
                           "decode reader null workspace") ||
        test_expect_data(ctx, out, sizeof(out), STR("xxxx"),
                         "decode null workspace leaves output alone"))
        return 1;

    return 0;
}

// Verify that a source reader capacity failure is reported as a transformer
// workspace failure instead of the caller-output BUFFER_TOO_SMALL status.
static int test_reader_source_requires_larger_workspace(TestContext *ctx) {
    TestCompleteChunkReader raw_source = {
        .data = "abcdef",
        .len = 6,
    };
    TestCompleteChunkReader encoded_source = {
        .data = "YWJjZGVm",
        .len = 8,
    };
    ImgnekoBase64EncodeReader encoder = {0};
    ImgnekoBase64DecodeReader decoder = {0};
    char encode_workspace[3];
    char decode_workspace[4];
    char out[16];
    size_t len = 0;
    int status;

    imgneko_base64_encode_reader_init(
        &encoder, test_complete_chunk_reader_as_reader(&raw_source),
        encode_workspace, sizeof(encode_workspace));
    memset(out, 'x', sizeof(out));
    status =
        imgneko_reader_read(imgneko_base64_encode_reader_as_reader(&encoder),
                            out, sizeof(out), &len);
    if (test_expect_status(ctx, status, IMGNEKO_READER_WORKSPACE_TOO_SMALL,
                           "encode source needs larger workspace") ||
        test_expect_data(ctx, out, sizeof(out), "xxxxxxxxxxxxxxxx", sizeof(out),
                         "encode source workspace leaves output alone"))
        return 1;

    imgneko_base64_decode_reader_init(
        &decoder, test_complete_chunk_reader_as_reader(&encoded_source),
        decode_workspace, sizeof(decode_workspace));
    memset(out, 'x', sizeof(out));
    status =
        imgneko_reader_read(imgneko_base64_decode_reader_as_reader(&decoder),
                            out, sizeof(out), &len);
    if (test_expect_status(ctx, status, IMGNEKO_READER_WORKSPACE_TOO_SMALL,
                           "decode source needs larger workspace") ||
        test_expect_data(ctx, out, sizeof(out), "xxxxxxxxxxxxxxxx", sizeof(out),
                         "decode source workspace leaves output alone"))
        return 1;

    return 0;
}

// Verify that transformer readers preserve ordinary source failures without
// treating them as Base64 decoding failures.
static int test_reader_source_errors(TestContext *ctx) {
    ImgnekoReader source = {
        .read = error_reader_func,
    };
    ImgnekoBase64EncodeReader encoder = {0};
    ImgnekoBase64DecodeReader decoder = {0};
    char encode_workspace[6];
    char decode_workspace[8];
    char out[4];
    size_t len = 0;
    int status;

    imgneko_base64_encode_reader_init(&encoder, source, encode_workspace,
                                      sizeof(encode_workspace));
    memset(out, 'x', sizeof(out));
    status =
        imgneko_reader_read(imgneko_base64_encode_reader_as_reader(&encoder),
                            out, sizeof(out), &len);
    if (test_expect_status(ctx, status, IMGNEKO_READER_ERROR,
                           "encode source error") ||
        test_expect_data(ctx, out, sizeof(out), STR("xxxx"),
                         "encode source error leaves output alone"))
        return 1;

    imgneko_base64_decode_reader_init(&decoder, source, decode_workspace,
                                      sizeof(decode_workspace));
    memset(out, 'x', sizeof(out));
    status =
        imgneko_reader_read(imgneko_base64_decode_reader_as_reader(&decoder),
                            out, sizeof(out), &len);
    if (test_expect_status(ctx, status, IMGNEKO_READER_ERROR,
                           "decode source error") ||
        test_expect_status(ctx, decoder.error_status, IMGNEKO_BASE64_OK,
                           "decode source error base64 status") ||
        test_expect_data(ctx, out, sizeof(out), STR("xxxx"),
                         "decode source error leaves output alone"))
        return 1;

    return 0;
}

// Verify that transformer readers reject source readers that report impossible
// OK lengths.
static int test_reader_source_protocol_errors(TestContext *ctx) {
    const size_t reported_lens[] = {0, SIZE_MAX};

    for (size_t i = 0; i < ARRAY_SIZE(reported_lens); ++i) {
        BadOkReader bad_source = {
            .reported_len = reported_lens[i],
        };
        ImgnekoReader source = {
            .read = bad_ok_reader_func,
            .ctx = &bad_source,
        };
        ImgnekoBase64EncodeReader encoder = {0};
        ImgnekoBase64DecodeReader decoder = {0};
        char encode_workspace[6];
        char decode_workspace[8];
        char out[4];
        size_t len = 0;
        int status;

        imgneko_base64_encode_reader_init(&encoder, source, encode_workspace,
                                          sizeof(encode_workspace));
        memset(out, 'x', sizeof(out));
        status = imgneko_reader_read(
            imgneko_base64_encode_reader_as_reader(&encoder), out, sizeof(out),
            &len);
        if (test_expect_status(ctx, status, IMGNEKO_READER_ERROR,
                               "encode bad source OK length") ||
            test_expect_data(ctx, out, sizeof(out), STR("xxxx"),
                             "encode bad source leaves output alone"))
            return 1;

        imgneko_base64_decode_reader_init(&decoder, source, decode_workspace,
                                          sizeof(decode_workspace));
        memset(out, 'x', sizeof(out));
        status = imgneko_reader_read(
            imgneko_base64_decode_reader_as_reader(&decoder), out, sizeof(out),
            &len);
        if (test_expect_status(ctx, status, IMGNEKO_READER_ERROR,
                               "decode bad source OK length") ||
            test_expect_data(ctx, out, sizeof(out), STR("xxxx"),
                             "decode bad source leaves output alone"))
            return 1;
    }

    return 0;
}

// Verify that decode errors found while checking source EOF after a padded
// final quartet are deferred until after the decoded bytes are returned.
static int test_decode_reader_padded_verify_errors(TestContext *ctx) {
    const ImgnekoReaderStatus final_statuses[] = {
        IMGNEKO_READER_BUFFER_TOO_SMALL,
        IMGNEKO_READER_ERROR,
    };
    const ImgnekoReaderStatus expected_statuses[] = {
        IMGNEKO_READER_WORKSPACE_TOO_SMALL,
        IMGNEKO_READER_ERROR,
    };

    for (size_t i = 0; i < ARRAY_SIZE(final_statuses); ++i) {
        StatusAfterDataReader source = {
            .data = "Zg==",
            .len = 4,
            .final_status = final_statuses[i],
        };
        ImgnekoReader source_reader = {
            .read = status_after_data_reader_func,
            .ctx = &source,
        };
        ImgnekoBase64DecodeReader decoder = {0};
        char workspace[8];
        char out[3];
        size_t len = 0;
        int status;

        imgneko_base64_decode_reader_init(&decoder, source_reader, workspace,
                                          sizeof(workspace));

        memset(out, 'x', sizeof(out));
        status = imgneko_reader_read(
            imgneko_base64_decode_reader_as_reader(&decoder), out, sizeof(out),
            &len);
        if (test_expect_status(ctx, status, IMGNEKO_READER_OK,
                               "decode padded verify output") ||
            test_expect_buffer_output(ctx, out, len, sizeof(out), STR("f"), 'x',
                                      "decode padded verify data"))
            return 1;

        memset(out, 'x', sizeof(out));
        status = imgneko_reader_read(
            imgneko_base64_decode_reader_as_reader(&decoder), out, sizeof(out),
            &len);
        if (test_expect_status(ctx, status, expected_statuses[i],
                               "decode padded deferred verify error") ||
            test_expect_size(ctx, len, 0,
                             "decode padded verify error output size") ||
            test_expect_data(ctx, out, sizeof(out), STR("xxx"),
                             "decode padded verify error leaves output alone"))
            return 1;
    }

    return 0;
}

// Verify decode transformer errors before emitting any output for invalid input
// that has no complete valid prefix.
static int test_decode_reader_invalid_input(TestContext *ctx) {
    const struct {
        const char *input;
        ImgnekoBase64Status expected_error_status;
    } inputs[] = {
        {"Zg=", IMGNEKO_BASE64_TRUNCATED_INPUT},
        {"!!!!", IMGNEKO_BASE64_INVALID_INPUT},
        {"Zg=A", IMGNEKO_BASE64_INVALID_INPUT},
    };

    for (size_t i = 0; i < ARRAY_SIZE(inputs); ++i) {
        ImgnekoMemoryReader memory = {0};
        ImgnekoBase64DecodeReader decoder = {0};
        char workspace[8];
        char out[3] = "xxx";
        size_t len = 0;
        int status;

        imgneko_memory_reader_init(&memory, inputs[i].input,
                                   strlen(inputs[i].input));
        imgneko_base64_decode_reader_init(
            &decoder, imgneko_memory_reader_as_reader(&memory), workspace,
            sizeof(workspace));

        status = imgneko_reader_read(
            imgneko_base64_decode_reader_as_reader(&decoder), out, sizeof(out),
            &len);
        if (test_expect_status(ctx, status, IMGNEKO_READER_ERROR,
                               "decode reader generic invalid input") ||
            test_expect_status(ctx, decoder.error_status,
                               inputs[i].expected_error_status,
                               "decode reader invalid input detail") ||
            test_expect_data(ctx, out, sizeof(out), STR("xxx"),
                             "decode reader error leaves output alone"))
            return 1;
    }

    return 0;
}

// Verify invalid input after valid decoded bytes is reported after those bytes
// are returned to the caller.
static int test_decode_reader_partial_success_before_error(TestContext *ctx) {
    const char *inputs[] = {
        "Zg==AA",     "Zm8=AA",     "Zm9v!!!!",
        "Zm9vYg==AA", "Zm9vYmE=AA", "Zm9vYmFy!!!!",
    };
    const char *outputs[] = {
        "f", "fo", "foo", "foob", "fooba", "foobar",
    };

    for (size_t i = 0; i < ARRAY_SIZE(inputs); ++i) {
        ImgnekoMemoryReader memory = {0};
        ImgnekoBase64DecodeReader decoder = {0};
        char workspace[8];
        char out[6];
        ImgnekoReader reader;
        size_t len = 0;
        int status;
        size_t output_len = strlen(outputs[i]);

        imgneko_memory_reader_init(&memory, inputs[i], strlen(inputs[i]));
        imgneko_base64_decode_reader_init(
            &decoder, imgneko_memory_reader_as_reader(&memory), workspace,
            sizeof(workspace));
        reader = imgneko_base64_decode_reader_as_reader(&decoder);

        memset(out, 'x', sizeof(out));
        status = imgneko_reader_read(reader, out, sizeof(out), &len);
        if (test_expect_status(ctx, status, IMGNEKO_READER_OK,
                               "decode reader valid prefix") ||
            test_expect_size(ctx, len, output_len,
                             "decode valid prefix size") ||
            test_expect_buffer_output(ctx, out, len, sizeof(out), outputs[i],
                                      output_len, 'x',
                                      "decode valid prefix data"))
            return 1;

        memset(out, 'x', sizeof(out));
        status = imgneko_reader_read(reader, out, sizeof(out), &len);
        if (test_expect_status(ctx, status, IMGNEKO_READER_ERROR,
                               "decode reader deferred generic error") ||
            test_expect_status(ctx, decoder.error_status,
                               IMGNEKO_BASE64_INVALID_INPUT,
                               "decode reader deferred error detail") ||
            test_expect_size(ctx, len, 0,
                             "decode deferred error output size") ||
            test_expect_data(ctx, out, sizeof(out), STR("xxxxxx"),
                             "decode deferred error leaves output alone"))
            return 1;
    }

    return 0;
}

// Run one stacked-reader round trip using deterministic pseudo-random data and
// chunk sizes derived from `seed`.
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
    ImgnekoBase64EncodeReader encoder = {0};
    ImgnekoBase64DecodeReader decoder = {0};
    char encode_workspace[96];
    char decode_workspace[128];
    char out[257];
    ImgnekoReader reader;
    String output = str_empty;
    int result = 0;

    for (size_t i = 0; i < sizeof(input); ++i) {
        input[i] = (char)next_test_random(&random);
        if (i % 257 == 0)
            input[i] = '\0';
    }

    for (size_t i = 0; i < ARRAY_SIZE(source_chunks); ++i)
        source_chunks[i] = next_test_random(&random) % 97 + 1;

    imgneko_base64_encode_reader_init(
        &encoder, test_chunked_reader_as_reader(&source), encode_workspace,
        sizeof(encode_workspace));
    imgneko_base64_decode_reader_init(
        &decoder, imgneko_base64_encode_reader_as_reader(&encoder),
        decode_workspace, sizeof(decode_workspace));
    reader = imgneko_base64_decode_reader_as_reader(&decoder);

    while (true) {
        size_t cap = next_test_random(&random) % sizeof(out);
        size_t len = 0;

        memset(out, 'x', sizeof(out));
        int status = imgneko_reader_read(reader, out, cap, &len);

        if (status == IMGNEKO_READER_BUFFER_TOO_SMALL) {
            if (len == 0 || len > sizeof(out)) {
                result = test_fail_message(ctx, "invalid retry size");
                break;
            }

            for (size_t i = 0; i < sizeof(out); ++i) {
                if (out[i] == 'x')
                    continue;

                result = test_fail_message(
                    ctx, "small output read modified output buffer");
                break;
            }
            if (result != 0)
                break;

            cap = len;
            memset(out, 'x', sizeof(out));
            status = imgneko_reader_read(reader, out, cap, &len);
        }

        if (status == IMGNEKO_READER_EOF) {
            result = test_expect_size(ctx, len, 0, "stacked reader EOF size");
            break;
        }

        if (status != IMGNEKO_READER_OK) {
            fprintf(stderr, "%s: stacked reader failed with status %d\n",
                    ctx->test_name, status);
            result = 1;
            break;
        }

        if (len == 0 || len > cap) {
            result = test_fail_message(ctx, "invalid stacked reader output");
            break;
        }

        for (size_t i = len; i < sizeof(out); ++i) {
            if (out[i] == 'x')
                continue;

            result = test_fail_message(
                ctx, "stacked reader modified bytes beyond returned data");
            break;
        }
        if (result != 0)
            break;

        str_append_data(output, out, len);
    }

    if (result == 0)
        result = test_expect_data(ctx, output.cstr, output.len, input,
                                  sizeof(input), "stacked reader output");

    str_free(output);
    return result;
}

// Verify stacked encode/decode readers over binary data with irregular source
// chunks and irregular caller output sizes.
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

// Check public base64 error strings, including the fallback for unknown values.
static int test_error_strings(TestContext *ctx) {
    const ImgnekoBase64Status statuses[] = {
        IMGNEKO_BASE64_OK,
        IMGNEKO_BASE64_INVALID_INPUT,
        IMGNEKO_BASE64_OVERFLOW,
        IMGNEKO_BASE64_BUFFER_TOO_SMALL,
        IMGNEKO_BASE64_TRUNCATED_INPUT,
        (ImgnekoBase64Status)12345,
    };

    for (size_t i = 0; i < ARRAY_SIZE(statuses); ++i) {
        const char *message = imgneko_base64_error_string(statuses[i]);
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
        PREFIXED_TEST(test_encode_vectors),
        PREFIXED_TEST(test_decode_vectors),
        PREFIXED_TEST(test_helper_failures),
        PREFIXED_TEST(test_encode_reader_padding),
        PREFIXED_TEST(test_encode_reader_small_output),
        PREFIXED_TEST(test_encode_reader_awkward_chunks),
        PREFIXED_TEST(test_encode_reader_uses_workspace),
        PREFIXED_TEST(test_decode_reader_small_output),
        PREFIXED_TEST(test_decode_reader_awkward_chunks),
        PREFIXED_TEST(test_decode_reader_output_with_carry),
        PREFIXED_TEST(test_decode_reader_uses_workspace),
        PREFIXED_TEST(test_decode_reader_padded_eof),
        PREFIXED_TEST(test_reader_invalid_workspace),
        PREFIXED_TEST(test_reader_source_requires_larger_workspace),
        PREFIXED_TEST(test_reader_source_errors),
        PREFIXED_TEST(test_reader_source_protocol_errors),
        PREFIXED_TEST(test_decode_reader_padded_verify_errors),
        PREFIXED_TEST(test_decode_reader_invalid_input),
        PREFIXED_TEST(test_decode_reader_partial_success_before_error),
        PREFIXED_TEST(test_stacked_reader_pseudorandom_round_trip),
        PREFIXED_TEST(test_error_strings),
    };

    return run_subtests(argc, argv, subtests, ARRAY_SIZE(subtests));
}
