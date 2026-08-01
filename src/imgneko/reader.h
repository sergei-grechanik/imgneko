// SPDX-License-Identifier: MIT-0

// Pull-based byte readers used by imgneko APIs.
//
// This API lets byte sources and byte transformers share the same interface.
// Callers repeatedly ask a reader to fill caller-owned storage until it reports
// EOF.
//
// Some readers expose an unbounded byte stream and may fill the caller's buffer
// with any non-empty span that fits. Other readers have message or chunk
// boundaries and return a complete boundary-delimited unit at a time.

#ifndef IMGNEKO_READER_H
#define IMGNEKO_READER_H

#include <stdbool.h>
#include <stddef.h>

// Standard results from a reader callback.
typedef enum ImgnekoReaderStatus {
    // Bytes were copied to the caller's buffer. `len_out` is greater than zero.
    IMGNEKO_READER_OK = 0,
    // No more bytes are available. The reader copies no bytes, writes zero to
    // `len_out`, and keeps returning EOF until it is reset.
    IMGNEKO_READER_EOF = 1,
    // The caller's buffer is too small for the next logical chunk. The reader
    // copies no bytes and remains observationally unchanged. It writes a
    // required retry size greater than `out_cap` to `len_out`, using a
    // best-effort guess if the exact size cannot be determined.
    IMGNEKO_READER_BUFFER_TOO_SMALL = 2,
    // The read failed because of an invalid argument or an underlying I/O
    // error. The reader copies no bytes.
    IMGNEKO_READER_ERROR = 3,
    // A transformer cannot make progress because its internal or caller-owned
    // workspace is too small. The reader copies no bytes, and callers must not
    // interpret `len_out` as a retry size for `out`.
    IMGNEKO_READER_WORKSPACE_TOO_SMALL = 4,
} ImgnekoReaderStatus;

// Return a stable string for a reader status, or a fallback for unknown values.
const char *imgneko_reader_status_string(ImgnekoReaderStatus status);

// Pull callback for the next logical byte chunk.
//
// This function must do one of the following:
// - Copy a non-empty span of bytes to `out` smaller than `out_cap`, write the
//   copied length to `len_out`, and return IMGNEKO_READER_OK. The bytes beyond
//   `len_out` in `out` must remain unchanged.
// - Return IMGNEKO_READER_BUFFER_TOO_SMALL, copy no bytes to `out`, and write a
//   required or best-effort retry size greater than `out_cap` to `len_out`.
// - Return IMGNEKO_READER_EOF, copy no bytes to `out`, and write zero to
//   `len_out`. The reader must keep returning EOF until it is reset.
// - Return IMGNEKO_READER_ERROR or IMGNEKO_READER_WORKSPACE_TOO_SMALL, copy no
//   bytes to `out`, and write zero to `len_out`.
//
// A reader never returns an implementation-specific status. A concrete reader
// that has detailed failure information must record it in its own state and
// return IMGNEKO_READER_ERROR through this interface.
//
// `ctx`
//     Opaque context owned by the concrete reader.
// `out`
//     Caller-owned buffer receiving bytes. It may be NULL only when `out_cap`
//     is zero.
// `out_cap`
//     Number of bytes available in `out`.
// `len_out`
//     Output parameter receiving the number of copied bytes, or the required
//     or best-effort retry size for IMGNEKO_READER_BUFFER_TOO_SMALL. It must
//     not be NULL.
typedef ImgnekoReaderStatus (*ImgnekoReaderFunc)(void *ctx, char *out,
                                                 size_t out_cap,
                                                 size_t *len_out);

// A byte reader from an implementation-specific source.
typedef struct ImgnekoReader {
    // Callback that reads the next logical byte chunk.
    ImgnekoReaderFunc read;
    // Opaque callback context interpreted by `read`.
    void *ctx;
} ImgnekoReader;

// Read the next logical byte chunk through `reader`.
//
// This helper returns IMGNEKO_READER_ERROR when `reader.read` is NULL,
// `len_out` is NULL, or `out` is NULL with a nonzero `out_cap`.
//
// `reader`
//     Reader value containing a callback and context.
// `out`
//     Caller-owned buffer receiving bytes. It may be NULL only when `out_cap`
//     is zero.
// `out_cap`
//     Number of bytes available in `out`.
// `len_out`
//     Output parameter receiving the number of copied bytes, or the required
//     or best-effort retry size for IMGNEKO_READER_BUFFER_TOO_SMALL. It must
//     not be NULL.
static inline ImgnekoReaderStatus imgneko_reader_read(ImgnekoReader reader,
                                                      char *out, size_t out_cap,
                                                      size_t *len_out) {
    // IMGNEKO_UNCOVERED_OK[2 lines]
    if (len_out == NULL)
        return IMGNEKO_READER_ERROR;

    *len_out = 0;

    // IMGNEKO_UNCOVERED_OK[2 lines]
    if (reader.read == NULL || (out == NULL && out_cap != 0))
        return IMGNEKO_READER_ERROR;

    return reader.read(reader.ctx, out, out_cap, len_out);
}

// Reader over a borrowed memory span. The reader does not copy `data`, so the
// caller must keep it alive until the reader is no longer used or is reset.
typedef struct ImgnekoMemoryReader {
    // Borrowed memory span being read.
    const char *data;
    // Number of bytes in `data`.
    size_t len;
    // Number of bytes already returned to the caller.
    size_t offset;
} ImgnekoMemoryReader;

// Initialize a memory reader over `data[0..len)`. `data` may be NULL only when
// `len` is zero.
void imgneko_memory_reader_init(ImgnekoMemoryReader *reader, const char *data,
                                size_t len);

// Reader callback for an ImgnekoMemoryReader context.
ImgnekoReaderStatus imgneko_memory_reader_func(void *ctx, char *out,
                                               size_t out_cap, size_t *len_out);

// Return a generic reader view of `reader`. The caller must keep `reader`
// alive while the returned value is used.
static inline ImgnekoReader
imgneko_memory_reader_as_reader(ImgnekoMemoryReader *reader) {
    return (ImgnekoReader){
        .read = imgneko_memory_reader_func,
        .ctx = reader,
    };
}

// Reader over a borrowed file descriptor. The reader never closes `fd`; the
// caller owns the descriptor and must keep it open while the reader is used.
typedef struct ImgnekoFdReader {
    int fd;
    bool eof;
} ImgnekoFdReader;

// Initialize a file descriptor reader over the already-open descriptor `fd`.
void imgneko_fd_reader_init(ImgnekoFdReader *reader, int fd);

// Reader callback for an ImgnekoFdReader context.
ImgnekoReaderStatus imgneko_fd_reader_func(void *ctx, char *out, size_t out_cap,
                                           size_t *len_out);

// Return a generic reader view of `reader`. The caller must keep `reader`
// alive while the returned value is used.
static inline ImgnekoReader
imgneko_fd_reader_as_reader(ImgnekoFdReader *reader) {
    return (ImgnekoReader){
        .read = imgneko_fd_reader_func,
        .ctx = reader,
    };
}

// Reader that opens a file path and owns the resulting descriptor. The path
// string is used only during initialization and is not stored.
typedef struct ImgnekoFileReader {
    ImgnekoFdReader fd_reader;
} ImgnekoFileReader;

// Open `path` for reading and initialize `reader` with ownership of the opened
// descriptor. Returns 0 on success and -1 on failure.
int imgneko_file_reader_init(ImgnekoFileReader *reader, const char *path);

// Close the owned descriptor, if any, and reset `reader` to an inert state.
void imgneko_file_reader_deinit(ImgnekoFileReader *reader);

// Return a generic reader view of `reader`. The caller must keep `reader`
// alive while the returned value is used.
static inline ImgnekoReader
imgneko_file_reader_as_reader(ImgnekoFileReader *reader) {
    // IMGNEKO_UNCOVERED_OK
    return imgneko_fd_reader_as_reader(reader == NULL ? NULL
                                                      : &reader->fd_reader);
}

#endif
