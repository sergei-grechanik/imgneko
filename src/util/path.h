// SPDX-License-Identifier: MIT-0

#ifndef UTIL_PATH_H
#define UTIL_PATH_H

#include <stdbool.h>

#include "util/string.h"

// Return whether `path` names an absolute filesystem location.
bool path_is_absolute(const char *path);

// Normalize a directory path by dropping trailing slashes, while preserving
// the root path "/".
void path_trim_trailing_slashes(String *path);

// Append one path segment to `*path`, inserting a separator when needed.
void path_append(String *path, const char *segment);

// Join two path segments into a newly allocated String. The caller owns the
// returned String and frees it with str_free.
String path_join(const char *left, const char *right);

// Create `path` and any missing parent directories. Repeated separators are
// treated like a single `/`.
//
// Returns true on success. On failure, it leaves errno from the failing
// filesystem operation intact and returns false.
bool mkdir_p(const char *path);

// Resolve `path` to an absolute path, using the current working directory when
// the input is relative.
//
// `*out` must hold a valid String. This function always frees the previous
// value in `*out` first.
//
// On success, it stores a newly allocated result in `*out` and returns true.
// On failure, it leaves `errno` from the failing syscall intact, leaves `*out`
// in the empty-string state `str_empty`, and returns false.
bool path_resolve_absolute(String *out, const char *path);

#endif
