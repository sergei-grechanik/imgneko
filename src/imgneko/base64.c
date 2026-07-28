// SPDX-License-Identifier: MIT-0

// Implementation of standard padded base64 helpers and reader transformers.

#include "imgneko/base64.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "util/common.h"

//===----------------------------------------------------------------------===//
// Base64 Helpers and one-shot decoder and encoder
//===----------------------------------------------------------------------===//

static const char BASE64_ALPHABET[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

const char *imgneko_base64_error_string(ImgnekoBase64Status status) {
    switch (status) {
    case IMGNEKO_BASE64_OK:
        return "ok";
    case IMGNEKO_BASE64_INVALID_INPUT:
        return "invalid base64 input";
    case IMGNEKO_BASE64_OVERFLOW:
        return "base64 length overflow";
    case IMGNEKO_BASE64_BUFFER_TOO_SMALL:
        return "buffer too small";
    case IMGNEKO_BASE64_TRUNCATED_INPUT:
        return "truncated base64 input";
    }

    return "unknown base64 error";
}

ImgnekoBase64Status imgneko_base64_encoded_len(size_t input_len,
                                               size_t *len_out) {
    assert(len_out != NULL);
    *len_out = 0;

    size_t groups = input_len / 3;
    size_t rem = input_len % 3;

    if (groups > SIZE_MAX / 4)
        return IMGNEKO_BASE64_OVERFLOW;

    size_t len = groups * 4;
    if (rem != 0) {
        if (len > SIZE_MAX - 4)
            return IMGNEKO_BASE64_OVERFLOW;
        len += 4;
    }

    *len_out = len;
    return IMGNEKO_BASE64_OK;
}

// Encode a complete 3-byte group without any padding decisions.
static void encode_complete_group(const unsigned char data[3], char out[4]) {
    // Base64 divides the three input bytes, in network bit order, into four
    // consecutive six-bit alphabet indices.
    uint32_t bits = ((uint32_t)data[0] << 16) | ((uint32_t)data[1] << 8) |
                    (uint32_t)data[2];

    out[0] = BASE64_ALPHABET[(bits >> 18) & 0x3fu];
    out[1] = BASE64_ALPHABET[(bits >> 12) & 0x3fu];
    out[2] = BASE64_ALPHABET[(bits >> 6) & 0x3fu];
    out[3] = BASE64_ALPHABET[bits & 0x3fu];
}

// Encode every complete 3-byte group in `data[0..len)` and return the number
// of output bytes written. `len` must be a multiple of three.
static size_t encode_complete_groups(const unsigned char *data, size_t len,
                                     char *out) {
    assert(len % 3 == 0);

    size_t out_offset = 0;
    for (size_t offset = 0; offset < len; offset += 3) {
        encode_complete_group(data + offset, out + out_offset);
        out_offset += 4;
    }

    return out_offset;
}

// Encode the final 1- or 2-byte group into a padded base64 quartet.
static void encode_final_group(const unsigned char *data, size_t len,
                               char out[4]) {
    assert(1 <= len && len <= 2);

    unsigned char b0 = data[0];
    unsigned char b1 = len == 2 ? data[1] : 0;

    out[0] = BASE64_ALPHABET[b0 >> 2];
    out[1] = BASE64_ALPHABET[((b0 & 0x03u) << 4) | (b1 >> 4)];
    out[2] = len == 2 ? BASE64_ALPHABET[(b1 & 0x0fu) << 2] : '=';
    out[3] = '=';
}

ImgnekoBase64Status imgneko_base64_encode(const char *data, size_t len,
                                          char *out, size_t out_cap,
                                          size_t *len_out) {
    assert(len_out != NULL);
    assert(data != NULL || len == 0);
    assert(out != NULL || out_cap == 0);
    *len_out = 0;

    size_t required = 0;
    ImgnekoBase64Status status = imgneko_base64_encoded_len(len, &required);
    if (status != IMGNEKO_BASE64_OK)
        return status;

    *len_out = required;

    if (required > out_cap)
        return IMGNEKO_BASE64_BUFFER_TOO_SMALL;

    size_t complete_len = len - len % 3;
    size_t out_offset =
        encode_complete_groups((const unsigned char *)data, complete_len, out);

    if (complete_len != len)
        encode_final_group((const unsigned char *)data + complete_len,
                           len - complete_len, out + out_offset);

    return IMGNEKO_BASE64_OK;
}

enum {
    // Valid decoded values occupy six bits, so this bit marks every other
    // ASCII byte (including padding) without colliding with valid data.
    BASE64_INVALID_VALUE = 0x40,
};

// Map ASCII bytes to their decoded values. Keeping invalid bytes in the table
// lets the common path validate a quartet with a single predictable branch.
// Bytes with the high bit set are rejected before indexing this table.
// clang-format off
static const unsigned char BASE64_DECODE_TABLE[128] = {
    // 0x00-0x1f
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    // 0x20-0x2f: '+' and '/'
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 64, 64, 63,
    // 0x30-0x3f: '0'-'9'
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64,
    // 0x40-0x4f: 'A'-'O'
    64, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14,
    // 0x50-0x5f: 'P'-'Z'
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 64,
    // 0x60-0x6f: 'a'-'o'
    64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    // 0x70-0x7f: 'p'-'z'
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64, 64, 64,
};
// clang-format on

// Decode one strict base64 quartet.
//
// `len_out` receives 1, 2, or 3 decoded bytes. `is_final_out` receives true
// when the quartet contains padding and therefore must be the final quartet.
static ImgnekoBase64Status decode_quartet(const char quartet[4], char out[3],
                                          size_t *len_out, bool *is_final_out) {
    *len_out = 0;
    *is_final_out = false;

    const unsigned char ch0 = (unsigned char)quartet[0];
    const unsigned char ch1 = (unsigned char)quartet[1];
    const unsigned char ch2 = (unsigned char)quartet[2];
    const unsigned char ch3 = (unsigned char)quartet[3];

    // Make sure all bytes are in [0, 128) so we can use the decode table.
    if ((ch0 | ch1 | ch2 | ch3) >= 128)
        return IMGNEKO_BASE64_INVALID_INPUT;

    const unsigned int value0 = BASE64_DECODE_TABLE[ch0];
    const unsigned int value1 = BASE64_DECODE_TABLE[ch1];
    const unsigned int value2 = BASE64_DECODE_TABLE[ch2];
    const unsigned int value3 = BASE64_DECODE_TABLE[ch3];

    // Unpadded quartets account for virtually all bytes in ordinary input.
    // Decode that case directly and leave padding checks on the cold path.
    if (((value0 | value1 | value2 | value3) & BASE64_INVALID_VALUE) == 0) {
        out[0] = (char)((value0 << 2) | (value1 >> 4));
        out[1] = (char)(((value1 & 0x0f) << 4) | (value2 >> 2));
        out[2] = (char)(((value2 & 0x03) << 6) | value3);
        *len_out = 3;
        return IMGNEKO_BASE64_OK;
    }

    if (value0 == BASE64_INVALID_VALUE || value1 == BASE64_INVALID_VALUE)
        return IMGNEKO_BASE64_INVALID_INPUT;

    if (ch2 == '=') {
        if (ch3 != '=' || (value1 & 0x0f) != 0)
            return IMGNEKO_BASE64_INVALID_INPUT;

        out[0] = (char)((value0 << 2) | (value1 >> 4));
        *len_out = 1;
        *is_final_out = true;
        return IMGNEKO_BASE64_OK;
    }

    if (value2 == BASE64_INVALID_VALUE)
        return IMGNEKO_BASE64_INVALID_INPUT;

    if (ch3 == '=') {
        if ((value2 & 0x03) != 0)
            return IMGNEKO_BASE64_INVALID_INPUT;

        out[0] = (char)((value0 << 2) | (value1 >> 4));
        out[1] = (char)(((value1 & 0x0f) << 4) | (value2 >> 2));
        *len_out = 2;
        *is_final_out = true;
        return IMGNEKO_BASE64_OK;
    }

    return IMGNEKO_BASE64_INVALID_INPUT;
}

ImgnekoBase64Status imgneko_base64_decoded_len(const char *data, size_t len,
                                               size_t *len_out) {
    assert(len_out != NULL);
    assert(data != NULL || len == 0);
    *len_out = 0;

    if (len % 4 != 0)
        return IMGNEKO_BASE64_INVALID_INPUT;

    if (len == 0)
        return IMGNEKO_BASE64_OK;

    if (data[len - 2] == '=' && data[len - 1] != '=')
        return IMGNEKO_BASE64_INVALID_INPUT;

    if (data[len - 3] == '=')
        return IMGNEKO_BASE64_INVALID_INPUT;

    size_t padding = 0;
    if (data[len - 1] == '=') {
        padding = 1;
        if (data[len - 2] == '=')
            padding = 2;
    }

    *len_out = (len / 4) * 3 - padding;
    return IMGNEKO_BASE64_OK;
}

ImgnekoBase64Status imgneko_base64_decode(const char *data, size_t len,
                                          char *out, size_t out_cap,
                                          size_t *len_out) {
    assert(len_out != NULL);
    assert(data != NULL || len == 0);
    assert(out != NULL || out_cap == 0);

    *len_out = 0;
    ImgnekoBase64Status status = imgneko_base64_decoded_len(data, len, len_out);
    if (status != IMGNEKO_BASE64_OK)
        return status;

    if (*len_out > out_cap)
        return IMGNEKO_BASE64_BUFFER_TOO_SMALL;

    size_t out_offset = 0;
    bool previous_was_final = false;

    for (size_t offset = 0; offset < len; offset += 4) {
        char decoded[3];
        size_t decoded_len = 0;
        bool is_final = false;

        if (previous_was_final)
            return IMGNEKO_BASE64_INVALID_INPUT;

        status =
            decode_quartet(data + offset, decoded, &decoded_len, &is_final);
        if (status != IMGNEKO_BASE64_OK)
            return status;

        memcpy(out + out_offset, decoded, decoded_len);

        out_offset += decoded_len;
        previous_was_final = is_final;
    }

    return IMGNEKO_BASE64_OK;
}

//===----------------------------------------------------------------------===//
// Reader Transformer Helpers
//===----------------------------------------------------------------------===//

// Treat source capacity failures as transformer workspace failures because
// callers cannot fix them by resizing `out`.
static ImgnekoReaderStatus translate_source_error(ImgnekoReaderStatus status) {
    if (status == IMGNEKO_READER_BUFFER_TOO_SMALL)
        return IMGNEKO_READER_WORKSPACE_TOO_SMALL;
    return status;
}

//===----------------------------------------------------------------------===//
// Base64 Encode Reader
//===----------------------------------------------------------------------===//

void imgneko_base64_encode_reader_init(ImgnekoBase64EncodeReader *reader,
                                       ImgnekoReader source, char *buffer,
                                       size_t buffer_cap) {
    assert(reader != NULL);
    *reader = (ImgnekoBase64EncodeReader){
        .source = source,
        .buffer = buffer,
        .buffer_cap = buffer_cap,
    };
}

// Return how many source bytes can be read without exceeding `out_remaining`
// encoded output bytes or the remaining workspace after carried raw bytes.
static size_t
base64_encode_reader_input_cap(const ImgnekoBase64EncodeReader *reader,
                               size_t out_remaining) {
    size_t groups = out_remaining / 4;
    // IMGNEKO_UNCOVERED_OK[3 lines]: The public callback checks output and
    // workspace sizes before asking for an input capacity.
    if (groups == 0 || reader->carry_len >= reader->buffer_cap)
        return 0;

    size_t cap = groups * 3 - reader->carry_len;
    size_t available = reader->buffer_cap - reader->carry_len;
    return MIN(cap, available);
}

// Encode complete 3-byte groups from `reader->buffer` into `out`.
//
// Any trailing 1 or 2 raw bytes are compacted to the start of the workspace
// until more input arrives or the source reaches EOF.
static void
base64_encode_reader_encode_buffer(ImgnekoBase64EncodeReader *reader,
                                   size_t input_len, char *out,
                                   size_t *out_len) {
    size_t total_len = reader->carry_len + input_len;
    size_t process_len = total_len - total_len % 3;

    *out_len = encode_complete_groups((const unsigned char *)reader->buffer,
                                      process_len, out);

    reader->carry_len = total_len - process_len;
    if (reader->carry_len != 0)
        memmove(reader->buffer, reader->buffer + process_len,
                reader->carry_len);
}

// Reader callback for base64-encoding bytes from an underlying source.
static ImgnekoReaderStatus base64_encode_reader_func(void *ctx, char *out,
                                                     size_t out_cap,
                                                     size_t *len_out) {
    ImgnekoBase64EncodeReader *reader = ctx;

    assert(len_out != NULL);
    assert(reader != NULL);
    assert(out != NULL || out_cap == 0);

    *len_out = 0;

    if (reader->eof)
        return IMGNEKO_READER_EOF;

    if (reader->buffer == NULL || reader->buffer_cap < 3)
        return IMGNEKO_READER_WORKSPACE_TOO_SMALL;

    if (out_cap < 4) {
        *len_out = 4;
        return IMGNEKO_READER_BUFFER_TOO_SMALL;
    }

    // We read the source in a loop until we can produce at least one encoded
    // quartet or the source reaches EOF. A successful output ends the call.
    while (true) {
        // Compute the maximum number of source bytes we can read and encode
        // without exceeding the output capacity (or the remaining `buffer`).
        size_t cap = base64_encode_reader_input_cap(reader, out_cap);
        // IMGNEKO_UNCOVERED_OK[3 lines]: `base64_encode_reader_input_cap()`
        // returns zero only for state rejected before this call.
        if (cap == 0)
            return IMGNEKO_READER_WORKSPACE_TOO_SMALL;

        // New source data is appended after carried bytes so the workspace
        // starts with as many complete 3-byte groups as possible.
        size_t len = 0;
        ImgnekoReaderStatus status = imgneko_reader_read(
            reader->source, reader->buffer + reader->carry_len, cap, &len);

        if (status == IMGNEKO_READER_OK) {
            if (len == 0 || len > cap)
                return IMGNEKO_READER_ERROR;

            // Encode complete groups and compact any new trailing 1-2 bytes to
            // the start of the workspace for the next read.
            base64_encode_reader_encode_buffer(reader, len, out, len_out);
            if (*len_out != 0)
                return IMGNEKO_READER_OK;

            continue;
        }

        if (status == IMGNEKO_READER_EOF) {
            if (reader->carry_len == 0) {
                reader->eof = true;
                return IMGNEKO_READER_EOF;
            }

            // Source EOF turns the carried 1-2 bytes into the final padded
            // quartet. The earlier out_cap check guarantees room for it.
            encode_final_group((const unsigned char *)reader->buffer,
                               reader->carry_len, out);
            *len_out = 4;
            reader->carry_len = 0;
            reader->eof = true;
            return IMGNEKO_READER_OK;
        }

        return translate_source_error(status);
    }
}

ImgnekoReader
imgneko_base64_encode_reader_as_reader(ImgnekoBase64EncodeReader *reader) {
    return (ImgnekoReader){
        .read = base64_encode_reader_func,
        .ctx = reader,
    };
}

//===----------------------------------------------------------------------===//
// Base64 Decode Reader
//===----------------------------------------------------------------------===//

void imgneko_base64_decode_reader_init(ImgnekoBase64DecodeReader *reader,
                                       ImgnekoReader source, char *buffer,
                                       size_t buffer_cap) {
    assert(reader != NULL);
    *reader = (ImgnekoBase64DecodeReader){
        .source = source,
        .buffer = buffer,
        .buffer_cap = buffer_cap,
        .error_status = IMGNEKO_BASE64_OK,
        .pending_status = IMGNEKO_READER_OK,
    };
}

// Verify that a padded quartet is actually followed by source EOF.
static ImgnekoReaderStatus
verify_decode_source_eof(ImgnekoBase64DecodeReader *reader) {
    size_t len = 0;
    ImgnekoReaderStatus status = imgneko_reader_read(
        reader->source, reader->buffer, reader->buffer_cap, &len);

    if (status == IMGNEKO_READER_EOF)
        return IMGNEKO_READER_OK;
    if (status == IMGNEKO_READER_OK) {
        reader->error_status = IMGNEKO_BASE64_INVALID_INPUT;
        return IMGNEKO_READER_ERROR;
    }
    if (status == IMGNEKO_READER_BUFFER_TOO_SMALL)
        return IMGNEKO_READER_WORKSPACE_TOO_SMALL;
    return status;
}

// Return how many encoded source bytes can be read without exceeding
// `out_remaining` decoded output bytes or the remaining workspace after carried
// encoded bytes.
static size_t
base64_decode_reader_input_cap(const ImgnekoBase64DecodeReader *reader,
                               size_t out_remaining) {
    size_t groups = out_remaining / 3;
    // IMGNEKO_UNCOVERED_OK[3 lines]: The public callback checks output and
    // workspace sizes before asking for an input capacity.
    if (groups == 0 || reader->carry_len >= reader->buffer_cap)
        return 0;

    size_t available = reader->buffer_cap - reader->carry_len;
    // IMGNEKO_UNCOVERED_OK[2 lines]: `out_cap` cannot practically reach this
    // branch in tests.
    size_t group_cap = groups > SIZE_MAX / 4 ? SIZE_MAX : groups * 4;
    size_t cap = group_cap - reader->carry_len;

    return MIN(cap, available);
}

// Record `status` for the next call if this call has already produced bytes.
// This lets the decoder report valid bytes before the invalid input that
// follows them, while still making the eventual error a no-output read.
//
// `reader`
//     Decode reader whose deferred-error state is updated.
// `status`
//     Reader status to return now, or to defer if bytes were decoded.
// `decoded_len`
//     Number of bytes already written to the caller's output buffer.
// `decoded_len_out`
//     Output parameter receiving `decoded_len` when `status` is deferred.
static ImgnekoReaderStatus
base64_decode_reader_finish_error(ImgnekoBase64DecodeReader *reader,
                                  ImgnekoReaderStatus status,
                                  size_t decoded_len, size_t *decoded_len_out) {
    assert(status != IMGNEKO_READER_OK);
    assert(status != IMGNEKO_READER_EOF);

    if (decoded_len == 0)
        return status;

    reader->carry_len = 0;
    reader->pending_status = status;
    *decoded_len_out = decoded_len;
    return IMGNEKO_READER_OK;
}

// Record a detailed base64 failure and expose it through the generic reader
// interface. Valid decoded bytes take precedence over the error for the
// current call, so callers can consume them before receiving the failure.
static ImgnekoReaderStatus
base64_decode_reader_fail(ImgnekoBase64DecodeReader *reader,
                          ImgnekoBase64Status error_status, size_t decoded_len,
                          size_t *decoded_len_out) {
    assert(error_status != IMGNEKO_BASE64_OK);

    reader->error_status = error_status;
    return base64_decode_reader_finish_error(reader, IMGNEKO_READER_ERROR,
                                             decoded_len, decoded_len_out);
}

// Decode complete quartets from `reader->buffer` directly into `out`.
// Trailing encoded bytes that do not yet make a full quartet are compacted to
// the start of the borrowed workspace.
//
// `reader`
//     Decode reader containing carried encoded bytes and borrowed workspace.
// `input_len`
//     Number of newly read encoded bytes appended after `reader->carry_len`.
// `out`
//     Caller-owned output buffer receiving decoded bytes directly.
// `decoded_len_out`
//     Output parameter receiving the number of bytes written to `out`.
static ImgnekoReaderStatus
base64_decode_reader_decode_buffer(ImgnekoBase64DecodeReader *reader,
                                   size_t input_len, char *out,
                                   size_t *decoded_len_out) {
    size_t total_len = reader->carry_len + input_len;
    size_t decoded_len = 0;
    size_t offset = 0;

    *decoded_len_out = 0;

    while (offset + 4 <= total_len) {
        size_t group_len = 0;
        bool is_final = false;

        ImgnekoBase64Status status = decode_quartet(
            reader->buffer + offset, out + decoded_len, &group_len, &is_final);

        if (status != IMGNEKO_BASE64_OK)
            return base64_decode_reader_fail(reader, status, decoded_len,
                                             decoded_len_out);

        offset += 4;
        decoded_len += group_len;

        if (is_final) {
            if (offset != total_len)
                return base64_decode_reader_fail(reader,
                                                 IMGNEKO_BASE64_INVALID_INPUT,
                                                 decoded_len, decoded_len_out);

            ImgnekoReaderStatus eof_status = verify_decode_source_eof(reader);
            if (eof_status != IMGNEKO_READER_OK)
                return base64_decode_reader_finish_error(
                    reader, eof_status, decoded_len, decoded_len_out);

            reader->eof = true;
            break;
        }
    }

    if (reader->eof) {
        // A final padded quartet has already been verified against source EOF.
        reader->carry_len = 0;
    } else {
        // Preserve any partial quartet for the next source read.
        reader->carry_len = total_len - offset;
        if (reader->carry_len != 0)
            memmove(reader->buffer, reader->buffer + offset, reader->carry_len);
    }

    *decoded_len_out = decoded_len;
    return IMGNEKO_READER_OK;
}

// Reader callback for base64-decoding bytes from an underlying source.
static ImgnekoReaderStatus base64_decode_reader_func(void *ctx, char *out,
                                                     size_t out_cap,
                                                     size_t *len_out) {
    ImgnekoBase64DecodeReader *reader = ctx;

    assert(len_out != NULL);
    assert(reader != NULL);
    assert(out != NULL || out_cap == 0);

    *len_out = 0;

    if (reader->eof)
        return IMGNEKO_READER_EOF;

    if (reader->error_status != IMGNEKO_BASE64_OK)
        return IMGNEKO_READER_ERROR;

    if (reader->pending_status != IMGNEKO_READER_OK)
        return reader->pending_status;

    if (reader->buffer == NULL || reader->buffer_cap < 4)
        return IMGNEKO_READER_WORKSPACE_TOO_SMALL;

    if (out_cap < 3) {
        *len_out = 3;
        return IMGNEKO_READER_BUFFER_TOO_SMALL;
    }

    // Read the source in a loop until we can produce decoded bytes or learn
    // that the encoded stream has ended.
    while (true) {
        // Read only enough encoded data to produce complete decoded groups that
        // fit in this call's output buffer (and the `buffer` workspace).
        size_t cap = base64_decode_reader_input_cap(reader, out_cap);
        // IMGNEKO_UNCOVERED_OK[3 lines]: `base64_decode_reader_input_cap()`
        // returns zero only for state rejected before this call.
        if (cap == 0)
            return IMGNEKO_READER_WORKSPACE_TOO_SMALL;

        size_t len = 0;
        ImgnekoReaderStatus status = imgneko_reader_read(
            reader->source, reader->buffer + reader->carry_len, cap, &len);

        if (status == IMGNEKO_READER_OK) {
            if (len == 0 || len > cap)
                return IMGNEKO_READER_ERROR;

            size_t decoded_len = 0;
            status = base64_decode_reader_decode_buffer(reader, len, out,
                                                        &decoded_len);
            if (status != IMGNEKO_READER_OK)
                return status;

            if (decoded_len != 0) {
                *len_out = decoded_len;
                return IMGNEKO_READER_OK;
            }

            continue;
        }

        if (status == IMGNEKO_READER_EOF) {
            if (reader->carry_len != 0)
                return base64_decode_reader_fail(
                    reader, IMGNEKO_BASE64_TRUNCATED_INPUT, 0, len_out);

            reader->eof = true;
            return IMGNEKO_READER_EOF;
        }

        return translate_source_error(status);
    }

    return IMGNEKO_READER_ERROR; // IMGNEKO_UNCOVERED_OK
}

ImgnekoReader
imgneko_base64_decode_reader_as_reader(ImgnekoBase64DecodeReader *reader) {
    return (ImgnekoReader){
        .read = base64_decode_reader_func,
        .ctx = reader,
    };
}
