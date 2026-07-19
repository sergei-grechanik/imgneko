// SPDX-License-Identifier: MIT-0

// Streaming RFC 1950 zlib readers.
//
// These transformers use zlib's normal wrapper format: an RFC 1950 header
// and Adler-32 trailer around an RFC 1951 DEFLATE stream. They do not produce
// or accept raw DEFLATE or gzip streams.

#ifndef IMGNEKO_ZLIB_H
#define IMGNEKO_ZLIB_H

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>

#include <zlib.h>

#include "imgneko/reader.h"

// zlib-related statuses returned by reader initialization and recorded in a
// transformer's error_status when its generic reader view fails.
typedef enum ImgnekoZlibStatus {
    // Operation completed successfully.
    IMGNEKO_ZLIB_OK = 0,
    // Reader initialization received an invalid workspace.
    IMGNEKO_ZLIB_INVALID_ARGUMENT = 1101,
    // zlib could not initialize or advance its internal stream state.
    IMGNEKO_ZLIB_STREAM_ERROR = 1102,
    // Input does not contain a valid RFC 1950 zlib stream.
    IMGNEKO_ZLIB_INVALID_INPUT = 1103,
    // Input ended before the RFC 1950 stream reached its Adler-32 checksum.
    IMGNEKO_ZLIB_TRUNCATED_INPUT = 1104,
    // Bytes follow a complete RFC 1950 stream.
    IMGNEKO_ZLIB_TRAILING_INPUT = 1105,
    // Input requires a preset compression dictionary, which this reader does
    // not support.
    IMGNEKO_ZLIB_DICTIONARY_REQUIRED = 1106,
} ImgnekoZlibStatus;

// Return a stable string for `status`, or a fallback for unknown values.
const char *imgneko_zlib_error_string(ImgnekoZlibStatus status);

// Reader transformer that zlib-compresses bytes pulled from `source`.
typedef struct ImgnekoZlibCompressReader {
    // Underlying reader providing uncompressed bytes.
    ImgnekoReader source;
    // Borrowed workspace used to read input from `source`.
    char *buffer;
    size_t buffer_cap;
    // zlib's mutable compression state.
    z_stream stream;
    // Detailed zlib or RFC 1950 failure status. It is IMGNEKO_ZLIB_OK after a
    // successful initialization and is read-only to callers.
    ImgnekoZlibStatus error_status;
    // True after deflateInit() has initialized `stream`.
    bool stream_initialized;
    // True after `source` has reported EOF.
    bool source_eof;
    // True when zlib has no pending work and should read source input.
    bool needs_input;
    // True after this transformer has reported EOF.
    bool eof;
} ImgnekoZlibCompressReader;

// Initialize an RFC 1950 compression reader transformer over `source`.
//
// `reader`
//     Transformer state to initialize.
// `source`
//     Reader providing uncompressed bytes.
// `buffer`
//     Borrowed source-input workspace. The caller must keep it alive and
//     unmodified until imgneko_zlib_compress_reader_deinit() is called.
//     A 16 KiB workspace is a practical default. Use a larger workspace only
//     when the source requires larger indivisible chunks or to reduce source
//     read overhead.
// `buffer_cap`
//     Number of bytes available in `buffer`. It must be nonzero and large
//     enough for any source reader that requires complete logical chunks, and
//     it must not exceed UINT_MAX.
//
// Returns IMGNEKO_ZLIB_OK on success. A failed initialization leaves `reader`
// safe to pass to imgneko_zlib_compress_reader_deinit() and records the
// failure in reader->error_status.
ImgnekoZlibStatus
imgneko_zlib_compress_reader_init(ImgnekoZlibCompressReader *reader,
                                  ImgnekoReader source, char *buffer,
                                  size_t buffer_cap);

// Release zlib state held by `reader` and reset it to an inert state. It is
// safe to call after a failed initialization and to call again after reset.
void imgneko_zlib_compress_reader_deinit(ImgnekoZlibCompressReader *reader);

// Return a generic reader view of `reader`. zlib and RFC 1950 failures return
// IMGNEKO_READER_ERROR; inspect reader->error_status for detail. The caller
// must keep `reader` alive while the returned value is used.
ImgnekoReader
imgneko_zlib_compress_reader_as_reader(ImgnekoZlibCompressReader *reader);

// Reader transformer that zlib-decompresses bytes pulled from `source`.
typedef struct ImgnekoZlibDecompressReader {
    // Underlying reader providing RFC 1950 zlib bytes.
    ImgnekoReader source;
    // Borrowed workspace used to read input from `source`.
    char *buffer;
    size_t buffer_cap;
    // zlib's mutable decompression state.
    z_stream stream;
    // Detailed zlib or RFC 1950 failure status. It is IMGNEKO_ZLIB_OK after a
    // successful initialization and is read-only to callers.
    ImgnekoZlibStatus error_status;
    // Source reader status deferred after already-produced output bytes.
    int pending_source_status;
    // True after inflateInit() has initialized `stream`.
    bool stream_initialized;
    // True after `source` has reported EOF.
    bool source_eof;
    // True when zlib has no pending work and should read source input.
    bool needs_input;
    // True after this transformer has reported EOF.
    bool eof;
} ImgnekoZlibDecompressReader;

// Initialize an RFC 1950 decompression reader transformer over `source`.
//
// `reader`
//     Transformer state to initialize.
// `source`
//     Reader providing RFC 1950 zlib bytes.
// `buffer`
//     Borrowed source-input workspace. The caller must keep it alive and
//     unmodified until imgneko_zlib_decompress_reader_deinit() is called.
//     A 16 KiB workspace is a practical default. Use a larger workspace only
//     when the source requires larger indivisible chunks or to reduce source
//     read overhead.
// `buffer_cap`
//     Number of bytes available in `buffer`. It must be nonzero and large
//     enough for any source reader that requires complete logical chunks, and
//     it must not exceed UINT_MAX.
//
// The decompressor validates the full input stream. It rejects truncated
// streams, a stream requiring a preset dictionary, and trailing bytes after a
// complete stream.
//
// Returns IMGNEKO_ZLIB_OK on success. A failed initialization leaves `reader`
// safe to pass to imgneko_zlib_decompress_reader_deinit() and records the
// failure in reader->error_status.
ImgnekoZlibStatus
imgneko_zlib_decompress_reader_init(ImgnekoZlibDecompressReader *reader,
                                    ImgnekoReader source, char *buffer,
                                    size_t buffer_cap);

// Release zlib state held by `reader` and reset it to an inert state. It is
// safe to call after a failed initialization and to call again after reset.
void imgneko_zlib_decompress_reader_deinit(ImgnekoZlibDecompressReader *reader);

// Return a generic reader view of `reader`. zlib and RFC 1950 failures return
// IMGNEKO_READER_ERROR; inspect reader->error_status for detail. The caller
// must keep `reader` alive while the returned value is used.
ImgnekoReader
imgneko_zlib_decompress_reader_as_reader(ImgnekoZlibDecompressReader *reader);

#endif
