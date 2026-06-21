// SPDX-License-Identifier: MIT-0

// CLI parsing and formatting glue for imgneko placeholder background options.

#ifndef CLI_PLACEHOLDER_BG_H
#define CLI_PLACEHOLDER_BG_H

#include <stdbool.h>
#include <stddef.h>

#include "imgneko/placeholder.h"
#include "util/string.h"

// Owned background format data for placeholder background options.
//
// The root PlaceholderFormat owns its context recursively. Use
// placeholder_bg_copy_option() to copy this value and
// placeholder_bg_clear_option() to destroy it.
typedef struct PlaceholderBg {
    PlaceholderFormat *root;
} PlaceholderBg;

// Parse a placeholder background expression as a `PlaceholderBg` value.
//
// Supported formats:
// - `default`: reset to the terminal default background.
// - INDEX: 256-color palette index, 0 through 255.
// - STRING_LITERAL: a string containing the formatting escape sequence.
// - `#rrggbb`: web-style RGB hex color.
// - `rgb(r, g, b)`: decimal 8-bit RGB channels.
// - `checkerboard(bg, bg)` or `ch(bg, bg)`: two backgrounds alternated by
//   cell parity.
// - `hstripes(bg, bg)` or `hs(bg, bg)`: two backgrounds alternated by row.
// - `vstripes(bg, bg)` or `vs(bg, bg)`: two backgrounds alternated by column.
//
// Pattern arguments can be nested, for example:
// `vstripes(hstripes(#010203, 4), rgb(5, 6, 7))`.
bool placeholder_bg_parse_option(void *value, const char *text, size_t text_len,
                                 String *error_out);

// Parse raw bytes as a background formatting sequence without expression
// parsing or escape interpretation.
bool placeholder_bg_parse_option_raw(void *value, const char *text,
                                     size_t text_len, String *error_out);

// Deep-copy PlaceholderBg.
void placeholder_bg_copy_option(void *dst_value, const void *src_value);

// Destroy PlaceholderBg.
void placeholder_bg_clear_option(void *value);

// Return the formatting descriptor stored in a parsed background.
//
// The returned format borrows `bg`; the caller must keep `bg` alive
// until placeholder writing is complete.
PlaceholderFormat placeholder_bg_to_format(const PlaceholderBg *bg);

#endif
