// SPDX-License-Identifier: MIT-0

// Implementation of generic imgneko byte readers.

// Enable POSIX definitions for open(), read(), and close().
#define _POSIX_C_SOURCE 200809L

#include "imgneko/reader.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

int imgneko_memory_reader_func(void *ctx, char *out, size_t out_cap,
                               size_t *len_out) {
    ImgnekoMemoryReader *reader = ctx;

    assert(len_out != NULL);
    assert(reader != NULL);
    assert(out != NULL || out_cap == 0);
    assert(reader->data != NULL || reader->len == 0);
    assert(reader->offset <= reader->len);

    *len_out = 0;

    if (reader->offset == reader->len)
        return IMGNEKO_READER_EOF;

    if (out_cap == 0) {
        *len_out = 1;
        return IMGNEKO_READER_BUFFER_TOO_SMALL;
    }

    size_t remaining = reader->len - reader->offset;
    size_t len = remaining < out_cap ? remaining : out_cap;

    memcpy(out, reader->data + reader->offset, len);
    reader->offset += len;
    *len_out = len;
    return IMGNEKO_READER_OK;
}

void imgneko_memory_reader_init(ImgnekoMemoryReader *reader, const char *data,
                                size_t len) {
    assert(reader != NULL);
    reader->data = data;
    reader->len = len;
    reader->offset = 0;
}

// Reader callback for a borrowed file descriptor, retrying interrupted reads.
int imgneko_fd_reader_func(void *ctx, char *out, size_t out_cap,
                           size_t *len_out) {
    ImgnekoFdReader *reader = ctx;

    assert(len_out != NULL);
    assert(reader != NULL);
    assert(out != NULL || out_cap == 0);

    *len_out = 0;

    if (reader->fd < 0)
        return IMGNEKO_READER_ERROR;

    if (reader->eof)
        return IMGNEKO_READER_EOF;

    if (out_cap == 0) {
        *len_out = 1;
        return IMGNEKO_READER_BUFFER_TOO_SMALL;
    }

    ssize_t nread;
    do {
        nread = read(reader->fd, out, out_cap);
    } while (nread < 0 && errno == EINTR); // IMGNEKO_UNCOVERED_OK

    if (nread < 0)
        return IMGNEKO_READER_ERROR;

    if (nread == 0) {
        reader->eof = true;
        return IMGNEKO_READER_EOF;
    }

    *len_out = (size_t)nread;
    return IMGNEKO_READER_OK;
}

void imgneko_fd_reader_init(ImgnekoFdReader *reader, int fd) {
    assert(reader != NULL);
    reader->fd = fd;
    reader->eof = false;
}

int imgneko_file_reader_init(ImgnekoFileReader *reader, const char *path) {
    assert(reader != NULL);

    imgneko_fd_reader_init(&reader->fd_reader, -1);
    reader->fd_reader.eof = true;

    if (path == NULL)
        return -1;

    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;

    imgneko_fd_reader_init(&reader->fd_reader, fd);
    return 0;
}

void imgneko_file_reader_deinit(ImgnekoFileReader *reader) {
    assert(reader != NULL);

    if (reader->fd_reader.fd >= 0)
        close(reader->fd_reader.fd);

    reader->fd_reader.fd = -1;
    reader->fd_reader.eof = true;
}
