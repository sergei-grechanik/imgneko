// SPDX-License-Identifier: MIT-0

// Implementation of terminal placeholder printing. The chunker keeps
// placeholder graphemes intact, prefers line-boundary flushes, and appends a
// reset sequence to every ANSI-enabled write so formatting never leaks past
// chunk boundaries.

// Enable POSIX constants used for the default chunk size.
#define _POSIX_C_SOURCE 200809L

#include "imgneko/placeholder.h"

#include <assert.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "imgneko/rowcolumn_diacritics.h"
#include "util/common.h"
#include "util/error.h"

#define PLACEHOLDER_STACK_CHUNK_SIZE 4096
#define PLACEHOLDER_RESET "\x1b[0m"
#define PLACEHOLDER_RESET_LEN 4

#if defined(_POSIX_PIPE_BUF)
#define PLACEHOLDER_DEFAULT_CHUNK_SIZE _POSIX_PIPE_BUF
#else
#define PLACEHOLDER_DEFAULT_CHUNK_SIZE 512
#endif

#define TRY_APPEND(expr)                                                       \
    do {                                                                       \
        PlaceholderError _error = (expr);                                      \
        if (_error != PLACEHOLDER_OK)                                          \
            return _error;                                                     \
    } while (0)

// Resolved diacritic encoding plan for a validated placeholder and mode.
typedef struct PlaceholderPlan {
    // Diacritic count for the first cell in each rendered row.
    uint8_t first_cell_diacritics;
    // Diacritic count for all non-first cells in each rendered row.
    uint8_t other_cell_diacritics;
    // High image-ID byte emitted as an optional third diacritic.
    uint8_t image_id_high_byte;
    // Byte length of `PlaceholderMode.unrepresentable_cell_symbol`.
    size_t unrepresentable_cell_symbol_len;
} PlaceholderPlan;

// Buffered placeholder writer that preserves safe chunk boundaries.
typedef struct PlaceholderChunker {
    // Pending bytes to be written through `writer`.
    char *data;
    // Number of pending bytes currently stored in `data`.
    size_t len;
    // Length of the prefix of `data` that contains only completed rows. This
    // can span multiple rows and is the preferred flush length.
    size_t complete_len;
    // Capacity of `data` (equal to the maximum bytes passed to the writer in
    // one chunk).
    size_t cap;
    // Destination callback.
    ImgnekoWriter writer;
    // True when ID colors and ANSI resets are omitted.
    bool grapheme_only;
    // True when pending ANSI styling must be reset before a flush boundary.
    bool needs_reset;
} PlaceholderChunker;

// Shared context for row-start and row-end append callbacks.
typedef struct AppendRowContext {
    // Placeholder being rendered.
    const Placeholder *placeholder;
    // Effective write options.
    const PlaceholderOptions *options;
    // Zero-based row offset relative to `placeholder->rect.start_row`.
    uint32_t row;
    // First-line and last-line flags for the row.
    PlaceholderPositionFlags flags;
} AppendRowContext;

// Context for appending a single placeholder cell.
typedef struct AppendCellContext {
    // Placeholder being rendered.
    const Placeholder *placeholder;
    // Effective write options.
    const PlaceholderOptions *options;
    // Validated encoding plan.
    const PlaceholderPlan *plan;
    // Zero-based placeholder column in the same coordinate system as
    // `placeholder->rect`, relative to the image left.
    uint32_t col;
    // Zero-based placeholder row in the same coordinate system as
    // `placeholder->rect`, relative to the image top.
    uint32_t row;
    // True when this is the first rendered cell in the current row.
    bool first_cell_in_row;
} AppendCellContext;

// Writer state for `placeholder_write_to_buffer`.
typedef struct BufferWriteContext {
    // Destination buffer, or NULL when only measuring output length.
    char *out;
    // Capacity of `out`.
    size_t out_cap;
    // Total byte length requested by writes, including bytes not written to
    // `out`.
    size_t len;
} BufferWriteContext;

const char *placeholder_error_string(PlaceholderError error) {
    switch (error) {
    case PLACEHOLDER_OK:
        return "ok";
    case PLACEHOLDER_INVALID_ARGUMENT:
        return "invalid argument";
    case PLACEHOLDER_INVALID_IMAGE_ID:
        return "invalid image ID";
    case PLACEHOLDER_INVALID_PLACEMENT_ID:
        return "invalid placement ID";
    case PLACEHOLDER_INVALID_RECTANGLE:
        return "invalid rectangle";
    case PLACEHOLDER_INVALID_MODE:
        return "invalid placeholder mode";
    case PLACEHOLDER_INCOMPLETE_FIRST_COLUMN:
        return "first column information does not carry enough information";
    case PLACEHOLDER_UNREPRESENTABLE_CELL:
        return "unrepresentable cell";
    case PLACEHOLDER_UNREPRESENTABLE_COLUMN:
        return "unrepresentable column";
    case PLACEHOLDER_CHUNK_TOO_SMALL:
        return "chunk too small";
    case PLACEHOLDER_FORMAT_FAILED:
        return "format callback failed";
    case PLACEHOLDER_POSITION_FAILED:
        return "position callback failed";
    case PLACEHOLDER_WRITE_FAILED:
        return "write failed";
    }

    return "unknown placeholder error";
}

// Copy `data` to `out` if it fits and return the length of `data`. This is a
// helper used in callbacks.
//
// `out`
//     Destination buffer. It may be NULL only when `out_cap` is zero.
// `out_cap`
//     Number of bytes available in `out`.
// `data`
//     Bytes to copy when they fit.
// `len`
//     Number of bytes in `data`.
//
// Returns `len`, which may be larger than `out_cap` when the output does not
// fit, or a negative value when `len` is not representable as an `int`.
static int memcpy_counted(char *out, size_t out_cap, const char *data,
                          size_t len) {
    if (out == NULL && out_cap != 0)
        return -1;
    // IMGNEKO_UNCOVERED_OK: `strlen` cannot practically reach `INT_MAX` here.
    if (len > (size_t)INT_MAX)
        return -1; // IMGNEKO_UNCOVERED_OK
    if (len > out_cap)
        return (int)len;

    if (len != 0)
        memcpy(out, data, len);
    return (int)len;
}

// Placeholder formatting callback for static bytes.
//
// `ctx`
//     Static NUL-terminated string to emit, or NULL for no formatting.
//
// Returns the callback-style byte count for `ctx`.
static int static_format_func(void *ctx, const Placeholder *placeholder,
                              uint32_t col, uint32_t row, char *out,
                              size_t out_cap) {
    const char *data = ctx;
    (void)placeholder;
    (void)col;
    (void)row;

    if (data == NULL)
        return 0;

    return memcpy_counted(out, out_cap, data, strlen(data));
}

PlaceholderFormat placeholder_format_static(const char *data) {
    return (PlaceholderFormat){
        .func = static_format_func,
        .ctx = (void *)data,
        .per_cell = false,
    };
}

PlaceholderFormat placeholder_format_dynamic_row(PlaceholderFormatFunc func,
                                                 void *ctx) {
    return (PlaceholderFormat){
        .func = func,
        .ctx = ctx,
        .per_cell = false,
    };
}

PlaceholderFormat placeholder_format_dynamic_cell(PlaceholderFormatFunc func,
                                                  void *ctx) {
    return (PlaceholderFormat){
        .func = func,
        .ctx = ctx,
        .per_cell = true,
    };
}

// Format bytes into a caller-provided output buffer.
//
// `out`
//     Destination buffer. It may be NULL only when `out_cap` is zero.
// `out_cap`
//     Number of bytes available in `out`.
// `fmt`
//     printf-style format string for all bytes before `final_byte`.
// `final_byte`
//     Final literal byte appended after the formatted prefix. This replaces
//     snprintf's NUL terminator that we don't need.
// `args`
//     printf arguments for `fmt`.
//
// Returns the byte length of the formatted output, a value larger than
// `out_cap` when it does not fit, or a negative value on formatting failure.
static int vformat_counted(char *out, size_t out_cap, const char *fmt,
                           char final_byte, va_list args) {
    if (out == NULL && out_cap != 0)
        return -1;

    int prefix_len = vsnprintf(out, out_cap, fmt, args);

    // IMGNEKO_UNCOVERED_OK: The fixed ASCII formats used here do not fail.
    if (prefix_len < 0)
        return -1; // IMGNEKO_UNCOVERED_OK
    size_t len = (size_t)prefix_len + 1;
    // IMGNEKO_UNCOVERED_OK: The fixed ASCII formats used here are small.
    if (len > (size_t)INT_MAX)
        return -1; // IMGNEKO_UNCOVERED_OK
    if (len > out_cap)
        return (int)len;

    out[prefix_len] = final_byte;
    return (int)len;
}

// Varargs wrapper for `vformat_counted`.
//
// `out`
//     Destination buffer. It may be NULL only when `out_cap` is zero.
// `out_cap`
//     Number of bytes available in `out`.
// `fmt`
//     printf-style format string for all bytes before `final_byte`.
// `final_byte`
//     Final literal byte appended after the formatted prefix.
static int format_counted(char *out, size_t out_cap, const char *fmt,
                          int final_byte, ...) {
    va_list args;

    va_start(args, final_byte);
    int len = vformat_counted(out, out_cap, fmt, (char)final_byte, args);
    va_end(args);

    return len;
}

// Build a relative movement sequence from the last rendered cell to a final
// cursor position.
//
// `placeholder`
//     Placeholder being rendered.
// `final_cursor`
//     Requested final cursor position.
// `linefeed_next_line`
//     Use a literal newline for `next-line`, preserving text-mode behavior.
// `out`
//     Buffer receiving the final cursor sequence.
// `out_cap`
//     Number of bytes available in `out`.
static int write_final_cursor_sequence(const Placeholder *placeholder,
                                       PlaceholderFinalCursor final_cursor,
                                       bool linefeed_next_line, char *out,
                                       size_t out_cap) {
    if (placeholder == NULL)
        return -1;

    uint32_t width = placeholder->rect.end_col - placeholder->rect.start_col;
    uint32_t rows_up =
        placeholder->rect.end_row - placeholder->rect.start_row - 1;

    switch (final_cursor) {
    case PLACEHOLDER_FINAL_CURSOR_BOTTOM_RIGHT:
        return 0;
    case PLACEHOLDER_FINAL_CURSOR_BOTTOM_LEFT:
        return format_counted(out, out_cap, "\033[%u", 'D', (unsigned)width);
    case PLACEHOLDER_FINAL_CURSOR_NEXT_LINE:
        if (linefeed_next_line)
            return memcpy_counted(out, out_cap, "\n", 1);
        return memcpy_counted(out, out_cap, "\n\r", 2);
    case PLACEHOLDER_FINAL_CURSOR_BELOW_LEFT:
        return format_counted(out, out_cap, "\033[%uD\033", 'D',
                              (unsigned)width);
    case PLACEHOLDER_FINAL_CURSOR_TOP_LEFT:
        if (rows_up)
            return format_counted(out, out_cap, "\033[%uD\033[%u", 'A',
                                  (unsigned)width, (unsigned)rows_up);
        return format_counted(out, out_cap, "\033[%u", 'D', (unsigned)width);
    case PLACEHOLDER_FINAL_CURSOR_TOP_RIGHT:
        if (rows_up)
            return format_counted(out, out_cap, "\033[%u", 'A',
                                  (unsigned)rows_up);
        return 0;
    }

    return -1;
}

// Build an absolute movement sequence from the configured terminal origin to a
// final cursor position.
//
// `placeholder`
//     Placeholder being rendered.
// `pos`
//     Zero-based terminal origin used by the absolute positioner.
// `final_cursor`
//     Requested final cursor position.
// `out`
//     Buffer receiving the final cursor sequence.
// `out_cap`
//     Number of bytes available in `out`.
static int write_absolute_final_cursor_sequence(
    const Placeholder *placeholder, const PlaceholderAbsPos *pos,
    PlaceholderFinalCursor final_cursor, char *out, size_t out_cap) {
    if (placeholder == NULL)
        return -1;

    uint32_t width = placeholder->rect.end_col - placeholder->rect.start_col;
    uint32_t height = placeholder->rect.end_row - placeholder->rect.start_row;
    uint32_t target_col = pos->origin_col;
    uint32_t target_row = pos->origin_row;

    switch (final_cursor) {
    case PLACEHOLDER_FINAL_CURSOR_BOTTOM_RIGHT:
        return 0;
    case PLACEHOLDER_FINAL_CURSOR_BOTTOM_LEFT:
        target_row += height - 1;
        break;
    case PLACEHOLDER_FINAL_CURSOR_BELOW_LEFT:
        target_row += height;
        break;
    case PLACEHOLDER_FINAL_CURSOR_NEXT_LINE:
        target_col = 0;
        target_row += height;
        break;
    case PLACEHOLDER_FINAL_CURSOR_TOP_LEFT:
        break;
    case PLACEHOLDER_FINAL_CURSOR_TOP_RIGHT:
        target_col += width;
        break;
    default:
        return -1;
    }

    return format_counted(out, out_cap, "\033[%u;%u", 'H',
                          (unsigned)(target_row + 1),
                          (unsigned)(target_col + 1));
}

// Return whether the final cursor is in the left column of the placeholder.
static bool final_cursor_in_left_column(PlaceholderFinalCursor final_cursor) {
    switch (final_cursor) {
    case PLACEHOLDER_FINAL_CURSOR_BOTTOM_LEFT:
    case PLACEHOLDER_FINAL_CURSOR_BELOW_LEFT:
    case PLACEHOLDER_FINAL_CURSOR_TOP_LEFT:
        return true;
    case PLACEHOLDER_FINAL_CURSOR_BOTTOM_RIGHT:
    case PLACEHOLDER_FINAL_CURSOR_NEXT_LINE:
    case PLACEHOLDER_FINAL_CURSOR_TOP_RIGHT:
        return false;
    }

    return false;
}

// Build a final cursor sequence that starts by restoring the cursor saved at
// the beginning of the last placeholder row.
//
// `placeholder`
//     Placeholder being rendered.
// `final_cursor`
//     Requested final cursor position.
// `out`
//     Buffer receiving the final cursor sequence.
// `out_cap`
//     Number of bytes available in `out`.
static int
write_saved_final_cursor_sequence(const Placeholder *placeholder,
                                  PlaceholderFinalCursor final_cursor,
                                  char *out, size_t out_cap) {
    if (placeholder == NULL)
        return -1;

    switch (final_cursor) {
    case PLACEHOLDER_FINAL_CURSOR_BOTTOM_LEFT:
        return memcpy_counted(out, out_cap, "\033[u", 3);
    case PLACEHOLDER_FINAL_CURSOR_BELOW_LEFT:
        return memcpy_counted(out, out_cap, "\033[u\033D", 5);
    case PLACEHOLDER_FINAL_CURSOR_TOP_LEFT: {
        uint32_t rows_up =
            placeholder->rect.end_row - placeholder->rect.start_row - 1;
        if (rows_up)
            return format_counted(out, out_cap, "\033[u\033[%u", 'A',
                                  (unsigned)rows_up);
        return memcpy_counted(out, out_cap, "\033[u", 3);
    }
    case PLACEHOLDER_FINAL_CURSOR_BOTTOM_RIGHT:
    case PLACEHOLDER_FINAL_CURSOR_NEXT_LINE:
    case PLACEHOLDER_FINAL_CURSOR_TOP_RIGHT:
        return write_final_cursor_sequence(placeholder, final_cursor,
                                           /*linefeed_next_line=*/false, out,
                                           out_cap);
    }

    return -1;
}

PlaceholderFormat placeholder_format_bg_256(uint8_t index, char *out,
                                            size_t out_cap) {
    require(out != NULL, "placeholder 256-color background buffer is missing");
    int len = snprintf(out, out_cap, "\033[48;5;%um", (unsigned)index);
    // IMGNEKO_UNCOVERED_OK[2 lines]: The fixed ASCII format does not fail, and
    // too-small helper buffers abort the process.
    require(len >= 0 && (size_t)len < out_cap,
            "placeholder 256-color background buffer is too small");
    return placeholder_format_static(out);
}

PlaceholderFormat placeholder_format_bg_rgb(uint8_t r, uint8_t g, uint8_t b,
                                            char *out, size_t out_cap) {
    require(out != NULL, "placeholder RGB background buffer is missing");
    int len = snprintf(out, out_cap, "\033[48;2;%u;%u;%um", (unsigned)r,
                       (unsigned)g, (unsigned)b);
    // IMGNEKO_UNCOVERED_OK[2 lines]: The fixed ASCII format does not fail, and
    // too-small helper buffers abort the process.
    require(len >= 0 && (size_t)len < out_cap,
            "placeholder RGB background buffer is too small");
    return placeholder_format_static(out);
}

// Select and emit one branch from a pair of alternating format descriptors.
static int alternating_format_emit(PlaceholderAlternatingFormat *alternating,
                                   bool use_first,
                                   const Placeholder *placeholder, uint32_t col,
                                   uint32_t row, char *out, size_t out_cap) {
    if (alternating == NULL)
        return 0;

    PlaceholderFormat *format =
        use_first ? &alternating->first : &alternating->second;
    if (format->func == NULL)
        return 0;

    return format->func(format->ctx, placeholder, col, row, out, out_cap);
}

int placeholder_format_checkerboard_func(void *ctx,
                                         const Placeholder *placeholder,
                                         uint32_t col, uint32_t row, char *out,
                                         size_t out_cap) {
    return alternating_format_emit(ctx, ((col + row) & 1u) == 0, placeholder,
                                   col, row, out, out_cap);
}

PlaceholderFormat
placeholder_format_checkerboard(PlaceholderAlternatingFormat *format) {
    return placeholder_format_dynamic_cell(placeholder_format_checkerboard_func,
                                           format);
}

int placeholder_format_horizontal_stripes_func(void *ctx,
                                               const Placeholder *placeholder,
                                               uint32_t col, uint32_t row,
                                               char *out, size_t out_cap) {
    return alternating_format_emit(ctx, (row & 1u) == 0, placeholder, col, row,
                                   out, out_cap);
}

PlaceholderFormat
placeholder_format_horizontal_stripes(PlaceholderAlternatingFormat *format) {
    if (format != NULL && (format->first.per_cell || format->second.per_cell))
        return placeholder_format_dynamic_cell(
            placeholder_format_horizontal_stripes_func, format);

    return placeholder_format_dynamic_row(
        placeholder_format_horizontal_stripes_func, format);
}

int placeholder_format_vertical_stripes_func(void *ctx,
                                             const Placeholder *placeholder,
                                             uint32_t col, uint32_t row,
                                             char *out, size_t out_cap) {
    return alternating_format_emit(ctx, (col & 1u) == 0, placeholder, col, row,
                                   out, out_cap);
}

PlaceholderFormat
placeholder_format_vertical_stripes(PlaceholderAlternatingFormat *format) {
    return placeholder_format_dynamic_cell(
        placeholder_format_vertical_stripes_func, format);
}

// Return a zero-initialized config when a standard positioner has no context.
static const PlaceholderPositionConfig *position_config_or_default(void *ctx) {
    static const PlaceholderPositionConfig default_config = {0};
    return ctx ? ctx : &default_config;
}

// Positioner callback that emits newlines between rows.
static int linefeed_positioner_write(void *ctx, const Placeholder *placeholder,
                                     uint32_t row,
                                     PlaceholderPositionFlags flags, char *out,
                                     size_t out_cap) {
    const PlaceholderPositionConfig *config = position_config_or_default(ctx);

    (void)row;

    if (flags & PLACEHOLDER_POSITION_LINE_START) {
        if ((flags & PLACEHOLDER_POSITION_FIRST_LINE) &&
            config->first_line_start_prefix) {
            return memcpy_counted(out, out_cap, config->first_line_start_prefix,
                                  strlen(config->first_line_start_prefix));
        }
        return 0;
    }
    if (flags & PLACEHOLDER_POSITION_LINE_END) {
        if (flags & PLACEHOLDER_POSITION_LAST_LINE)
            return write_final_cursor_sequence(
                placeholder, config->final_cursor,
                /*linefeed_next_line=*/true, out, out_cap);

        return memcpy_counted(out, out_cap, "\n", 1);
    }

    return 0;
}

PlaceholderPositioner
placeholder_position_linefeeds(PlaceholderPositionConfig *config) {
    return (PlaceholderPositioner){
        .func = linefeed_positioner_write,
        .ctx = config,
    };
}

// Positioner callback that emits absolute cursor moves.
//
// `ctx`
//     `PlaceholderAbsPos` describing the zero-based origin.
static int absolute_positioner_write(void *ctx, const Placeholder *placeholder,
                                     uint32_t row,
                                     PlaceholderPositionFlags flags, char *out,
                                     size_t out_cap) {
    PlaceholderAbsPos *pos = ctx;
    assert(pos);

    if (flags & PLACEHOLDER_POSITION_LINE_START)
        return format_counted(out, out_cap, "\033[%u;%u", 'H',
                              (unsigned)(pos->origin_row + row + 1),
                              (unsigned)(pos->origin_col + 1));

    if ((flags & PLACEHOLDER_POSITION_LINE_END) &&
        (flags & PLACEHOLDER_POSITION_LAST_LINE))
        return write_absolute_final_cursor_sequence(
            placeholder, pos, pos->final_cursor, out, out_cap);

    return 0;
}

PlaceholderPositioner placeholder_position_absolute(PlaceholderAbsPos *pos) {
    require(pos != NULL, "absolute positioner context is missing");
    return (PlaceholderPositioner){
        .func = absolute_positioner_write,
        .ctx = pos,
    };
}

// Positioner callback that saves the cursor before each row whose start may be
// needed later and restores it at row end.
static int cursor_positioner_with_save_write(void *ctx,
                                             const Placeholder *placeholder,
                                             uint32_t row,
                                             PlaceholderPositionFlags flags,
                                             char *out, size_t out_cap) {
    const PlaceholderPositionConfig *config = position_config_or_default(ctx);
    (void)row;

    if (flags & PLACEHOLDER_POSITION_LINE_START) {
        const char *prefix = NULL;
        bool save_line = true;

        if (flags & PLACEHOLDER_POSITION_FIRST_LINE)
            prefix = config->first_line_start_prefix;
        if (flags & PLACEHOLDER_POSITION_LAST_LINE)
            save_line = final_cursor_in_left_column(config->final_cursor);
        if (!save_line) {
            if (prefix != NULL)
                return memcpy_counted(out, out_cap, prefix, strlen(prefix));
            return 0;
        }
        if (prefix != NULL)
            return format_counted(out, out_cap, "%s\033[", 's', prefix);

        return memcpy_counted(out, out_cap, "\033[s", 3);
    }
    if (flags & PLACEHOLDER_POSITION_LINE_END) {
        if (flags & PLACEHOLDER_POSITION_LAST_LINE)
            return write_saved_final_cursor_sequence(
                placeholder, config->final_cursor, out, out_cap);

        return memcpy_counted(out, out_cap, "\033[u\033D", 5);
    }

    return 0;
}

PlaceholderPositioner
placeholder_position_at_cursor_with_save(PlaceholderPositionConfig *config) {
    return (PlaceholderPositioner){
        .func = cursor_positioner_with_save_write,
        .ctx = config,
    };
}

// Positioner callback that moves left and down after each non-final row.
static int cursor_positioner_with_moves_write(void *ctx,
                                              const Placeholder *placeholder,
                                              uint32_t row,
                                              PlaceholderPositionFlags flags,
                                              char *out, size_t out_cap) {
    const PlaceholderPositionConfig *config = position_config_or_default(ctx);
    (void)row;

    if (flags & PLACEHOLDER_POSITION_LINE_START) {
        if ((flags & PLACEHOLDER_POSITION_FIRST_LINE) &&
            config->first_line_start_prefix != NULL)
            return memcpy_counted(out, out_cap, config->first_line_start_prefix,
                                  strlen(config->first_line_start_prefix));

        return 0;
    }
    if (flags & PLACEHOLDER_POSITION_LINE_END) {
        if (flags & PLACEHOLDER_POSITION_LAST_LINE)
            return write_final_cursor_sequence(
                placeholder, config->final_cursor,
                /*linefeed_next_line=*/false, out, out_cap);

        if (placeholder == NULL)
            return -1;
        uint32_t width =
            placeholder->rect.end_col - placeholder->rect.start_col;
        return format_counted(out, out_cap, "\033[%uD\033", 'D',
                              (unsigned)width);
    }

    return 0;
}

PlaceholderPositioner
placeholder_position_at_cursor_with_moves(PlaceholderPositionConfig *config) {
    return (PlaceholderPositioner){
        .func = cursor_positioner_with_moves_write,
        .ctx = config,
    };
}

// Convert a diacritic mode level into the number of diacritics per cell.
static bool level_to_count(PlaceholderDiacriticLevel level, bool idbyte_nonzero,
                           uint8_t *out) {
    switch (level) {
    case PLACEHOLDER_DIACRITIC_NONE:
        *out = 0;
        return true;
    case PLACEHOLDER_DIACRITIC_ROW:
        *out = 1;
        return true;
    case PLACEHOLDER_DIACRITIC_ROW_COL:
        *out = 2;
        return true;
    case PLACEHOLDER_DIACRITIC_ROW_COL_IDBYTE:
        *out = 3;
        return true;
    case PLACEHOLDER_DIACRITIC_ROW_COL_IDBYTE_IF_NONZERO:
        *out = idbyte_nonzero ? 3 : 2;
        return true;
    }

    return false;
}

// Validate placeholder arguments and optionally return the resolved plan.
//
// `placeholder`
//     Placeholder geometry and IDs to validate.
// `mode`
//     Metadata encoding mode to validate.
// `out_plan`
//     Optional output plan populated on success.
static PlaceholderError validate_and_make_plan(const Placeholder *placeholder,
                                               const PlaceholderMode *mode,
                                               PlaceholderPlan *out_plan) {
    PlaceholderPlan plan;

    if (placeholder == NULL || mode == NULL)
        return PLACEHOLDER_INVALID_ARGUMENT;
    if (placeholder->image_id == 0)
        return PLACEHOLDER_INVALID_IMAGE_ID;
    if (placeholder->placement_id > 0xFFFFFFu)
        return PLACEHOLDER_INVALID_PLACEMENT_ID;
    if (placeholder->rect.start_col >= placeholder->rect.end_col ||
        placeholder->rect.start_row >= placeholder->rect.end_row)
        return PLACEHOLDER_INVALID_RECTANGLE;

    uint8_t idbyte = (uint8_t)(placeholder->image_id >> 24);
    bool idbyte_nonzero = idbyte != 0;

    if (!level_to_count(mode->first_col_level, idbyte_nonzero,
                        &plan.first_cell_diacritics) ||
        !level_to_count(mode->other_cols_level, idbyte_nonzero,
                        &plan.other_cell_diacritics))
        return PLACEHOLDER_INVALID_MODE;
    if (mode->first_col_level == PLACEHOLDER_DIACRITIC_NONE)
        return PLACEHOLDER_INCOMPLETE_FIRST_COLUMN;

    plan.image_id_high_byte = idbyte;

    if (idbyte_nonzero && plan.first_cell_diacritics < 3)
        return PLACEHOLDER_INCOMPLETE_FIRST_COLUMN;
    if (placeholder->rect.start_col != 0 && plan.first_cell_diacritics < 2)
        return PLACEHOLDER_INCOMPLETE_FIRST_COLUMN;

    bool needs_unrepresentable_cell_symbol =
        placeholder->rect.start_col >= ROWCOLUMN_DIACRITIC_MAX ||
        placeholder->rect.end_row - 1 >= ROWCOLUMN_DIACRITIC_MAX;
    if (needs_unrepresentable_cell_symbol) {
        if (mode->unrepresentable_cell_symbol == NULL)
            return PLACEHOLDER_UNREPRESENTABLE_CELL;
        plan.unrepresentable_cell_symbol_len =
            strlen(mode->unrepresentable_cell_symbol);
    } else {
        plan.unrepresentable_cell_symbol_len = 0;
    }

    if (out_plan != NULL)
        *out_plan = plan;

    return PLACEHOLDER_OK;
}

PlaceholderError placeholder_validate(const Placeholder *placeholder,
                                      const PlaceholderMode *mode) {
    return validate_and_make_plan(placeholder, mode, NULL);
}

// Return remaining chunk bytes. Room for an ANSI reset is reserved when styling
// is already active, or when `force_reserve_reset` asks for room before
// appending bytes that will make styling active.
static size_t chunker_available(const PlaceholderChunker *chunker,
                                bool force_reserve_reset) {
    bool should_reserve_reset = !chunker->grapheme_only &&
                                (force_reserve_reset || chunker->needs_reset);
    size_t pad = should_reserve_reset ? PLACEHOLDER_RESET_LEN : 0;

    if (chunker->cap < pad + chunker->len)
        return 0;

    return chunker->cap - pad - chunker->len;
}

// Append raw bytes to the chunker.
static PlaceholderError chunker_put_bytes(PlaceholderChunker *chunker,
                                          const char *data, size_t len) {
    if (len == 0)
        return PLACEHOLDER_OK;
    if (len > chunker_available(chunker, /*force_reserve_reset=*/false))
        return PLACEHOLDER_CHUNK_TOO_SMALL;

    memcpy(chunker->data + chunker->len, data, len);
    chunker->len += len;
    return PLACEHOLDER_OK;
}

// Append an ANSI reset and clear the pending-reset state.
static PlaceholderError chunker_put_reset(PlaceholderChunker *chunker) {
    if (chunker->cap - chunker->len < PLACEHOLDER_RESET_LEN)
        return PLACEHOLDER_CHUNK_TOO_SMALL;

    memcpy(chunker->data + chunker->len, PLACEHOLDER_RESET,
           PLACEHOLDER_RESET_LEN);
    chunker->len += PLACEHOLDER_RESET_LEN;
    chunker->needs_reset = false;
    return PLACEHOLDER_OK;
}

// Flush the first `len` pending bytes. When `reset_active_style` is true,
// append an ANSI reset to the flushed prefix so styling cannot leak past the
// chunk.
static PlaceholderError chunker_flush_prefix(PlaceholderChunker *chunker,
                                             size_t len,
                                             bool reset_active_style) {
    size_t write_len = len;

    // IMGNEKO_UNCOVERED_OK: Valid placeholders always append at least one cell.
    if (len == 0)
        return PLACEHOLDER_OK; // IMGNEKO_UNCOVERED_OK

    // Insert a reset at the flush boundary if needed.
    if (!chunker->grapheme_only && reset_active_style) {
        // IMGNEKO_UNCOVERED_OK: All appends reserve reset padding.
        if (chunker->len + PLACEHOLDER_RESET_LEN > chunker->cap)
            return PLACEHOLDER_CHUNK_TOO_SMALL; // IMGNEKO_UNCOVERED_OK
        memmove(chunker->data + len + PLACEHOLDER_RESET_LEN,
                chunker->data + len, chunker->len - len);
        memcpy(chunker->data + len, PLACEHOLDER_RESET, PLACEHOLDER_RESET_LEN);
        write_len += PLACEHOLDER_RESET_LEN;
    }

    if (imgneko_writer_write(chunker->writer, chunker->data, write_len))
        return PLACEHOLDER_WRITE_FAILED;

    // Move the unwritten tail to the front of `data` and update chunker state.
    size_t tail_len = chunker->len - len;
    size_t tail_start = write_len;
    bool tail_needs_reset = chunker->needs_reset && tail_len != 0;
    memmove(chunker->data, chunker->data + tail_start, tail_len);
    chunker->len = tail_len;
    chunker->complete_len =
        chunker->complete_len >= len ? chunker->complete_len - len : 0;
    chunker->needs_reset = tail_needs_reset;

    return PLACEHOLDER_OK;
}

// Flush bytes before retrying an indivisible append operation.
static PlaceholderError chunker_flush_for_retry(PlaceholderChunker *chunker) {
    if (chunker->len == 0)
        return PLACEHOLDER_OK;

    size_t len;
    bool reset_active_style;

    if (chunker->complete_len != 0) {
        // Flush only complete rows if possible. Complete rows never need reset.
        len = chunker->complete_len;
        reset_active_style = false;
    } else {
        // If there are no complete rows, flush the whole chunk with a reset if
        // needed.
        len = chunker->len;
        reset_active_style = chunker->needs_reset;
    }

    return chunker_flush_prefix(chunker, len, reset_active_style);
}

// Calls `append(chunker, ctx)` as an indivisible row-start, cell, or row-end
// unit. If it overflows, the chunker rewinds, flushes, and retries.
//
// `chunker`
//     Chunker receiving the append.
// `append`
//     Callback that appends the indivisible unit.
// `ctx`
//     Context passed to `append`.
static PlaceholderError chunker_append_with_retry(
    PlaceholderChunker *chunker,
    PlaceholderError (*append)(PlaceholderChunker *, void *), void *ctx) {
    for (;;) {
        PlaceholderChunker saved = *chunker;
        PlaceholderError result = append(chunker, ctx);
        if (result == PLACEHOLDER_OK)
            return PLACEHOLDER_OK;

        *chunker = saved;
        PlaceholderError error = chunker_flush_for_retry(chunker);
        if (error != PLACEHOLDER_OK)
            return error;
        if (chunker->len == saved.len)
            return result;
    }
}

// Append an image-ID or placement-ID SGR color sequence.
//
// `chunker`
//     Chunker receiving the SGR sequence.
// `code`
//     SGR color selector, such as "38" for foreground or "58" for underline.
// `allow_256color`
//     True when byte-sized values may use the compact 256-color form.
// `value`
//     RGB or byte-sized color value to encode.
static PlaceholderError append_sgr_color(PlaceholderChunker *chunker,
                                         const char *code, bool allow_256color,
                                         uint32_t value) {
    size_t available = chunker_available(chunker, /*force_reserve_reset=*/true);
    int len;

    if (allow_256color && (value & 0xFFFF00u) == 0) {
        len = format_counted(chunker->data + chunker->len, available,
                             "\033[%s;5;%u", 'm', code,
                             (unsigned)(value & 0xFFu));
    } else {
        len = format_counted(
            chunker->data + chunker->len, available, "\033[%s;2;%u;%u;%u", 'm',
            code, (unsigned)((value >> 16) & 0xFFu),
            (unsigned)((value >> 8) & 0xFFu), (unsigned)(value & 0xFFu));
    }

    // IMGNEKO_UNCOVERED_OK[2 lines]: The fixed ASCII format does not fail.
    if (len < 0)
        return PLACEHOLDER_FORMAT_FAILED;
    if ((size_t)len > available)
        return PLACEHOLDER_CHUNK_TOO_SMALL;

    chunker->len += (size_t)len;
    chunker->needs_reset = true;
    return PLACEHOLDER_OK;
}

// Append image and placement ID colors for a representable placeholder cell.
static PlaceholderError append_id_colors(PlaceholderChunker *chunker,
                                         const Placeholder *placeholder,
                                         const PlaceholderMode *mode) {
    uint32_t image_id_low = placeholder->image_id & 0xFFFFFFu;
    uint32_t placement_id = placeholder->placement_id;

    TRY_APPEND(append_sgr_color(chunker, "38", mode->allow_256color_image_id,
                                image_id_low));
    if (mode->skip_zero_placement_id && placement_id == 0)
        return PLACEHOLDER_OK;

    return append_sgr_color(chunker, "58", mode->allow_256color_placement_id,
                            placement_id);
}

// Append the row or column number encoded as a UTF-8 diacritic.
static PlaceholderError append_diacritic(PlaceholderChunker *chunker,
                                         uint32_t num) {
    uint8_t diacritic_len = 0;
    const char *diacritic =
        rowcolumn_num_to_diacritic_utf8(num, &diacritic_len);

    require(diacritic != NULL,
            "placeholder validation missed an unrepresentable diacritic");

    return chunker_put_bytes(chunker, diacritic, diacritic_len);
}

// Return true when a cell must be displayed as the configured replacement
// symbol instead of a placeholder grapheme.
static bool cell_is_unrepresentable(const AppendCellContext *cell) {
    return cell->row >= ROWCOLUMN_DIACRITIC_MAX ||
           cell->placeholder->rect.start_col >= ROWCOLUMN_DIACRITIC_MAX;
}

// Append the visible bytes for a single placeholder cell. Cells that cannot
// carry a usable row anchor use the configured replacement symbol. Later
// columns outside the diacritic range keep the row diacritic, when requested,
// but drop the unrepresentable column and ID-byte diacritics.
static PlaceholderError append_cell_symbol(PlaceholderChunker *chunker,
                                           const AppendCellContext *cell,
                                           uint8_t diacritic_count) {
    uint32_t row_num = cell->row + 1;
    uint32_t col_num = cell->col + 1;

    if (cell_is_unrepresentable(cell)) {
        const char *symbol = cell->options->mode.unrepresentable_cell_symbol;
        require(symbol != NULL,
                "placeholder validation missed a missing fallback symbol");

        return chunker_put_bytes(chunker, symbol,
                                 cell->plan->unrepresentable_cell_symbol_len);
    }

    TRY_APPEND(
        chunker_put_bytes(chunker, PLACEHOLDER_UTF8, PLACEHOLDER_UTF8_LEN));

    if (diacritic_count >= 1)
        TRY_APPEND(append_diacritic(chunker, row_num));
    if (col_num > ROWCOLUMN_DIACRITIC_MAX)
        return PLACEHOLDER_OK;
    if (diacritic_count >= 2)
        TRY_APPEND(append_diacritic(chunker, col_num));
    if (diacritic_count >= 3)
        TRY_APPEND(append_diacritic(
            chunker, (uint32_t)cell->plan->image_id_high_byte + 1));

    return PLACEHOLDER_OK;
}

// Append user-provided formatting for a row or cell.
//
// `chunker`
//     Chunker receiving the formatting bytes.
// `placeholder`
//     Placeholder being rendered.
// `format`
//     Formatting callback and context.
// `col`
//     Column passed to the formatting callback.
// `row`
//     Row passed to the formatting callback.
static PlaceholderError append_user_format(PlaceholderChunker *chunker,
                                           const Placeholder *placeholder,
                                           const PlaceholderFormat *format,
                                           uint32_t col, uint32_t row) {
    if (format->func == NULL)
        return PLACEHOLDER_OK;

    size_t available = chunker_available(chunker, /*force_reserve_reset=*/true);
    int format_len = format->func(format->ctx, placeholder, col, row,
                                  chunker->data + chunker->len, available);
    if (format_len < 0)
        return PLACEHOLDER_FORMAT_FAILED;
    if ((size_t)format_len > available)
        return PLACEHOLDER_CHUNK_TOO_SMALL;

    chunker->len += (size_t)format_len;
    if (format_len != 0)
        chunker->needs_reset = true;

    return PLACEHOLDER_OK;
}

// Append row-level styling before a cell. Row styling may be regenerated after
// a mid-row flush while the chunker searches for a fitting boundary.
static PlaceholderError append_row_style(PlaceholderChunker *chunker,
                                         const AppendCellContext *cell,
                                         bool prepend_reset) {
    if (prepend_reset && !cell->options->grapheme_only)
        TRY_APPEND(chunker_put_reset(chunker));

    if (!cell->options->format.per_cell)
        TRY_APPEND(append_user_format(
            chunker, cell->placeholder, &cell->options->format,
            cell->placeholder->rect.start_col, cell->row));

    // Note that replacement symbols are ordinary text, so they must not carry
    // the automatic colors used as placeholder metadata.
    if (!cell->options->grapheme_only && !cell_is_unrepresentable(cell)) {
        TRY_APPEND(
            append_id_colors(chunker, cell->placeholder, &cell->options->mode));
    }

    return PLACEHOLDER_OK;
}

// Append a single placeholder cell with diacritics and per-cell user
// formatting.
static PlaceholderError append_cell(PlaceholderChunker *chunker, void *ctx) {
    AppendCellContext *cell = ctx;
    uint8_t diacritic_count = cell->first_cell_in_row
                                  ? cell->plan->first_cell_diacritics
                                  : cell->plan->other_cell_diacritics;

    if (cell->first_cell_in_row || chunker->len == 0)
        TRY_APPEND(append_row_style(chunker, cell,
                                    /*prepend_reset=*/chunker->len == 0));

    if (cell->options->format.per_cell)
        TRY_APPEND(append_user_format(chunker, cell->placeholder,
                                      &cell->options->format, cell->col,
                                      cell->row));

    return append_cell_symbol(chunker, cell, diacritic_count);
}

// Append positioning bytes for the current row boundary.
//
// `chunker`
//     Chunker receiving the positioning bytes.
// `placeholder`
//     Placeholder being rendered.
// `options`
//     Effective write options containing the positioner callback.
// `row`
//     Zero-based row offset passed to the positioner.
// `flags`
//     Position flags passed to the positioner.
static PlaceholderError append_position(PlaceholderChunker *chunker,
                                        const Placeholder *placeholder,
                                        const PlaceholderOptions *options,
                                        uint32_t row,
                                        PlaceholderPositionFlags flags) {
    size_t available =
        chunker_available(chunker, /*force_reserve_reset=*/false);
    int len = options->positioner.func(options->positioner.ctx, placeholder,
                                       row, flags, chunker->data + chunker->len,
                                       available);
    if (len < 0)
        return PLACEHOLDER_POSITION_FAILED;
    if ((size_t)len > available)
        return PLACEHOLDER_CHUNK_TOO_SMALL;

    chunker->len += (size_t)len;
    return PLACEHOLDER_OK;
}

// Append the reset and positioning for a new row.
static PlaceholderError append_row_start(PlaceholderChunker *chunker,
                                         void *ctx) {
    AppendRowContext *row_start = ctx;

    if (!row_start->options->grapheme_only)
        TRY_APPEND(chunker_put_reset(chunker));

    TRY_APPEND(append_position(
        chunker, row_start->placeholder, row_start->options, row_start->row,
        row_start->flags | PLACEHOLDER_POSITION_LINE_START));

    return PLACEHOLDER_OK;
}

// Append the reset and positioning suffix for a completed row.
static PlaceholderError append_row_end(PlaceholderChunker *chunker, void *ctx) {
    AppendRowContext *row_end = ctx;

    if (!row_end->options->grapheme_only && chunker->needs_reset)
        TRY_APPEND(chunker_put_reset(chunker));

    return append_position(chunker, row_end->placeholder, row_end->options,
                           row_end->row,
                           row_end->flags | PLACEHOLDER_POSITION_LINE_END);
}

PlaceholderError placeholder_write(const Placeholder *placeholder,
                                   const PlaceholderOptions *options,
                                   ImgnekoWriter writer) {
    PlaceholderOptions default_options;
    PlaceholderPlan plan;
    char chunk_storage[PLACEHOLDER_STACK_CHUNK_SIZE];
    char *chunk_data = chunk_storage;
    PlaceholderChunker chunker;
    PlaceholderError error;

    if (placeholder == NULL || writer.write == NULL)
        return PLACEHOLDER_INVALID_ARGUMENT;

    if (options == NULL) {
        default_options = placeholder_options_default();
        options = &default_options;
    } else if (options->positioner.func == NULL) {
        default_options = *options;
        default_options.positioner = placeholder_position_linefeeds(NULL);
        options = &default_options;
    }

    error = validate_and_make_plan(placeholder, &options->mode, &plan);
    if (error != PLACEHOLDER_OK)
        return error;

    size_t chunk_size = options->chunk_size == 0
                            ? PLACEHOLDER_DEFAULT_CHUNK_SIZE
                            : options->chunk_size;
    if (!options->grapheme_only && chunk_size < PLACEHOLDER_RESET_LEN)
        return PLACEHOLDER_CHUNK_TOO_SMALL;

    if (chunk_size > PLACEHOLDER_STACK_CHUNK_SIZE) {
        chunk_data = malloc(chunk_size);
        // IMGNEKO_UNCOVERED_OK: Allocation failure is not reliably testable.
        if (chunk_data == NULL) {
            error = PLACEHOLDER_WRITE_FAILED; // IMGNEKO_UNCOVERED_OK
            goto cleanup;                     // IMGNEKO_UNCOVERED_OK
        }
    }

    chunker = (PlaceholderChunker){
        .data = chunk_data,
        .cap = chunk_size,
        .grapheme_only = options->grapheme_only,
        .writer = writer,
    };

    for (uint32_t row = placeholder->rect.start_row;
         row < placeholder->rect.end_row; ++row) {
        uint32_t row_offset = row - placeholder->rect.start_row;
        bool last_row = row + 1 == placeholder->rect.end_row;
        PlaceholderPositionFlags flags =
            (row == placeholder->rect.start_row
                 ? PLACEHOLDER_POSITION_FIRST_LINE
                 : 0) |
            (last_row ? PLACEHOLDER_POSITION_LAST_LINE : 0);
        AppendRowContext row_ctx = {
            .placeholder = placeholder,
            .options = options,
            .row = row_offset,
            .flags = flags,
        };

        error = chunker_append_with_retry(&chunker, append_row_start, &row_ctx);
        if (error != PLACEHOLDER_OK)
            goto cleanup;

        for (uint32_t col = placeholder->rect.start_col;
             col < placeholder->rect.end_col; ++col) {
            AppendCellContext cell = {
                .placeholder = placeholder,
                .options = options,
                .plan = &plan,
                .col = col,
                .row = row,
                .first_cell_in_row = col == placeholder->rect.start_col,
            };

            error = chunker_append_with_retry(&chunker, append_cell, &cell);
            if (error != PLACEHOLDER_OK)
                goto cleanup;
        }

        error = chunker_append_with_retry(&chunker, append_row_end, &row_ctx);
        if (error != PLACEHOLDER_OK)
            goto cleanup;
        chunker.complete_len = chunker.len;
    }

    error = chunker_flush_prefix(&chunker, chunker.len,
                                 /*reset_active_style=*/false);

cleanup:
    if (chunk_data != chunk_storage)
        free(chunk_data);
    return error;
}

PlaceholderError placeholder_write_fd(const Placeholder *placeholder,
                                      const PlaceholderOptions *options,
                                      int fd) {
    return placeholder_write(placeholder, options, imgneko_writer_fd(&fd));
}

// Writer callback that copies into a fixed buffer while counting all bytes.
static int buffer_writer_func(void *ctx, const char *data, size_t len) {
    BufferWriteContext *buffer = ctx;
    size_t available =
        buffer->len < buffer->out_cap ? buffer->out_cap - buffer->len : 0;
    size_t copy_len = MIN(available, len);

    if (copy_len != 0)
        memcpy(buffer->out + buffer->len, data, copy_len);
    buffer->len += len;
    return 0;
}

PlaceholderError placeholder_write_to_buffer(const Placeholder *placeholder,
                                             const PlaceholderOptions *options,
                                             char *out, size_t out_cap,
                                             size_t *len_out) {
    BufferWriteContext buffer = {.out = out, .out_cap = out_cap};

    if (out == NULL && out_cap != 0)
        return PLACEHOLDER_INVALID_ARGUMENT;

    PlaceholderError error = placeholder_write(
        placeholder, options,
        (ImgnekoWriter){.write = buffer_writer_func, .ctx = &buffer});

    if (len_out != NULL)
        *len_out = buffer.len;
    if (error != PLACEHOLDER_OK)
        return error;
    if (buffer.len > out_cap)
        return PLACEHOLDER_WRITE_FAILED;

    return PLACEHOLDER_OK;
}
