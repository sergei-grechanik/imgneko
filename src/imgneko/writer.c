// SPDX-License-Identifier: MIT-0

// Implementation of generic imgneko byte writers.

// Enable POSIX definitions for ssize_t and write().
#define _POSIX_C_SOURCE 200809L

#include "imgneko/writer.h"

#include <errno.h>
#include <sys/types.h>
#include <unistd.h>

// Writer callback that writes the full byte span to a file descriptor, retrying
// interrupted and short write(2) calls.
static int fd_writer_func(void *ctx, const char *data, size_t len) {
    if (ctx == NULL)
        return -1;

    int fd = *(int *)ctx;

    for (size_t offset = 0; offset < len;) {
        ssize_t written;

        do {
            written = write(fd, data + offset, len - offset);
        } while (written < 0 && errno == EINTR); // IMGNEKO_UNCOVERED_OK

        if (written <= 0)
            return -1;

        offset += (size_t)written;
    }

    return 0;
}

ImgnekoWriter imgneko_writer_fd(int *fd) {
    return (ImgnekoWriter){
        .write = fd_writer_func,
        .ctx = fd,
    };
}
