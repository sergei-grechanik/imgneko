// Enable POSIX APIs used in this file (getcwd).
#define _POSIX_C_SOURCE 200809L

#include "util/path.h"

#include <limits.h>
#include <stdlib.h>
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

bool path_resolve_absolute(String *out, const char *path) {
    char cwd[PATH_MAX];
    String resolved = str_empty;

    str_free(*out);

    if (path_is_absolute(path)) {
        resolved = str_from_cstr(path);
    } else {
        if (getcwd(cwd, sizeof(cwd)) == NULL)
            return false;
        resolved = str_from_cstr(cwd);
        path_append(&resolved, path);
    }

    path_trim_trailing_slashes(&resolved);
    *out = resolved;
    return true;
}
