// SPDX-License-Identifier: MIT-0

// Implementation of CLI-only placeholder background parsing.

#include "cli/placeholder_bg.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cli/placeholder_bg_file.h"
#include "util/array.h"
#include "util/common.h"
#include "util/error.h"
#include "util/expr.h"
#include "util/options.h"

#define PLACEHOLDER_BG_FORMATS                                                 \
    "STRING, file(STRING), default, INDEX, #rrggbb, rgb(r, g, b), "            \
    "checkerboard(bg, bg), ch(bg, bg), hstripes(bg, bg), hs(bg, bg), "         \
    "vstripes(bg, bg), or vs(bg, bg)"

// Color representation accepted by the placeholder background CLI parser.
typedef enum PlaceholderBgColorKind {
    // Default terminal background, emitted as `49`.
    PLACEHOLDER_BG_COLOR_DEFAULT = 0,
    // 256-color palette index, emitted as `48;5;<index>`.
    PLACEHOLDER_BG_COLOR_INDEX,
    // 24-bit RGB color, emitted as `48;2;<r>;<g>;<b>`.
    PLACEHOLDER_BG_COLOR_RGB,
} PlaceholderBgColorKind;

// Parsed concrete background color.
typedef struct PlaceholderBgColor {
    PlaceholderBgColorKind kind;
    uint8_t index;
    uint8_t r;
    uint8_t g;
    uint8_t b;
} PlaceholderBgColor;

typedef PlaceholderFormat (*PlaceholderBgAltFormatConstructor)(
    PlaceholderAlternatingFormat *format);

// Descriptor for two-color background functions sharing the same grammar.
typedef struct PlaceholderBgAltFunction {
    const char *name;
    PlaceholderBgAltFormatConstructor format;
} PlaceholderBgAltFunction;

// Store a formatted parser error in `error_out` and return false.
static bool placeholder_bg_parse_errorf(String *error_out, const char *format,
                                        ...) {
    if (error_out == NULL)
        return false;

    va_list args;
    va_start(args, format);
    String message = str_vprintf(format, args);
    va_end(args);

    str_free(*error_out);
    *error_out = message;
    return false;
}

// Store the generic background grammar diagnostic in `error_out`.
static bool placeholder_bg_expected_error(String *error_out) {
    return placeholder_bg_parse_errorf(error_out, "expected %s",
                                       PLACEHOLDER_BG_FORMATS);
}

// Append a compact cursor marker for a failing source offset.
//
// The marker shows at most ten bytes before and after the failing offset so
// long malformed expressions do not dominate the option diagnostic.
static void placeholder_bg_append_error_position(String *message, StrSpan span,
                                                 size_t offset) {
    const size_t context = 10;

    assert(offset <= span.len);

    size_t start = offset > context ? offset - context : 0;
    size_t end = MIN(span.len, offset + context);
    StrSpan before = str_span_slice(span, (ptrdiff_t)start, (ptrdiff_t)offset);
    StrSpan after = str_span_slice(span, (ptrdiff_t)offset, (ptrdiff_t)end);

    str_append_cstr(*message, " at '");
    if (start != 0)
        str_append_cstr(*message, "...");
    if (before.len != 0) {
        str_span_sanitize_for_diagnostic(before_context, 48, before);
        str_append_cstr(*message, before_context);
    }
    str_append_cstr(*message, "<here>");
    if (after.len != 0) {
        str_span_sanitize_for_diagnostic(after_context, 48, after);
        str_append_cstr(*message, after_context);
    }
    if (end != span.len)
        str_append_cstr(*message, "...");
    str_push(*message, '\'');
}

// Parse a decimal integer token as a byte value.
static bool parse_decimal_u8(StrSpan span, uint8_t *out) {
    int value = 0;

    if (!opt_parse_int_span(span.data, span.len, &value))
        return false;
    // IMGNEKO_UNCOVERED_OK: Expr decimal tokens cannot be negative.
    if (value < 0 || value > UINT8_MAX)
        return false;

    *out = (uint8_t)value;
    return true;
}

// Parse a web-style `#rrggbb` color operand.
static bool parse_bg_hex(StrSpan span, PlaceholderBgColor *out) {
    uint8_t channels[3] = {0};

    // IMGNEKO_UNCOVERED_OK[2 lines]: Parser validates colors.
    if (span.len != 7)
        return false;

    for (size_t channel = 0; channel < ARRAY_SIZE(channels); ++channel) {
        size_t offset = 1 + channel * 2;
        int hi = str_ascii_hex_digit_value(span.data[offset]);
        int lo = str_ascii_hex_digit_value(span.data[offset + 1]);

        // IMGNEKO_UNCOVERED_OK[2 lines]: Parser validates colors.
        if (hi < 0 || lo < 0)
            return false;
        channels[channel] = (uint8_t)(hi * 16 + lo);
    }

    *out = (PlaceholderBgColor){
        .kind = PLACEHOLDER_BG_COLOR_RGB,
        .r = channels[0],
        .g = channels[1],
        .b = channels[2],
    };
    return true;
}

// Store a diagnostic for an expression that parsed successfully but is not a
// valid background operand.
static bool bg_expr_unexpected_error(const Expr *expr, const char *kind,
                                     String *error_out) {
    str_span_sanitize_for_diagnostic(token, 64, expr->token);
    return placeholder_bg_parse_errorf(error_out,
                                       "unexpected %s '%s'; expected %s", kind,
                                       token, PLACEHOLDER_BG_FORMATS);
}

// Parse an `rgb(r, g, b)` call with decimal 8-bit channels.
static bool bg_expr_parse_rgb(const Expr *expr, PlaceholderBgColor *out,
                              String *error_out) {
    uint8_t channels[3] = {0};

    if (!expr_is_call(expr, "rgb"))
        return placeholder_bg_expected_error(error_out);

    if (expr->args.size != 3) {
        return placeholder_bg_parse_errorf(
            error_out, "rgb() expects 3 arguments, got %zu", expr->args.size);
    }

    for (size_t i = 0; i < 3; ++i) {
        const Expr *arg = expr->args.data[i];
        if (arg->kind == EXPR_DECIMAL_INTEGER &&
            parse_decimal_u8(arg->token, &channels[i])) {
            continue;
        }

        return placeholder_bg_parse_errorf(
            error_out,
            "rgb() argument %zu must be a decimal integer from 0 to 255",
            i + 1);
    }

    *out = (PlaceholderBgColor){
        .kind = PLACEHOLDER_BG_COLOR_RGB,
        .r = channels[0],
        .g = channels[1],
        .b = channels[2],
    };
    return true;
}

// Parse any concrete color operand accepted by the CLI from an expression AST.
static bool bg_expr_parse_color(const Expr *expr, PlaceholderBgColor *out,
                                String *error_out) {
    // IMGNEKO_UNCOVERED_OK: EXPR_INVALID is impossible.
    switch (expr->kind) {
    case EXPR_COLOR:
        // IMGNEKO_UNCOVERED_OK
        if (parse_bg_hex(expr->token, out))
            return true;
        // IMGNEKO_UNCOVERED_OK[2 lines]: EXPR_COLOR is always #rrggbb.
        return placeholder_bg_parse_errorf(
            error_out, "background color must use #rrggbb syntax");
    case EXPR_DECIMAL_INTEGER:
        if (parse_decimal_u8(expr->token, &out->index)) {
            out->kind = PLACEHOLDER_BG_COLOR_INDEX;
            return true;
        }
        return placeholder_bg_parse_errorf(
            error_out,
            "background color index must be a decimal integer from 0 to 255");
    case EXPR_CALL:
        return bg_expr_parse_rgb(expr, out, error_out);
    case EXPR_IDENTIFIER:
        if (str_span_equals_cstr(expr->token, "default")) {
            out->kind = PLACEHOLDER_BG_COLOR_DEFAULT;
            return true;
        }
        return bg_expr_unexpected_error(expr, "identifier", error_out);
    case EXPR_HEX_INTEGER:
        return bg_expr_unexpected_error(expr, "hexadecimal integer", error_out);
    // IMGNEKO_UNCOVERED_OK_START: String nodes become formatting strings before
    // the color parser is called.
    case EXPR_STRING:
        return bg_expr_unexpected_error(expr, "string literal", error_out);
        // IMGNEKO_UNCOVERED_OK_END
    // IMGNEKO_UNCOVERED_OK_START: expr_parse() returns only valid nodes on
    // success, and background parsing only evaluates successful parses.
    case EXPR_INVALID:
        return placeholder_bg_expected_error(error_out);
        // IMGNEKO_UNCOVERED_OK_END
    }

    return placeholder_bg_expected_error(error_out); // IMGNEKO_UNCOVERED_OK
}

// Convert a parsed color operand into an owned SGR sequence.
static String bg_color_to_string(const PlaceholderBgColor *color) {
    if (color->kind == PLACEHOLDER_BG_COLOR_DEFAULT)
        return str_from_cstr("\033[49m");

    char data[PLACEHOLDER_FORMAT_BG_RGB_SIZE] = {0};
    if (color->kind == PLACEHOLDER_BG_COLOR_INDEX) {
        placeholder_format_bg_256(color->index, data, sizeof(data));
    } else {
        placeholder_format_bg_rgb(color->r, color->g, color->b, data,
                                  sizeof(data));
    }

    return str_from_cstr(data);
}

// Allocate a recursive background format descriptor initialized to no format.
static PlaceholderFormat *placeholder_bg_format_alloc(void) {
    PlaceholderFormat *format = malloc(sizeof(*format));

    // IMGNEKO_UNCOVERED_OK[2 lines]
    if (format == NULL)
        arr__abort_oom();

    *format = placeholder_format_none();
    return format;
}

// Allocate an alternating-format context.
static PlaceholderAlternatingFormat *placeholder_bg_alternating_alloc(void) {
    PlaceholderAlternatingFormat *alternating = malloc(sizeof(*alternating));

    // IMGNEKO_UNCOVERED_OK[2 lines]
    if (alternating == NULL)
        arr__abort_oom();

    *alternating = (PlaceholderAlternatingFormat){0};
    return alternating;
}

// Allocate an owned String context.
static String *placeholder_bg_string_alloc(String str) {
    String *data = malloc(sizeof(*data));

    // IMGNEKO_UNCOVERED_OK[2 lines]
    if (data == NULL)
        arr__abort_oom();

    *data = str;
    return data;
}

// Emit a background color sequence stored in an owned String context.
static int placeholder_bg_string_format_func(void *ctx,
                                             const Placeholder *placeholder,
                                             uint32_t col, uint32_t row,
                                             char *out, size_t out_cap) {
    String *data = ctx;

    (void)placeholder;
    (void)col;
    (void)row;

    assert(data != NULL);

    // IMGNEKO_UNCOVERED_OK[2 lines]
    if (data->len > (size_t)INT_MAX)
        return -1;
    if (data->len > out_cap)
        return (int)data->len;

    memcpy(out, data->cstr, data->len);
    return (int)data->len;
}

// Create a static string format. The returned descriptor owns `data` and must
// be released with placeholder_bg_format_deinit().
static PlaceholderFormat placeholder_bg_format_new_string(String data) {
    return (PlaceholderFormat){
        .func = placeholder_bg_string_format_func,
        .ctx = placeholder_bg_string_alloc(data),
        .per_cell = false,
    };
}

// Create a color format from a parsed color. The returned descriptor owns its
// context and must be released with placeholder_bg_format_deinit().
static PlaceholderFormat
placeholder_bg_format_new_color(const PlaceholderBgColor *color) {
    return placeholder_bg_format_new_string(bg_color_to_string(color));
}

// Return true when `func` uses a PlaceholderAlternatingFormat context.
static bool placeholder_bg_is_alternating_func(PlaceholderFormatFunc func) {
    return func == placeholder_format_checkerboard_func ||
           func == placeholder_format_horizontal_stripes_func ||
           /*IMGNEKO_UNCOVERED_OK*/ func ==
               placeholder_format_vertical_stripes_func;
}

// Release recursively owned context storage without freeing `format` itself.
static void placeholder_bg_format_deinit(PlaceholderFormat *format) {
    // IMGNEKO_UNCOVERED_OK[2 lines]
    if (format == NULL || format->func == NULL)
        return;

    if (format->func == placeholder_bg_string_format_func) {
        String *data = format->ctx;
        str_free(*data);
        free(data);
    } else if (format->func == placeholder_bg_file_format_func) {
        placeholder_bg_file_destroy(format->ctx);
    } else {
        require(placeholder_bg_is_alternating_func(format->func),
                "background format has an unexpected function");
        PlaceholderAlternatingFormat *alternating = format->ctx;
        placeholder_bg_format_deinit(&alternating->first);
        placeholder_bg_format_deinit(&alternating->second);
        free(alternating);
    }

    *format = placeholder_format_none();
}

// Destroy a heap-allocated recursive background format descriptor.
static void placeholder_bg_format_destroy(PlaceholderFormat *format) {
    if (format == NULL)
        return;

    placeholder_bg_format_deinit(format);
    free(format);
}

// Deep-copy a recursive background format descriptor.
static PlaceholderFormat placeholder_bg_format_copy(PlaceholderFormat src) {
    PlaceholderFormat copy = src;

    // IMGNEKO_UNCOVERED_OK[2 lines]: Parsed backgrounds do not contain none.
    if (src.func == NULL)
        return copy;

    if (src.func == placeholder_bg_string_format_func) {
        String *data = src.ctx;
        copy.ctx = placeholder_bg_string_alloc(str_copy(*data));
        return copy;
    }

    if (src.func == placeholder_bg_file_format_func) {
        copy.ctx = placeholder_bg_file_copy(src.ctx);
        return copy;
    }

    require(placeholder_bg_is_alternating_func(src.func),
            "background format has an unexpected function");

    PlaceholderAlternatingFormat *src_alternating = src.ctx;
    PlaceholderAlternatingFormat *dst_alternating =
        placeholder_bg_alternating_alloc();

    dst_alternating->first = placeholder_bg_format_copy(src_alternating->first);
    dst_alternating->second =
        placeholder_bg_format_copy(src_alternating->second);

    copy.ctx = dst_alternating;
    return copy;
}

static bool bg_expr_parse_node(const Expr *expr, PlaceholderFormat *out,
                               String *error_out);

// Parse a `file("path")` call into a file-backed background format.
static bool bg_expr_parse_file_function(const Expr *expr,
                                        PlaceholderFormat *out,
                                        String *error_out) {
    if (expr->args.size != 1) {
        return placeholder_bg_parse_errorf(
            error_out, "file() expects 1 argument, got %zu", expr->args.size);
    }

    const Expr *path_expr = expr->args.data[0];
    if (path_expr->kind != EXPR_STRING) {
        return placeholder_bg_parse_errorf(
            error_out, "file() argument must be a string literal");
    }

    String path = expr_string_literal_value(path_expr);
    PlaceholderBgFile *file = placeholder_bg_file_load(path.cstr, error_out);
    str_free(path);
    if (file == NULL)
        return false;

    *out = placeholder_bg_file_format(file);
    return true;
}

// Parse a two-operand alternating pattern function.
static bool
bg_expr_parse_alt_pattern_function(const Expr *expr,
                                   const PlaceholderBgAltFunction *fn,
                                   PlaceholderFormat *out, String *error_out) {
    PlaceholderFormat first = placeholder_format_none();
    PlaceholderFormat second = placeholder_format_none();
    PlaceholderAlternatingFormat *alternating = NULL;
    bool ok = false;

    assert(expr_is_call(expr, fn->name));

    if (expr->args.size != 2) {
        return placeholder_bg_parse_errorf(error_out,
                                           "%s() expects 2 arguments, got %zu",
                                           fn->name, expr->args.size);
    }

    if (!bg_expr_parse_node(expr->args.data[0], &first, error_out))
        goto cleanup;

    if (!bg_expr_parse_node(expr->args.data[1], &second, error_out))
        goto cleanup;

    alternating = placeholder_bg_alternating_alloc();
    alternating->first = first;
    alternating->second = second;
    first = placeholder_format_none();
    second = placeholder_format_none();

    *out = fn->format(alternating);
    ok = true;

cleanup:
    placeholder_bg_format_deinit(&first);
    placeholder_bg_format_deinit(&second);
    return ok;
}

// Parse either a raw formatting string, a recursive pattern, or a solid color.
static bool bg_expr_parse_node(const Expr *expr, PlaceholderFormat *out,
                               String *error_out) {
    if (expr->kind == EXPR_STRING) {
        *out =
            placeholder_bg_format_new_string(expr_string_literal_value(expr));
        return true;
    }

    if (expr->kind == EXPR_CALL) {
        if (expr_is_call(expr, "file"))
            return bg_expr_parse_file_function(expr, out, error_out);

        static const PlaceholderBgAltFunction alt_functions[] = {
            {.name = "checkerboard", .format = placeholder_format_checkerboard},
            {.name = "ch", .format = placeholder_format_checkerboard},
            {.name = "hstripes",
             .format = placeholder_format_horizontal_stripes},
            {.name = "hs", .format = placeholder_format_horizontal_stripes},
            {.name = "vstripes", .format = placeholder_format_vertical_stripes},
            {.name = "vs", .format = placeholder_format_vertical_stripes},
        };

        for (size_t i = 0; i < ARRAY_SIZE(alt_functions); ++i) {
            const PlaceholderBgAltFunction *fn = &alt_functions[i];
            if (expr_is_call(expr, fn->name))
                return bg_expr_parse_alt_pattern_function(expr, fn, out,
                                                          error_out);
        }
    }

    PlaceholderBgColor color = {0};
    if (!bg_expr_parse_color(expr, &color, error_out))
        return false;

    *out = placeholder_bg_format_new_color(&color);
    return true;
}

// Parse a background expression into an owned recursive format tree.
static bool parse_bg(StrSpan span, PlaceholderBg *out, String *error_out) {
    Expr expr = {0};
    ExprParseError error = {0};
    PlaceholderFormat root = placeholder_format_none();
    bool ok = false;

    if (!expr_parse(span, &expr, &error)) {
        if (error_out != NULL) {
            str_free(*error_out);
            *error_out = error.message;
            error.message = str_empty;
            placeholder_bg_append_error_position(error_out, span, error.offset);
        }
        goto cleanup;
    }

    if (bg_expr_parse_node(&expr, &root, error_out)) {
        placeholder_bg_clear_option(out);
        out->root = placeholder_bg_format_alloc();
        *out->root = root;
        root = placeholder_format_none();
        ok = true;
        goto cleanup;
    }

cleanup:
    placeholder_bg_format_deinit(&root);
    expr_parse_error_clear(&error);
    expr_deinit(&expr);
    return ok;
}

bool placeholder_bg_parse_option(void *value, const char *text, size_t text_len,
                                 String *error_out) {
    PlaceholderBg *bg = value;

    if (text_len == 0)
        return placeholder_bg_expected_error(error_out);
    if (!parse_bg(str_span(text, text_len), bg, error_out))
        return false;

    return true;
}

bool placeholder_bg_parse_option_raw(void *value, const char *text,
                                     size_t text_len, String *error_out) {
    PlaceholderBg *bg = value;

    if (text == NULL)
        return opt_parse_error(error_out, "value is required");

    placeholder_bg_clear_option(bg);
    bg->root = placeholder_bg_format_alloc();
    *bg->root = placeholder_bg_format_new_string(str_from_data(text, text_len));
    return true;
}

bool placeholder_bg_parse_option_file(void *value, const char *text,
                                      size_t text_len, String *error_out) {
    PlaceholderBg *bg = value;

    if (text == NULL)
        return opt_parse_error(error_out, "value is required");

    String path = str_from_data(text, text_len);
    PlaceholderBgFile *file = placeholder_bg_file_load(path.cstr, error_out);
    str_free(path);
    if (file == NULL)
        return false;

    placeholder_bg_clear_option(bg);
    bg->root = placeholder_bg_format_alloc();
    *bg->root = placeholder_bg_file_format(file);
    return true;
}

void placeholder_bg_copy_option(void *dst_value, const void *src_value) {
    const PlaceholderBg *src = src_value;
    PlaceholderBg *dst = dst_value;

    placeholder_bg_clear_option(dst);
    if (src->root != NULL) {
        dst->root = placeholder_bg_format_alloc();
        *dst->root = placeholder_bg_format_copy(*src->root);
    }
}

void placeholder_bg_clear_option(void *value) {
    PlaceholderBg *bg = value;

    placeholder_bg_format_destroy(bg->root);
    bg->root = NULL;
}

PlaceholderFormat placeholder_bg_to_format(const PlaceholderBg *bg) {
    if (bg->root == NULL)
        return placeholder_format_none();

    return *bg->root;
}
