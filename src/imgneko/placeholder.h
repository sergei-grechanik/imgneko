// SPDX-License-Identifier: MIT-0

// Placeholder printing.
//
// This API emits Unicode placeholders used for image placement in the Kitty
// graphics protocol. It supports optional user formatting, positioning, and
// chunking suitable for PIPE_BUF-sized writes.
//
// Example:
//
//     Placeholder placeholder = {
//         .image_id = 7,
//         .placement_id = 0,
//         .rect = {.start_col = 0, .start_row = 0, .end_col = 2, .end_row = 2},
//     };
//     PlaceholderOptions options = placeholder_options_default();
//     placeholder_write_fd(&placeholder, &options, STDOUT_FILENO);
//
// The output is shaped like this, escaped for readability:
//     "\x1b[0m\x1b[38;5;7m<place><row1><col1><place><row1><col2>\x1b[0m\n"
//     "\x1b[0m\x1b[38;5;7m<place><row2><col1><place><row2><col2>\x1b[0m\n"
//
// `<place>` is `\xf4\x8e\xbb\xae`, the UTF-8 encoding of the placeholder code
// point U+10EEEE.
// `<rowN>` and `<colN>` stand for UTF-8 row/column diacritic byte sequences.

#ifndef IMGNEKO_PLACEHOLDER_H
#define IMGNEKO_PLACEHOLDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "imgneko/writer.h"

#define PLACEHOLDER_CODEPOINT 0x10EEEEu
#define PLACEHOLDER_UTF8 "\xF4\x8E\xBB\xAE"
#define PLACEHOLDER_UTF8_LEN 4

// Recommended array size for a null-terminated 256-color background sequence.
#define PLACEHOLDER_FORMAT_BG_256_SIZE 12
// Recommended array size for a null-terminated RGB background sequence.
#define PLACEHOLDER_FORMAT_BG_RGB_SIZE 20

typedef enum PlaceholderError {
    PLACEHOLDER_OK = 0,
    PLACEHOLDER_INVALID_ARGUMENT,
    PLACEHOLDER_INVALID_IMAGE_ID,
    PLACEHOLDER_INVALID_PLACEMENT_ID,
    PLACEHOLDER_INVALID_RECTANGLE,
    PLACEHOLDER_INVALID_MODE,
    PLACEHOLDER_INCOMPLETE_FIRST_COLUMN,
    PLACEHOLDER_UNREPRESENTABLE_CELL,
    PLACEHOLDER_UNREPRESENTABLE_COLUMN,
    PLACEHOLDER_CHUNK_TOO_SMALL,
    PLACEHOLDER_FORMAT_FAILED,
    PLACEHOLDER_POSITION_FAILED,
    PLACEHOLDER_WRITE_FAILED,
} PlaceholderError;

// Number of metadata diacritics emitted after a placeholder code point.
typedef enum PlaceholderDiacriticLevel {
    // Emit no diacritics. This is valid only for non-first columns.
    PLACEHOLDER_DIACRITIC_NONE = 0,
    // Emit the row number. For the first rendered column, this is valid only
    // when the high image-ID byte is zero and the rectangle starts at column 0.
    PLACEHOLDER_DIACRITIC_ROW = 1,
    // Emit the row and column numbers. For the first rendered column, this is
    // valid only when the high image-ID byte is zero.
    PLACEHOLDER_DIACRITIC_ROW_COL = 2,
    // Emit the row number, column number, and high byte of the image ID.
    PLACEHOLDER_DIACRITIC_ROW_COL_IDBYTE = 3,
    // Emit the row and column numbers, plus the high image-ID byte only when it
    // is nonzero.
    PLACEHOLDER_DIACRITIC_ROW_COL_IDBYTE_IF_NONZERO = 4,
} PlaceholderDiacriticLevel;

// Controls how metadata is encoded in each placeholder grapheme.
//
// Image and placement IDs are normally encoded as SGR colors. Row, column, and
// the high byte of the image ID can also be encoded as diacritics after the
// placeholder code point. For example, the default mode emits row+column
// diacritics for ordinary IDs, and upgrades to row+column+ID-byte diacritics
// when the image ID uses its high byte. Minimal mode keeps the default
// first-column diacritics and emits no diacritics for other columns.
typedef struct PlaceholderMode {
    // Use `38;5` (256 color fg) image-ID color encoding when the low 24 bits
    // fit in one byte.
    bool allow_256color_image_id;
    // Use `58;5` (256 color underline) placement-ID color encoding when the
    // placement ID fits in one byte.
    bool allow_256color_placement_id;
    // Omit placement-ID color encoding when `placement_id` is zero.
    bool skip_zero_placement_id;
    // Diacritics emitted for the first column of each row.
    PlaceholderDiacriticLevel first_col_level;
    // Diacritics emitted for columns after the first column of each row.
    PlaceholderDiacriticLevel other_cols_level;
    // Replacement bytes emitted for cells that cannot be represented safely.
    // This covers unrepresentable rows and rows whose first rendered column
    // cannot carry the required row metadata. NULL makes writes fail with
    // PLACEHOLDER_UNREPRESENTABLE_CELL. The default is "□".
    const char *unrepresentable_cell_symbol;
} PlaceholderMode;

// Zero-based terminal-cell rectangle. `end_col` and `end_row` are exclusive.
typedef struct PlaceholderRect {
    uint32_t start_col;
    uint32_t start_row;
    uint32_t end_col;
    uint32_t end_row;
} PlaceholderRect;

// Image placeholder metadata and the rectangle it occupies.
typedef struct Placeholder {
    uint32_t image_id;
    uint32_t placement_id;
    PlaceholderRect rect;
} Placeholder;

// A callback to write zero-width formatting escape sequences for a single
// placeholder cell. It must not append a null terminator to `out`.
//
// Parameters:
// `ctx`
//     Opaque callback context.
// `placeholder`
//     Original placeholder metadata and rectangle currently being written.
// `col`
//     Zero-based placeholder column being emitted, in the same coordinate
//     system as `placeholder->rect` (relative to the image left).
// `row`
//     Zero-based placeholder row being emitted, in the same coordinate system
//     as `placeholder->rect` (relative to the image top).
// `out`
//     Buffer receiving formatting bytes.
// `out_cap`
//     Number of bytes available in `out`.
//
// Returns the number of bytes written, a value larger than `out_cap` when the
// output does not fit, or a negative value on failure.
typedef int (*PlaceholderFormatFunc)(void *ctx, const Placeholder *placeholder,
                                     uint32_t col, uint32_t row, char *out,
                                     size_t out_cap);

// Optional user formatting layered on top of image and placement ID colors.
//
// The `per_cell` field controls whether `func` is row formatting or cell
// formatting. Row formatting is emitted before the first cell in every row and
// may be emitted again when a row is split across chunks, before image and
// placement ID colors. Cell formatting is emitted before every placeholder
// cell; image and placement ID colors are not re-emitted after it.
typedef struct PlaceholderFormat {
    PlaceholderFormatFunc func;
    void *ctx;
    bool per_cell;
} PlaceholderFormat;

// Describes why a positioner callback is being called for a placeholder line.
typedef enum PlaceholderPositionFlags {
    // The callback is being called before emitting this placeholder line.
    PLACEHOLDER_POSITION_LINE_START = 1u << 0,
    // The callback is being called after emitting this placeholder line.
    PLACEHOLDER_POSITION_LINE_END = 1u << 1,
    // This is the first line of the current placeholder output.
    PLACEHOLDER_POSITION_FIRST_LINE = 1u << 2,
    // This is the last line of the current placeholder output.
    PLACEHOLDER_POSITION_LAST_LINE = 1u << 3,
} PlaceholderPositionFlags;

// Emits positioning bytes before or after a placeholder line.
//
// Parameters:
// `ctx`
//     Opaque callback context.
// `placeholder`
//     Placeholder metadata and rectangle currently being written.
// `row`
//     Row relative to the top of the placeholder rect being emitted (not
//     necessarily relative to the whole image).
// `flags`
//     Indicates whether this is a line-start or line-end call, and whether the
//     line is first or last.
// `out`
//     Buffer receiving positioning bytes.
// `out_cap`
//     Number of bytes available in `out`.
//
// Returns the number of bytes written, a value larger than `out_cap` when the
// output does not fit, or a negative value on failure.
typedef struct PlaceholderPositioner {
    int (*func)(void *ctx, const Placeholder *placeholder, uint32_t row,
                PlaceholderPositionFlags flags, char *out, size_t out_cap);
    void *ctx;
} PlaceholderPositioner;

// Final cursor position after placeholder output is complete.
typedef enum PlaceholderFinalCursor {
    // Leave the cursor after the placeholder's bottom row.
    PLACEHOLDER_FINAL_CURSOR_BOTTOM_RIGHT = 0,
    // Move to the placeholder's bottom-left corner.
    PLACEHOLDER_FINAL_CURSOR_BOTTOM_LEFT = 1,
    // Move right below the placeholder's bottom-left corner.
    PLACEHOLDER_FINAL_CURSOR_BELOW_LEFT = 2,
    // Move to the start of the next line.
    PLACEHOLDER_FINAL_CURSOR_NEXT_LINE = 3,
    // Move to the placeholder's top-left corner.
    PLACEHOLDER_FINAL_CURSOR_TOP_LEFT = 4,
    // Move after the placeholder's top row.
    PLACEHOLDER_FINAL_CURSOR_TOP_RIGHT = 5,
} PlaceholderFinalCursor;

// Shared configuration for standard non-absolute positioners.
typedef struct PlaceholderPositionConfig {
    // Optional NUL-terminated bytes emitted at the start of the first line.
    const char *first_line_start_prefix;
    // Final cursor position. Zero-initialization selects bottom-right.
    PlaceholderFinalCursor final_cursor;
} PlaceholderPositionConfig;

// Context for absolute positioning. Coordinates are zero-based terminal cells.
typedef struct PlaceholderAbsPos {
    uint32_t origin_col;
    uint32_t origin_row;
    // Final cursor position. Zero-initialization selects bottom-right.
    PlaceholderFinalCursor final_cursor;
} PlaceholderAbsPos;

// Top-level placeholder print options.
typedef struct PlaceholderOptions {
    // Metadata encoding mode.
    PlaceholderMode mode;
    // Optional zero-width formatting emitted for each placeholder cell or line.
    PlaceholderFormat format;
    // Optional positioning emitted before and after placeholder lines.
    PlaceholderPositioner positioner;
    // Emit only placeholder graphemes, without the fg and underline color
    // sequences encoding image and placement IDs. Note that user-specified
    // formatting and positioning is still emitted.
    bool grapheme_only;
    // Byte limit for each low-level write. Zero uses a PIPE_BUF-based default.
    // The writer prefers chunks ending at line boundaries. When a line is too
    // large, it falls back to cell boundaries.
    size_t chunk_size;
} PlaceholderOptions;

// Return static text for a placeholder error code.
const char *placeholder_error_string(PlaceholderError error);

// Return the default mode. It uses compact 256-color image IDs when possible,
// true-color placement IDs, skips zero placement IDs, and emits the high
// image-ID byte only when it is nonzero.
static inline PlaceholderMode placeholder_mode_default(void) {
    return (PlaceholderMode){
        .allow_256color_image_id = true,
        .allow_256color_placement_id = false,
        .skip_zero_placement_id = true,
        .first_col_level = PLACEHOLDER_DIACRITIC_ROW_COL_IDBYTE_IF_NONZERO,
        .other_cols_level = PLACEHOLDER_DIACRITIC_ROW_COL_IDBYTE_IF_NONZERO,
        .unrepresentable_cell_symbol = "□",
    };
}

// Return a mode that always emits row, column, and high image-ID byte
// diacritics, even when the latter is zero.
static inline PlaceholderMode placeholder_mode_complete(void) {
    PlaceholderMode mode = placeholder_mode_default();
    mode.first_col_level = PLACEHOLDER_DIACRITIC_ROW_COL_IDBYTE;
    mode.other_cols_level = PLACEHOLDER_DIACRITIC_ROW_COL_IDBYTE;
    return mode;
}

// Return a compact mode that keeps the default first-column diacritics and
// emits no diacritics for later cells.
static inline PlaceholderMode placeholder_mode_minimal(void) {
    PlaceholderMode mode = placeholder_mode_default();
    mode.other_cols_level = PLACEHOLDER_DIACRITIC_NONE;
    return mode;
}

// Return a no-formatting descriptor.
static inline PlaceholderFormat placeholder_format_none(void) {
    return (PlaceholderFormat){0};
}

// Return a static row-formatting descriptor.
//
// Static formatting borrows `data`; the caller must keep it alive while
// placeholders are written.
//
// Parameters:
// `data`
//     Static bytes to emit, or NULL to emit nothing.
PlaceholderFormat placeholder_format_static(const char *data);

// Return dynamic formatting descriptors.
//
// Parameters:
// `func`
//     Formatting callback to run, or NULL to emit nothing.
// `ctx`
//     Opaque callback context.
PlaceholderFormat placeholder_format_dynamic_row(PlaceholderFormatFunc func,
                                                 void *ctx);
PlaceholderFormat placeholder_format_dynamic_cell(PlaceholderFormatFunc func,
                                                  void *ctx);

// Build an SGR 256-color background sequence into `out` and return a static
// row-formatting descriptor that borrows `out`.
//
// Parameters:
// `index`
//     256-color palette index.
// `out`
//     Buffer receiving the null-terminated SGR sequence. Use
//     `PLACEHOLDER_FORMAT_BG_256_SIZE` bytes.
// `out_cap`
//     Number of bytes available in `out`.
//
// If `out` is too small, terminate the process with an error message.
PlaceholderFormat placeholder_format_bg_256(uint8_t index, char *out,
                                            size_t out_cap);

// Build an SGR RGB background sequence into `out` and return a static
// row-formatting descriptor that borrows `out`.
//
// Parameters:
// `r`, `g`, `b`
//     Red, green, and blue channels.
// `out`
//     Buffer receiving the null-terminated SGR sequence. Use
//     `PLACEHOLDER_FORMAT_BG_RGB_SIZE` bytes.
// `out_cap`
//     Number of bytes available in `out`.
//
// If `out` is too small, terminate the process with an error message.
PlaceholderFormat placeholder_format_bg_rgb(uint8_t r, uint8_t g, uint8_t b,
                                            char *out, size_t out_cap);

// Two formatting descriptors used by alternating formatting helpers.
typedef struct PlaceholderAlternatingFormat {
    PlaceholderFormat first;
    PlaceholderFormat second;
} PlaceholderAlternatingFormat;

// Return a cell-formatting descriptor that alternates two formatting
// descriptors in a checkerboard pattern. `format->first` is used when
// `col + row` is even, and `format->second` is used when it is odd. The
// returned descriptor borrows `format` and both callback contexts from it.
PlaceholderFormat
placeholder_format_checkerboard(PlaceholderAlternatingFormat *format);

// Return a descriptor that alternates two formatting descriptors by row.
// `format->first` is used for even rows, and `format->second` is used for odd
// rows. The returned descriptor is row-formatting unless either nested
// descriptor needs per-cell evaluation. It borrows `format` and both callback
// contexts from it.
PlaceholderFormat
placeholder_format_horizontal_stripes(PlaceholderAlternatingFormat *format);

// Return a cell-formatting descriptor that alternates two formatting
// descriptors by column. `format->first` is used for even columns, and
// `format->second` is used for odd columns. The returned descriptor borrows
// `format` and both callback contexts from it.
PlaceholderFormat
placeholder_format_vertical_stripes(PlaceholderAlternatingFormat *format);

// Placeholder format funcs. These are not intended for direct use.
int placeholder_format_checkerboard_func(void *ctx,
                                         const Placeholder *placeholder,
                                         uint32_t col, uint32_t row, char *out,
                                         size_t out_cap);
int placeholder_format_horizontal_stripes_func(void *ctx,
                                               const Placeholder *placeholder,
                                               uint32_t col, uint32_t row,
                                               char *out, size_t out_cap);
int placeholder_format_vertical_stripes_func(void *ctx,
                                             const Placeholder *placeholder,
                                             uint32_t col, uint32_t row,
                                             char *out, size_t out_cap);

// Return a positioner that writes newlines between placeholder rows and uses
// the configured final cursor position after the last row.
// `config` is optional. If provided, the caller must keep it alive while
// placeholders are written.
PlaceholderPositioner
placeholder_position_linefeeds(PlaceholderPositionConfig *config);

// Return a positioner that moves to `(pos->origin_col, pos->origin_row + row)`
// before each placeholder row. Coordinates in `pos` are zero-based terminal
// cells. The caller must keep `pos` alive while placeholders are written.
PlaceholderPositioner placeholder_position_absolute(PlaceholderAbsPos *pos);

// Return a positioner for writing an image below the current cursor. It uses
// save-cursor and restore-cursor sequences around each non-final row, followed
// by an index sequence to move to the next terminal line. `config` is optional.
// If provided, the caller must keep it alive while placeholders are written.
PlaceholderPositioner
placeholder_position_at_cursor_with_save(PlaceholderPositionConfig *config);

// Return a positioner for writing an image below the current cursor without
// save/restore sequences. After each non-final row, it moves left by the
// placeholder width and then indexes to the next terminal line. `config` is
// optional. If provided, the caller must keep it alive while placeholders are
// written.
PlaceholderPositioner
placeholder_position_at_cursor_with_moves(PlaceholderPositionConfig *config);

// Return default print options.
static inline PlaceholderOptions placeholder_options_default(void) {
    return (PlaceholderOptions){
        .mode = placeholder_mode_default(),
        .format = {0},
        .positioner = placeholder_position_linefeeds(NULL),
        .grapheme_only = false,
        .chunk_size = 0,
    };
}

// Validate placeholder geometry, IDs, and mode.
PlaceholderError placeholder_validate(const Placeholder *placeholder,
                                      const PlaceholderMode *mode);

// Stream placeholder bytes through `writer`.
PlaceholderError placeholder_write(const Placeholder *placeholder,
                                   const PlaceholderOptions *options,
                                   ImgnekoWriter writer);

// Stream placeholder bytes to a file descriptor.
PlaceholderError placeholder_write_fd(const Placeholder *placeholder,
                                      const PlaceholderOptions *options,
                                      int fd);

// Write placeholder bytes into caller-provided storage.
//
// Parameters:
// `placeholder`
//     Placeholder metadata and rectangle to write.
// `options`
//     Print options, or NULL for defaults.
// `out`
//     Buffer receiving rendered bytes. May be NULL only when `out_cap` is zero.
// `out_cap`
//     Number of bytes available in `out`.
// `len_out`
//     Optional. Receives the byte count that would have been written, even when
//     `out_cap` is too small and PLACEHOLDER_WRITE_FAILED is returned.
//
// If output capacity is too small, returns PLACEHOLDER_WRITE_FAILED and sets
// `*len_out` to the required capacity.
PlaceholderError placeholder_write_to_buffer(const Placeholder *placeholder,
                                             const PlaceholderOptions *options,
                                             char *out, size_t out_cap,
                                             size_t *len_out);

#endif
