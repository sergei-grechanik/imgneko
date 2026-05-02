// SPDX-License-Identifier: MIT-0

#ifndef UTIL_IO_H
#define UTIL_IO_H

#include <limits.h>
#include <stddef.h>
#include <unistd.h>

#if defined(_POSIX_PIPE_BUF)
#define BUFFERED_WRITER_FALLBACK_CAPACITY _POSIX_PIPE_BUF
#else
#define BUFFERED_WRITER_FALLBACK_CAPACITY 512
#endif

// Buffered writer that tries to make writes as atomic as possible.
//
// The writer owns its internal buffer but not its file descriptor. Call
// buffered_writer_flush() before reusing the descriptor for unbuffered output,
// and call buffered_writer_free() before returning from the scope that owns the
// writer.
typedef struct BufferedWriter {
    int fd;
    size_t len;
    size_t capacity;
    char *buffer;
} BufferedWriter;

// Create a buffered writer for `fd`. The caller remains responsible for
// freeing the writer with buffered_writer_free() and closing `fd` when
// appropriate.
BufferedWriter buffered_writer_for_fd(int fd);

// Free resources owned by the writer. This does not flush buffered data.
void buffered_writer_free(BufferedWriter *writer);

// Flush any buffered bytes to the writer's file descriptor.
void buffered_writer_flush(BufferedWriter *writer);

// Ensure at least `bytes` bytes are available in the buffer, flushing first if
// needed. Requests larger than the buffer capacity leave the buffer empty, but
// don't extend the capacity.
void buffered_writer_make_room(BufferedWriter *writer, size_t bytes);

// Write raw bytes through the writer, buffering chunks that fit and writing
// larger chunks directly after flushing existing buffered data.
void buffered_writer_write(BufferedWriter *writer, const char *data,
                           size_t len);

// Format text and write it through the writer.
void buffered_writer_printf(BufferedWriter *writer, const char *fmt, ...);

#endif
