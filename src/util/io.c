// SPDX-License-Identifier: MIT-0

// Enable POSIX constants used by util/io.h.
#define _POSIX_C_SOURCE 200809L

#include "util/io.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "util/error.h"

// Write a complete byte span to the descriptor, retrying short writes and
// EINTR.
static void write_all(int fd, const char *data, size_t len) {
    size_t offset = 0;

    while (offset < len) {
        ssize_t written = write(fd, data + offset, len - offset);

        // IMGNEKO_UNCOVERED_OK_START: Hard to trigger.
        if (written <= 0) {
            if (written < 0 && errno == EINTR)
                continue;
            die("failed to write buffered data: %errno");
        }
        // IMGNEKO_UNCOVERED_OK_END

        offset += (size_t)written;
    }
}

// Return the preferred buffer capacity for `fd`, using the descriptor-specific
// pipe atomic write size when the platform provides it.
static size_t buffered_writer_capacity_for_fd(int fd) {
#ifdef _PC_PIPE_BUF
    long pipe_buf = fpathconf(fd, _PC_PIPE_BUF);

    if (pipe_buf > 0)
        return (size_t)pipe_buf;
#else
    (void)fd;
#endif

    return BUFFERED_WRITER_FALLBACK_CAPACITY;
}

BufferedWriter buffered_writer_for_fd(int fd) {
    BufferedWriter writer = {
        .fd = fd,
        .capacity = buffered_writer_capacity_for_fd(fd),
    };

    writer.buffer = malloc(writer.capacity);
    require(writer.buffer != NULL,
            "failed to allocate buffered writer: %errno");
    return writer;
}

void buffered_writer_free(BufferedWriter *writer) {
    free(writer->buffer);
    writer->buffer = NULL;
    writer->capacity = 0;
    writer->len = 0;
}

void buffered_writer_flush(BufferedWriter *writer) {
    if (writer->len == 0)
        return;

    write_all(writer->fd, writer->buffer, writer->len);
    writer->len = 0;
}

void buffered_writer_make_room(BufferedWriter *writer, size_t bytes) {
    if (bytes > writer->capacity || writer->capacity - writer->len < bytes) {
        buffered_writer_flush(writer);
    }
}

void buffered_writer_write(BufferedWriter *writer, const char *data,
                           size_t len) {
    if (len == 0)
        return;

    buffered_writer_make_room(writer, len);
    if (len > writer->capacity) {
        write_all(writer->fd, data, len);
        return;
    }

    memcpy(writer->buffer + writer->len, data, len);
    writer->len += len;
}

void buffered_writer_printf(BufferedWriter *writer, const char *fmt, ...) {
    va_list args;
    va_list args_copy;
    int formatted_len;

    va_start(args, fmt);
    va_copy(args_copy, args);

    buffered_writer_make_room(writer, 1);
    formatted_len = vsnprintf(writer->buffer + writer->len,
                              writer->capacity - writer->len, fmt, args);
    require(formatted_len >= 0, "failed to format buffered data");

    if ((size_t)formatted_len < writer->capacity - writer->len) {
        writer->len += (size_t)formatted_len;
    } else {
        char *formatted;

        buffered_writer_flush(writer);
        formatted = malloc((size_t)formatted_len + 1);
        require(formatted != NULL, "failed to allocate formatted data: %errno");
        require(vsnprintf(formatted, (size_t)formatted_len + 1, fmt,
                          args_copy) == formatted_len,
                "failed to format buffered data");
        buffered_writer_write(writer, formatted, (size_t)formatted_len);
        free(formatted);
    }

    va_end(args_copy);
    va_end(args);
}
