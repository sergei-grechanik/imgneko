#ifndef UTIL_FILE_H
#define UTIL_FILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "util/string.h"

// Read lines from `stream` into `*out`, preserving each line exactly as read.
//
// When `max_lines` is -1, keep all lines. Otherwise keep only the last
// `max_lines` lines.
//
// `*out` must hold a valid StringArray. On success, this frees the previous
// contents of `*out`, stores the newly read lines, and returns true. On
// failure, it leaves `*out` unchanged, preserves errno from the failing
// operation, and returns false.
bool file_read_stream_lines(StringArray *out, FILE *stream,
                            ptrdiff_t max_lines);

// Read `path` line-by-line into `*out`, preserving each line exactly as read.
//
// When `max_lines` is -1, keep all lines. Otherwise keep only the last
// `max_lines` lines.
//
// `*out` must hold a valid StringArray. On success, this frees the previous
// contents of `*out`, stores the newly read lines, and returns true. On
// failure, it leaves `*out` unchanged, preserves errno from the failing
// operation, and returns false.
bool file_read_lines(StringArray *out, const char *path, ptrdiff_t max_lines);

#endif
