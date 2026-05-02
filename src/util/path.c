// SPDX-License-Identifier: MIT-0

// Enable POSIX APIs used in this file (getcwd).
#define _POSIX_C_SOURCE 200809L

#include "util/path.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

bool path_is_absolute(const char *path) { return path[0] == '/'; }

void path_trim_trailing_slashes(String *path) {
    while (path->len > 1 && path->cstr[path->len - 1] == '/')
        str_truncate(*path, path->len - 1);
}

void path_append(String *path, const char *segment) {
    if (path->len > 0 && path->cstr[path->len - 1] != '/')
        str_push(*path, '/');
    str_append_cstr(*path, segment);
}

String path_join(const char *left, const char *right) {
    String result = str_from_cstr(left);
    path_append(&result, right);
    return result;
}

// Create one directory and treat an already-existing directory as success.
static bool mkdir_existing_ok(const char *path) {
    if (mkdir(path, 0755) == 0 || errno == EEXIST)
        return true;
    return false;
}

bool mkdir_p(const char *path) {
    String partial = str_empty;
    size_t index = 0;
    int saved_errno = 0;
    bool ok = false;

    if (path_is_absolute(path)) {
        partial = str_from_cstr("/");
        while (path[index] == '/')
            ++index;
    }

    if (path[index] == '\0') {
        if (partial.len == 0) {
            errno = ENOENT;
            goto cleanup;
        }

        ok = mkdir_existing_ok(partial.cstr);
        // On normal POSIX systems, `mkdir("/")` reports `EEXIST`, so this
        // should always be ok. IMGNEKO_UNCOVERED_OK[2 lines]
        if (!ok)
            saved_errno = errno;

        goto cleanup;
    }

    while (path[index] != '\0') {
        size_t component_start = index;

        while (path[index] != '\0' && path[index] != '/')
            ++index;

        if (partial.len > 0 && partial.cstr[partial.len - 1] != '/')
            str_push(partial, '/');
        str_append_data(partial, path + component_start,
                        index - component_start);

        if (!mkdir_existing_ok(partial.cstr)) {
            saved_errno = errno;
            goto cleanup;
        }

        // Skip empty path components so `a//b///c` behaves like `a/b/c`.
        while (path[index] == '/')
            ++index;
    }

    ok = true;

cleanup:
    if (!ok && saved_errno != 0)
        errno = saved_errno;
    str_free(partial);
    return ok;
}

bool path_resolve_absolute(String *out, const char *path) {
    char cwd[PATH_MAX];
    String resolved = str_empty;

    str_free(*out);

    if (path_is_absolute(path)) {
        resolved = str_from_cstr(path);
    } else {
        // IMGNEKO_UNCOVERED_OK[2 lines]
        if (getcwd(cwd, sizeof(cwd)) == NULL)
            return false;

        resolved = str_from_cstr(cwd);
        path_append(&resolved, path);
    }

    path_trim_trailing_slashes(&resolved);
    *out = resolved;
    return true;
}
