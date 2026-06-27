// SPDX-License-Identifier: MIT-0

// File-backed placeholder background formatting.

#ifndef CLI_PLACEHOLDER_BG_FILE_H
#define CLI_PLACEHOLDER_BG_FILE_H

#include <stdint.h>

#include "imgneko/placeholder.h"
#include "util/string.h"

typedef struct PlaceholderBgFile PlaceholderBgFile;

// Load a background formatting grid from `path`.
//
// The returned file context is owned by the caller and must be released with
// placeholder_bg_file_destroy(). On failure, this stores an optional diagnostic
// in `error_out` and returns NULL.
PlaceholderBgFile *placeholder_bg_file_load(const char *path,
                                            String *error_out);

// Deep-copy a loaded background file context.
PlaceholderBgFile *placeholder_bg_file_copy(const PlaceholderBgFile *file);

// Destroy a loaded background file context.
void placeholder_bg_file_destroy(PlaceholderBgFile *file);

// Return the per-cell formatting descriptor backed by `file`.
PlaceholderFormat placeholder_bg_file_format(PlaceholderBgFile *file);

// A format function for a file-backed background format. `ctx` must be a
// PlaceholderBgFile context. This function is not intended to be called
// directly, use placeholder_bg_file_format() instead.
int placeholder_bg_file_format_func(void *ctx, const Placeholder *placeholder,
                                    uint32_t col, uint32_t row, char *out,
                                    size_t out_cap);

#endif
