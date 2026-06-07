// SPDX-License-Identifier: MIT-0

// Generic byte writer used by imgneko APIs.
//
// ImgnekoWriter is a small callback wrapper for output sinks. Writer callbacks
// must either accept the complete byte span or report failure. Short writes are
// handled inside low-level adapters such as imgneko_writer_fd().

#ifndef IMGNEKO_WRITER_H
#define IMGNEKO_WRITER_H

#include <stddef.h>

typedef struct ImgnekoWriter {
    // Callback that accepts exactly `len` bytes from `data`, or returns -1.
    int (*write)(void *ctx, const char *data, size_t len);
    // Opaque callback context.
    void *ctx;
} ImgnekoWriter;

// Create a writer that writes to `*fd`. The caller must keep `fd` alive while
// the returned writer is used. Passing NULL as `fd` creates a writer that fails
// when used.
ImgnekoWriter imgneko_writer_fd(int *fd);

// Write exactly `len` bytes through `writer`. Returns 0 on success and -1 on
// failure. `writer.write` must be non-NULL. `data` may be NULL only when `len`
// is zero.
static inline int imgneko_writer_write(ImgnekoWriter writer, const char *data,
                                       size_t len) {
    if (writer.write == NULL || (data == NULL && len != 0))
        return -1;
    if (len == 0)
        return 0;

    return writer.write(writer.ctx, data, len);
}

#endif
