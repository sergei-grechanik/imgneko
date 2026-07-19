// SPDX-License-Identifier: MIT-0

// Implementation of streaming RFC 1950 zlib reader transformers.

#include "imgneko/zlib.h"

#include <assert.h>
#include <limits.h>

//===----------------------------------------------------------------------===//
// Shared Reader Helpers
//===----------------------------------------------------------------------===//

const char *imgneko_zlib_error_string(ImgnekoZlibStatus status) {
    switch (status) {
    case IMGNEKO_ZLIB_OK:
        return "ok";
    case IMGNEKO_ZLIB_INVALID_ARGUMENT:
        return "invalid zlib reader argument";
    case IMGNEKO_ZLIB_STREAM_ERROR:
        return "zlib stream error";
    case IMGNEKO_ZLIB_INVALID_INPUT:
        return "invalid zlib input";
    case IMGNEKO_ZLIB_TRUNCATED_INPUT:
        return "truncated zlib input";
    case IMGNEKO_ZLIB_TRAILING_INPUT:
        return "trailing zlib input";
    case IMGNEKO_ZLIB_DICTIONARY_REQUIRED:
        return "zlib input requires a dictionary";
    }

    return "unknown zlib error";
}

// zlib represents output capacities with uInt. Limit each callback result to
// UINT_MAX bytes instead of narrowing a larger caller-provided capacity.
static uInt zlib_uInt_cap(size_t cap) {
    // IMGNEKO_UNCOVERED_OK[2 lines]: Not testing with > 4 GiB output buffers.
    if (cap > UINT_MAX)
        return UINT_MAX;
    return (uInt)cap;
}

// Propagate custom source errors, but treat source capacity failures as
// transformer workspace failures because callers cannot fix them by resizing
// the output buffer.
static int zlib_translate_source_error(int status) {
    if (status == IMGNEKO_READER_BUFFER_TOO_SMALL)
        return IMGNEKO_READER_WORKSPACE_TOO_SMALL;
    return status;
}

// Fill an empty zlib input buffer from `source`.
//
// `source`
//     Reader providing bytes for the zlib stream.
// `buffer`
//     Borrowed workspace receiving the source bytes.
// `buffer_cap`
//     Number of bytes available in `buffer`.
// `stream`
//     zlib stream whose empty input fields receive the source bytes.
// `source_eof_out`
//     Output parameter set to true when `source` reports EOF.
//
// Returns IMGNEKO_READER_OK after loading input or observing source EOF.
// Other statuses are source failures translated for transformer semantics.
static int zlib_reader_fill_input(ImgnekoReader source, char *buffer,
                                  size_t buffer_cap, z_stream *stream,
                                  bool *source_eof_out) {
    assert(buffer != NULL);
    assert(buffer_cap != 0);
    assert(buffer_cap <= UINT_MAX);
    assert(stream != NULL);
    assert(stream->avail_in == 0);
    assert(source_eof_out != NULL);

    uInt input_cap = (uInt)buffer_cap;
    size_t input_len = 0;
    int status = imgneko_reader_read(source, buffer, input_cap, &input_len);

    if (status == IMGNEKO_READER_OK) {
        if (input_len == 0 || input_len > input_cap)
            return IMGNEKO_READER_ERROR;

        stream->next_in = (Bytef *)buffer;
        stream->avail_in = (uInt)input_len;
        return IMGNEKO_READER_OK;
    }

    if (status == IMGNEKO_READER_EOF) {
        *source_eof_out = true;
        return IMGNEKO_READER_OK;
    }

    return zlib_translate_source_error(status);
}

// Record a detailed zlib status and expose it through the generic reader API.
// The caller must check `error_status` before advancing zlib again, which makes
// the failure sticky without a separate pending reader status. If zlib already
// produced bytes, return them now and expose the generic error on the next
// call.
//
// `error_status`
//     Detailed status field updated with `zlib_status`.
// `zlib_status`
//     Detailed non-OK zlib status to record.
// `output_len`
//     Number of bytes already written to the caller's output buffer.
// `len_out`
//     Output parameter receiving `output_len` when bytes must be returned
//     first.
static int zlib_reader_fail(ImgnekoZlibStatus *error_status,
                            ImgnekoZlibStatus zlib_status, size_t output_len,
                            size_t *len_out) {
    assert(error_status != NULL);
    assert(zlib_status != IMGNEKO_ZLIB_OK);
    assert(len_out != NULL);

    *error_status = zlib_status;
    *len_out = output_len;
    return output_len == 0 ? IMGNEKO_READER_ERROR : IMGNEKO_READER_OK;
}

//===----------------------------------------------------------------------===//
// Compression Reader
//===----------------------------------------------------------------------===//

ImgnekoZlibStatus
imgneko_zlib_compress_reader_init(ImgnekoZlibCompressReader *reader,
                                  ImgnekoReader source, char *buffer,
                                  size_t buffer_cap) {
    assert(reader != NULL);

    *reader = (ImgnekoZlibCompressReader){
        .source = source,
        .buffer = buffer,
        .buffer_cap = buffer_cap,
        .error_status = IMGNEKO_ZLIB_OK,
        .needs_input = true,
    };

    if (buffer == NULL || buffer_cap == 0 || buffer_cap > UINT_MAX) {
        reader->error_status = IMGNEKO_ZLIB_INVALID_ARGUMENT;
        return reader->error_status;
    }

    // IMGNEKO_UNCOVERED_OK[3 lines]
    if (deflateInit(&reader->stream, Z_DEFAULT_COMPRESSION) != Z_OK) {
        reader->error_status = IMGNEKO_ZLIB_STREAM_ERROR;
        return reader->error_status;
    }

    reader->stream_initialized = true;
    return IMGNEKO_ZLIB_OK;
}

void imgneko_zlib_compress_reader_deinit(ImgnekoZlibCompressReader *reader) {
    assert(reader != NULL);

    if (reader->stream_initialized)
        deflateEnd(&reader->stream);

    *reader = (ImgnekoZlibCompressReader){0};
}

// Reader callback for zlib-compressing bytes from an underlying source.
static int zlib_compress_reader_func(void *ctx, char *out, size_t out_cap,
                                     size_t *len_out) {
    ImgnekoZlibCompressReader *reader = ctx;

    assert(reader != NULL);
    assert(out != NULL || out_cap == 0);
    assert(len_out != NULL);

    *len_out = 0;

    if (reader->eof)
        return IMGNEKO_READER_EOF;

    if (reader->error_status != IMGNEKO_ZLIB_OK)
        return IMGNEKO_READER_ERROR;

    if (!reader->stream_initialized) {
        reader->error_status = IMGNEKO_ZLIB_STREAM_ERROR;
        return IMGNEKO_READER_ERROR;
    }

    if (reader->buffer == NULL || reader->buffer_cap == 0)
        return IMGNEKO_READER_WORKSPACE_TOO_SMALL;

    if (out_cap == 0) {
        *len_out = 1;
        return IMGNEKO_READER_BUFFER_TOO_SMALL;
    }

    while (true) {
        // Read more from the source only after zlib fully consumes the current
        // input.
        if (reader->needs_input && reader->stream.avail_in == 0) {
            if (!reader->source_eof) {
                int status = zlib_reader_fill_input(
                    reader->source, reader->buffer, reader->buffer_cap,
                    &reader->stream, &reader->source_eof);
                if (status != IMGNEKO_READER_OK)
                    return status;
            }

            reader->needs_input = false;
        }

        uInt output_cap = zlib_uInt_cap(out_cap);
        reader->stream.next_out = (Bytef *)out;
        reader->stream.avail_out = output_cap;

        int zstatus = deflate(&reader->stream,
                              reader->source_eof ? Z_FINISH : Z_NO_FLUSH);
        size_t output_len = output_cap - reader->stream.avail_out;

        if (zstatus == Z_STREAM_END) {
            reader->needs_input = false;
            reader->eof = true;
            if (output_len != 0) {
                *len_out = output_len;
                return IMGNEKO_READER_OK;
            }
            return IMGNEKO_READER_EOF;
        }

        // With output space available, Z_BUF_ERROR means deflate() has neither
        // pending input nor output and is not finishing the stream. Normally it
        // occurs only with an empty input buffer, before we reach source EOF.
        // IMGNEKO_UNCOVERED_OK[2 lines]
        if (zstatus == Z_BUF_ERROR && !reader->source_eof &&
            reader->stream.avail_in == 0) {
            // Fetch another source chunk and retry.
            reader->needs_input = true;
            continue;
        }

        if (zstatus != Z_OK) {
            reader->needs_input = false;
            return zlib_reader_fail(&reader->error_status,
                                    IMGNEKO_ZLIB_STREAM_ERROR, output_len,
                                    len_out);
        }

        reader->needs_input = !reader->source_eof &&
                              reader->stream.avail_in == 0 &&
                              reader->stream.avail_out != 0;

        if (output_len != 0) {
            *len_out = output_len;
            return IMGNEKO_READER_OK;
        }
    }
}

ImgnekoReader
imgneko_zlib_compress_reader_as_reader(ImgnekoZlibCompressReader *reader) {
    return (ImgnekoReader){
        .read = zlib_compress_reader_func,
        .ctx = reader,
    };
}

//===----------------------------------------------------------------------===//
// Decompression Reader
//===----------------------------------------------------------------------===//

ImgnekoZlibStatus
imgneko_zlib_decompress_reader_init(ImgnekoZlibDecompressReader *reader,
                                    ImgnekoReader source, char *buffer,
                                    size_t buffer_cap) {
    assert(reader != NULL);

    *reader = (ImgnekoZlibDecompressReader){
        .source = source,
        .buffer = buffer,
        .buffer_cap = buffer_cap,
        .error_status = IMGNEKO_ZLIB_OK,
        .needs_input = true,
    };

    if (buffer == NULL || buffer_cap == 0 || buffer_cap > UINT_MAX) {
        reader->error_status = IMGNEKO_ZLIB_INVALID_ARGUMENT;
        return reader->error_status;
    }

    // IMGNEKO_UNCOVERED_OK[3 lines]
    if (inflateInit(&reader->stream) != Z_OK) {
        reader->error_status = IMGNEKO_ZLIB_STREAM_ERROR;
        return reader->error_status;
    }

    reader->stream_initialized = true;
    return IMGNEKO_ZLIB_OK;
}

void imgneko_zlib_decompress_reader_deinit(
    ImgnekoZlibDecompressReader *reader) {
    assert(reader != NULL);

    if (reader->stream_initialized)
        inflateEnd(&reader->stream);

    *reader = (ImgnekoZlibDecompressReader){0};
}

// Verify that no bytes follow a complete zlib stream.
//
// `source`
//     Reader checked for bytes after the complete zlib stream.
// `buffer`
//     Borrowed workspace used for the verification read.
// `buffer_cap`
//     Number of bytes available in `buffer`.
// `has_trailing_input_out`
//     Output parameter set to true when `source` provides bytes after the
//     complete zlib stream.
//
// Returns IMGNEKO_READER_OK after observing source EOF or trailing bytes.
// Other statuses are source failures translated for transformer semantics.
static int zlib_reader_verify_source_eof(ImgnekoReader source, char *buffer,
                                         size_t buffer_cap,
                                         bool *has_trailing_input_out) {
    assert(buffer != NULL);
    assert(buffer_cap != 0);
    assert(buffer_cap <= UINT_MAX);
    assert(has_trailing_input_out != NULL);

    *has_trailing_input_out = false;

    uInt input_cap = (uInt)buffer_cap;
    size_t input_len = 0;
    int status = imgneko_reader_read(source, buffer, input_cap, &input_len);

    if (status == IMGNEKO_READER_EOF)
        return IMGNEKO_READER_OK;

    if (status == IMGNEKO_READER_OK) {
        if (input_len == 0 || input_len > input_cap)
            return IMGNEKO_READER_ERROR;
        *has_trailing_input_out = true;
        return IMGNEKO_READER_OK;
    }

    return zlib_translate_source_error(status);
}

// Finish a decompression stream after inflate() has reported Z_STREAM_END.
// `len_out` receives `output_len` when the completed stream emitted bytes.
static int zlib_decompress_reader_finish(ImgnekoZlibDecompressReader *reader,
                                         size_t output_len, size_t *len_out) {
    int status = IMGNEKO_READER_OK;
    bool has_trailing_input = false;

    reader->needs_input = false;

    if (reader->stream.avail_in != 0)
        return zlib_reader_fail(&reader->error_status,
                                IMGNEKO_ZLIB_TRAILING_INPUT, output_len,
                                len_out);

    if (!reader->source_eof) {
        status = zlib_reader_verify_source_eof(reader->source, reader->buffer,
                                               reader->buffer_cap,
                                               &has_trailing_input);
    }

    if (has_trailing_input)
        return zlib_reader_fail(&reader->error_status,
                                IMGNEKO_ZLIB_TRAILING_INPUT, output_len,
                                len_out);

    if (status != IMGNEKO_READER_OK) {
        if (output_len == 0)
            return status;

        // Preserve valid output before exposing the source failure.
        reader->pending_source_status = status;
        *len_out = output_len;
        return IMGNEKO_READER_OK;
    }

    reader->eof = true;
    if (output_len != 0) {
        *len_out = output_len;
        return IMGNEKO_READER_OK;
    }
    return IMGNEKO_READER_EOF;
}

// Reader callback for zlib-decompressing bytes from an underlying source.
static int zlib_decompress_reader_func(void *ctx, char *out, size_t out_cap,
                                       size_t *len_out) {
    ImgnekoZlibDecompressReader *reader = ctx;

    assert(reader != NULL);
    assert(out != NULL || out_cap == 0);
    assert(len_out != NULL);

    *len_out = 0;

    if (reader->eof)
        return IMGNEKO_READER_EOF;

    if (reader->pending_source_status != IMGNEKO_READER_OK)
        return reader->pending_source_status;

    if (reader->error_status != IMGNEKO_ZLIB_OK)
        return IMGNEKO_READER_ERROR;

    if (!reader->stream_initialized) {
        reader->error_status = IMGNEKO_ZLIB_STREAM_ERROR;
        return IMGNEKO_READER_ERROR;
    }

    if (reader->buffer == NULL || reader->buffer_cap == 0) {
        return IMGNEKO_READER_WORKSPACE_TOO_SMALL;
    }

    if (out_cap == 0) {
        *len_out = 1;
        return IMGNEKO_READER_BUFFER_TOO_SMALL;
    }

    while (true) {
        // Read more from the source only after zlib fully consumes the current
        // input.
        if (reader->needs_input && reader->stream.avail_in == 0) {
            if (!reader->source_eof) {
                int status = zlib_reader_fill_input(
                    reader->source, reader->buffer, reader->buffer_cap,
                    &reader->stream, &reader->source_eof);
                if (status != IMGNEKO_READER_OK)
                    return status;
            }

            reader->needs_input = false;
        }

        uInt output_cap = zlib_uInt_cap(out_cap);
        reader->stream.next_out = (Bytef *)out;
        reader->stream.avail_out = output_cap;

        // Decompress normally so inflate() can stop when either the current
        // source chunk or the caller's bounded output buffer is exhausted.
        // Stream completion is still reported with Z_STREAM_END.
        int zstatus = inflate(&reader->stream, Z_NO_FLUSH);
        size_t output_len = output_cap - reader->stream.avail_out;

        if (zstatus == Z_STREAM_END)
            return zlib_decompress_reader_finish(reader, output_len, len_out);

        if (zstatus == Z_NEED_DICT) {
            reader->needs_input = false;
            return zlib_reader_fail(&reader->error_status,
                                    IMGNEKO_ZLIB_DICTIONARY_REQUIRED,
                                    output_len, len_out);
        }

        if (zstatus == Z_DATA_ERROR) {
            reader->needs_input = false;
            return zlib_reader_fail(&reader->error_status,
                                    IMGNEKO_ZLIB_INVALID_INPUT, output_len,
                                    len_out);
        }

        // With output space available, Z_BUF_ERROR means inflate() could not
        // make progress.
        // IMGNEKO_UNCOVERED_OK: It happens only when there is no pending input
        if (zstatus == Z_BUF_ERROR && reader->stream.avail_in == 0) {
            if (!reader->source_eof) {
                // Fetch another source chunk and retry.
                reader->needs_input = true;
                continue;
            }

            // zlib still needs input, but the source has already reported EOF.
            reader->needs_input = false;
            return zlib_reader_fail(&reader->error_status,
                                    IMGNEKO_ZLIB_TRUNCATED_INPUT, output_len,
                                    len_out);
        }

        if (zstatus != Z_OK) {
            reader->needs_input = false;
            return zlib_reader_fail(&reader->error_status,
                                    IMGNEKO_ZLIB_STREAM_ERROR, output_len,
                                    len_out);
        }

        // Ask the source for another chunk only when inflate consumed this one
        // and still had caller output storage in which it could make progress.
        reader->needs_input =
            reader->stream.avail_in == 0 && reader->stream.avail_out != 0;

        if (output_len != 0) {
            if (reader->source_eof && reader->needs_input)
                return zlib_reader_fail(&reader->error_status,
                                        IMGNEKO_ZLIB_TRUNCATED_INPUT,
                                        output_len, len_out);

            *len_out = output_len;
            return IMGNEKO_READER_OK;
        }

        // A complete stream must end before zlib runs out of source input.
        // zlib normally reports this as Z_BUF_ERROR above, so this is a
        // defensive guard for an otherwise invalid inflate state.
        // IMGNEKO_UNCOVERED_OK_START
        if (reader->source_eof && reader->needs_input) {
            return zlib_reader_fail(&reader->error_status,
                                    IMGNEKO_ZLIB_TRUNCATED_INPUT, 0, len_out);
        }
        // IMGNEKO_UNCOVERED_OK_END
    }
}

ImgnekoReader
imgneko_zlib_decompress_reader_as_reader(ImgnekoZlibDecompressReader *reader) {
    return (ImgnekoReader){
        .read = zlib_decompress_reader_func,
        .ctx = reader,
    };
}
