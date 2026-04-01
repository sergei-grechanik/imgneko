// Enable POSIX APIs used in this file (getline).
#define _POSIX_C_SOURCE 200809L

#include "util/file.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Reorder a circular buffer by moving `head` first lanes to the end of the
// array. This function modifies `lines`, but for simplicity it's not a real
// in-place algorithm.
static void rotate_tail_lines(StringArray *lines, size_t head) {
    StringArray reordered = arr_empty;

    if (head == 0)
        return;

    arr_reserve(reordered, lines->size);
    for (size_t i = 0; i < lines->size; ++i) {
        size_t index = (head + i) % lines->size;

        arr_push(reordered, lines->data[index]);
        lines->data[index] = (String)str_empty;
    }

    str_array_free(lines);
    *lines = reordered;
}

bool file_read_stream_lines(StringArray *out, FILE *stream,
                            ptrdiff_t max_lines) {
    char *line = NULL;
    size_t line_capacity = 0;
    StringArray lines = arr_empty;
    size_t tail_head = 0;
    int saved_errno = 0;
    bool keep_all = max_lines < 0;
    bool ok = false;

    while (getline(&line, &line_capacity, stream) >= 0) {
        String owned_line;

        owned_line = str_from_cstr(line);

        if (keep_all) {
            arr_push(lines, owned_line);
            continue;
        }

        if (max_lines == 0) {
            str_free(owned_line);
            continue;
        }

        if (lines.size < (size_t)max_lines) {
            arr_push(lines, owned_line);
            continue;
        }

        str_free(lines.data[tail_head]);
        lines.data[tail_head] = owned_line;
        tail_head = (tail_head + 1) % lines.size;
    }

    if (ferror(stream)) {
        saved_errno = errno;
        goto cleanup;
    }

    if (!keep_all && max_lines > 0) {
        rotate_tail_lines(&lines, tail_head);
    }

    str_array_free(out);
    *out = lines;
    lines = (StringArray)arr_empty;
    ok = true;

cleanup:
    if (!ok && saved_errno != 0)
        errno = saved_errno;
    str_array_free(&lines);
    free(line);
    return ok;
}

bool file_read_lines(StringArray *out, const char *path, ptrdiff_t max_lines) {
    FILE *stream = fopen(path, "r");
    bool ok;
    int saved_errno;

    if (stream == NULL)
        return false;

    ok = file_read_stream_lines(out, stream, max_lines);
    saved_errno = errno;
    if (fclose(stream) != 0 && ok) {
        errno = saved_errno != 0 ? saved_errno : errno;
        return false;
    }
    errno = saved_errno;
    return ok;
}
