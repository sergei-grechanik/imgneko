// SPDX-License-Identifier: MIT-0

// Base64 helpers and reader transformers.
//
// The helpers use the standard RFC 4648 alphabet with `=` padding. Decoding is
// strict: ASCII whitespace is not ignored, padding is allowed only in the final
// quartet, and non-zero padding bits are rejected.

#ifndef IMGNEKO_BASE64_H
#define IMGNEKO_BASE64_H

#include <stdbool.h>
#include <stddef.h>

#include "imgneko/reader.h"

// Base64-related status codes returned by helpers and reader transformers.
// Non-OK values intentionally do not overlap with ImgnekoReaderStatus,
// because base64 reader transformers can return them directly.
typedef enum ImgnekoBase64Status {
    // Operation completed successfully.
    IMGNEKO_BASE64_OK = 0,
    // Encoded input is not strict padded base64, including bad alphabet bytes,
    // bad padding placement, or non-zero padding bits.
    IMGNEKO_BASE64_INVALID_INPUT = 1001,
    // Encoded input ended before a final quartet was complete.
    IMGNEKO_BASE64_TRUNCATED_INPUT = 1002,
    // A computed encoded or decoded byte length would not fit in `size_t`.
    IMGNEKO_BASE64_OVERFLOW = 1003,
    // Caller-owned output storage is too small for the full operation.
    IMGNEKO_BASE64_BUFFER_TOO_SMALL = 1004,
} ImgnekoBase64Status;

// Return a stable string for `status`, or a fallback for unknown values.
const char *imgneko_base64_error_string(ImgnekoBase64Status status);

// Compute the byte length of the padded base64 encoding for an input of
// `input_len` bytes.
//
// Returns IMGNEKO_BASE64_OK on success or IMGNEKO_BASE64_OVERFLOW when the
// encoded length would not fit in a `size_t`.
ImgnekoBase64Status imgneko_base64_encoded_len(size_t input_len,
                                               size_t *len_out);

// Encode `data[0..len)` into caller-owned storage.
//
// When the encoded output is larger than `out_cap`, returns
// IMGNEKO_BASE64_BUFFER_TOO_SMALL, copies no bytes, and writes the required
// encoded length to `len_out`.
//
// `data`
//     Raw bytes to encode. It may be NULL only when `len` is zero.
// `len`
//     Number of bytes in `data`.
// `out`
//     Caller-owned buffer receiving encoded bytes. It may be NULL only when
//     `out_cap` is zero.
// `out_cap`
//     Number of bytes available in `out`.
// `len_out`
//     Output parameter receiving the required encoded byte length. It must not
//     be NULL.
ImgnekoBase64Status imgneko_base64_encode(const char *data, size_t len,
                                          char *out, size_t out_cap,
                                          size_t *len_out);

// Compute the decoded byte length implied by the base64 span length and final
// padding.
//
// This function is O(1). It does not validate the alphabet, padding bits, or
// padding placement outside the final quartet. Full validation should happen
// during decoding. Returns IMGNEKO_BASE64_INVALID_INPUT when the input length
// is not a multiple of 4 or the final padding shape cannot represent padded
// base64.
//
// `data`
//     Encoded bytes to measure. It may be NULL only when `len` is zero.
// `len`
//     Number of bytes in `data`.
// `len_out`
//     Output parameter receiving the decoded byte length. It must not be NULL.
ImgnekoBase64Status imgneko_base64_decoded_len(const char *data, size_t len,
                                               size_t *len_out);

// Decode `data[0..len)` into caller-owned storage.
//
// When the decoded output is larger than `out_cap`, returns
// IMGNEKO_BASE64_BUFFER_TOO_SMALL, copies no bytes, and writes the required
// decoded length to `len_out`.
//
// `data`
//     Encoded bytes to decode. It may be NULL only when `len` is zero.
// `len`
//     Number of bytes in `data`.
// `out`
//     Caller-owned buffer receiving decoded bytes. It may be NULL only when
//     `out_cap` is zero.
// `out_cap`
//     Number of bytes available in `out`.
// `len_out`
//     Output parameter receiving the required decoded byte length. It must not
//     be NULL.
ImgnekoBase64Status imgneko_base64_decode(const char *data, size_t len,
                                          char *out, size_t out_cap,
                                          size_t *len_out);

// Reader transformer that base64-encodes bytes pulled from `source`.
typedef struct ImgnekoBase64EncodeReader {
    // Underlying reader providing raw bytes.
    ImgnekoReader source;
    // Borrowed workspace used to store carried raw bytes and read from
    // `source`.
    char *buffer;
    size_t buffer_cap;
    // Number of raw bytes currently carried in `buffer`.
    size_t carry_len;
    // True after this transformer has reported EOF.
    bool eof;
} ImgnekoBase64EncodeReader;

// Initialize a base64 encoding reader transformer over `source`.
//
// The transformer borrows `buffer` as input workspace. The buffer must remain
// alive and unmodified by the caller while `reader` is used. `buffer_cap` must
// be at least 3 bytes, and it must also be large enough for any source reader
// that requires complete logical chunks.
//
// A larger buffer reduces calls into `source`. Prefer a buffer length that is a
// multiple of 3 so the encoder can consume the whole workspace without leaving
// avoidable carry bytes.
void imgneko_base64_encode_reader_init(ImgnekoBase64EncodeReader *reader,
                                       ImgnekoReader source, char *buffer,
                                       size_t buffer_cap);

// Return a generic reader view of `reader`. The caller must keep `reader`
// alive while the returned value is used.
ImgnekoReader
imgneko_base64_encode_reader_as_reader(ImgnekoBase64EncodeReader *reader);

// Reader transformer that base64-decodes bytes pulled from `source`.
typedef struct ImgnekoBase64DecodeReader {
    // Underlying reader providing encoded bytes.
    ImgnekoReader source;
    // Borrowed workspace used to store carried encoded bytes and source data.
    char *buffer;
    size_t buffer_cap;
    // Number of encoded bytes currently carried in `buffer`.
    size_t carry_len;
    // Deferred status reported after already-decoded bytes have been emitted.
    int pending_status;
    // True after this transformer has reported EOF.
    bool eof;
} ImgnekoBase64DecodeReader;

// Initialize a base64 decoding reader transformer over `source`.
//
// The transformer borrows `buffer` as input workspace. The buffer must remain
// alive and unmodified by the caller while `reader` is used. `buffer_cap` must
// be at least 4 bytes, and it must also be large enough for any source reader
// that requires complete logical chunks.
//
// A larger buffer reduces calls into `source`. Prefer a buffer length that is a
// multiple of 4 so the decoder can consume the whole workspace without leaving
// avoidable carried quartet bytes.
void imgneko_base64_decode_reader_init(ImgnekoBase64DecodeReader *reader,
                                       ImgnekoReader source, char *buffer,
                                       size_t buffer_cap);

// Return a generic reader view of `reader`. The caller must keep `reader`
// alive while the returned value is used.
ImgnekoReader
imgneko_base64_decode_reader_as_reader(ImgnekoBase64DecodeReader *reader);

#endif
