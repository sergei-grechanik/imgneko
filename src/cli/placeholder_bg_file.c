// SPDX-License-Identifier: MIT-0

// Implementation of file-backed placeholder background formatting.

#include "cli/placeholder_bg_file.h"

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "util/array.h"
#include "util/file.h"

DEFINE_ARRAY_TYPE(StrSpanArray, StrSpan)

// A parsed file row. `data` owns the loaded line after its line ending has been
// trimmed in place, and each sequence span stores the effective sequence for a
// covered output column.
typedef struct PlaceholderBgFileRow {
    String data;
    StrSpanArray sequences;
} PlaceholderBgFileRow;

DEFINE_ARRAY_TYPE(PlaceholderBgFileRowArray, PlaceholderBgFileRow)

// Parsed background file data. Rows repeat vertically; columns beyond a row's
// explicit data keep the row's final sequence.
struct PlaceholderBgFile {
    PlaceholderBgFileRowArray rows;
    bool per_row;
};

// Store a formatted parser error in `error_out` and return NULL.
static PlaceholderBgFile *placeholder_bg_file_errorf(String *error_out,
                                                     const char *format, ...) {
    if (error_out == NULL)
        return NULL;

    va_list args;
    va_start(args, format);
    String message = str_vprintf(format, args);
    va_end(args);

    str_free(*error_out);
    *error_out = message;
    return NULL;
}

// Allocate an empty file context.
static PlaceholderBgFile *placeholder_bg_file_alloc(void) {
    PlaceholderBgFile *file = malloc(sizeof(*file));

    // IMGNEKO_UNCOVERED_OK[2 lines]
    if (file == NULL)
        arr__abort_oom();

    *file = (PlaceholderBgFile){0};
    return file;
}

// Release a row's owned sequences.
static void placeholder_bg_file_row_deinit(PlaceholderBgFileRow *row) {
    str_free(row->data);
    arr_free(row->sequences);
}

// Release all parsed rows.
static void placeholder_bg_file_rows_deinit(PlaceholderBgFileRowArray *rows) {
    for (size_t i = 0; i < rows->size; ++i)
        placeholder_bg_file_row_deinit(&rows->data[i]);
    arr_free(*rows);
}

// Drop one text line ending from a line read by file_read_lines().
static void placeholder_bg_file_trim_line_ending(String *line) {
    // IMGNEKO_UNCOVERED_OK[2 lines]: Empty files are rejected, and
    // file_read_lines() does not emit zero-byte rows for non-empty files.
    if (line->len != 0 && line->cstr[line->len - 1] == '\n')
        str_drop_back(*line, 1);
    if (line->len != 0 && line->cstr[line->len - 1] == '\r')
        str_drop_back(*line, 1);
}

// Parse one loaded file line into effective per-column sequences. Takes
// ownership of `line`; the returned row releases it in
// placeholder_bg_file_row_deinit().
static PlaceholderBgFileRow placeholder_bg_file_parse_row(String line) {
    PlaceholderBgFileRow row = {.data = line};
    size_t token_start = 0;
    bool have_last_sequence = false;
    StrSpan last_sequence = str_span(row.data.cstr, 0);

    for (size_t i = 0; i <= row.data.len; ++i) {
        if (i != row.data.len && row.data.cstr[i] != ' ')
            continue;

        StrSpan sequence = last_sequence;
        if (i > token_start) {
            sequence = str_span(row.data.cstr + token_start, i - token_start);
            last_sequence = sequence;
            have_last_sequence = true;
        } else {
            if (!have_last_sequence)
                sequence = str_span(row.data.cstr, 0);
        }
        arr_push(row.sequences, sequence);

        token_start = i + 1;
    }

    return row;
}

PlaceholderBgFile *placeholder_bg_file_load(const char *path,
                                            String *error_out) {
    StringArray lines = arr_empty;
    PlaceholderBgFile *file = NULL;
    PlaceholderBgFile *result = NULL;

    if (!file_read_lines(&lines, path, -1)) {
        return placeholder_bg_file_errorf(
            error_out, "failed to read background file '%s': %s", path,
            strerror(errno));
    }

    if (lines.size == 0) {
        result =
            placeholder_bg_file_errorf(error_out, "background file is empty");
        goto cleanup;
    }

    file = placeholder_bg_file_alloc();
    file->per_row = true;
    arr_reserve(file->rows, lines.size);
    for (size_t i = 0; i < lines.size; ++i) {
        placeholder_bg_file_trim_line_ending(&lines.data[i]);
        String line = lines.data[i];
        lines.data[i] = str_empty;
        PlaceholderBgFileRow row = placeholder_bg_file_parse_row(line);
        if (row.sequences.size != 1)
            file->per_row = false;
        arr_push(file->rows, row);
    }

    result = file;
    file = NULL;

cleanup:
    placeholder_bg_file_destroy(file);
    str_array_free(&lines);
    return result;
}

PlaceholderBgFile *placeholder_bg_file_copy(const PlaceholderBgFile *file) {
    if (file == NULL)
        return NULL;

    PlaceholderBgFile *copy = placeholder_bg_file_alloc();
    copy->per_row = file->per_row;
    arr_reserve(copy->rows, file->rows.size);

    for (size_t row_index = 0; row_index < file->rows.size; ++row_index) {
        const PlaceholderBgFileRow *src_row = &file->rows.data[row_index];
        PlaceholderBgFileRow dst_row = {0};

        dst_row.data = str_copy(src_row->data);
        arr_reserve(dst_row.sequences, src_row->sequences.size);
        for (size_t i = 0; i < src_row->sequences.size; ++i) {
            StrSpan src_sequence = src_row->sequences.data[i];
            size_t offset = (size_t)(src_sequence.data - src_row->data.cstr);
            StrSpan dst_sequence =
                str_span(dst_row.data.cstr + offset, src_sequence.len);
            arr_push(dst_row.sequences, dst_sequence);
        }
        arr_push(copy->rows, dst_row);
    }

    return copy;
}

void placeholder_bg_file_destroy(PlaceholderBgFile *file) {
    if (file == NULL)
        return;

    placeholder_bg_file_rows_deinit(&file->rows);
    free(file);
}

int placeholder_bg_file_format_func(void *ctx, const Placeholder *placeholder,
                                    uint32_t col, uint32_t row, char *out,
                                    size_t out_cap) {
    const PlaceholderBgFile *file = ctx;

    (void)placeholder;

    if (file == NULL)
        return 0;
    // Loader rejects empty files before a formatting context can be created.
    // IMGNEKO_UNCOVERED_OK[2 lines]
    if (file->rows.size == 0)
        return 0;

    const PlaceholderBgFileRow *file_row =
        &file->rows.data[(size_t)row % file->rows.size];
    // Parsing always produces at least one effective sequence per loaded line.
    // IMGNEKO_UNCOVERED_OK[2 lines]
    if (file_row->sequences.size == 0)
        return 0;

    size_t col_index = col;
    if (col_index >= file_row->sequences.size)
        col_index = file_row->sequences.size - 1;

    const StrSpan *sequence = &file_row->sequences.data[col_index];
    // IMGNEKO_UNCOVERED_OK[2 lines]
    if (sequence->len > (size_t)INT_MAX)
        return -1;
    if (sequence->len > out_cap)
        return (int)sequence->len;

    memcpy(out, sequence->data, sequence->len);
    return (int)sequence->len;
}

PlaceholderFormat placeholder_bg_file_format(PlaceholderBgFile *file) {
    if (file != NULL && file->per_row)
        return placeholder_format_dynamic_row(placeholder_bg_file_format_func,
                                              file);

    return placeholder_format_dynamic_cell(placeholder_bg_file_format_func,
                                           file);
}
