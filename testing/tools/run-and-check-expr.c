// SPDX-License-Identifier: MIT-0

#include "run-and-check-expr.h"

#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "imgneko/rowcolumn_diacritics.h"
#include "util/options.h"

// Parser state for the limited expression language supported by variable-use
// fragments.
typedef struct ExprParser {
    const RunAndCheckExprContext *ctx;
    const char *text;
    size_t cursor;
} ExprParser;

static void expression_errorf(const ExprParser *parser, const char *format,
                              ...) {
    fprintf(stderr, "%s:%d: error: invalid expression [[%s]] in %s: ",
            parser->ctx->path, parser->ctx->line_number, parser->text,
            parser->ctx->directive_name);

    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);

    fputc('\n', stderr);
}

static void expression_expected_error(const ExprParser *parser,
                                      const char *expected) {
    char ch = parser->text[parser->cursor];

    if (ch == '\0') {
        expression_errorf(parser, "expected %s, got end of expression",
                          expected);
    } else {
        expression_errorf(parser, "expected %s, got '%c'", expected, ch);
    }
}

static void expression_unexpected_text_error(const ExprParser *parser) {
    expression_errorf(parser, "unexpected text '%s' after expression",
                      parser->text + parser->cursor);
}

static void expr_parser_skip_spaces(ExprParser *parser) {
    while (str_char_is_ascii_space(parser->text[parser->cursor]))
        parser->cursor++;
}

// Parse a decimal byte span as a uint32 value.
//
// `parser`
//     Expression parser used for diagnostics.
// `what`
//     Human-readable value name used in diagnostics.
// `text`
//     Decimal byte span to parse.
// `len`
//     Byte length of `text`.
// `out`
//     Receives the parsed integer on success.
static bool parse_uint32_decimal_span(const ExprParser *parser,
                                      const char *what, const char *text,
                                      size_t len, uint32_t *out) {
    int64_t parsed = 0;

    if (!opt_parse_int64_span(text, len, &parsed) || parsed < 0 ||
        parsed > UINT32_MAX) {
        expression_errorf(
            parser, "%s must be an unsigned 32-bit decimal integer", what);
        return false;
    }

    *out = (uint32_t)parsed;
    return true;
}

// Parse a decimal or 0x-prefixed hexadecimal byte span as a uint32 value.
//
// `parser`
//     Expression parser used for diagnostics.
// `what`
//     Human-readable value name used in diagnostics.
// `text`
//     Decimal or hexadecimal byte span to parse.
// `len`
//     Byte length of `text`.
// `out`
//     Receives the parsed integer on success.
static bool parse_uint32_hex_or_decimal_span(const ExprParser *parser,
                                             const char *what, const char *text,
                                             size_t len, uint32_t *out) {
    uint64_t parsed = 0;

    if (!opt_parse_uint64_hex_or_decimal_span(text, len, &parsed) ||
        parsed > UINT32_MAX) {
        expression_errorf(parser,
                          "%s must be an unsigned 32-bit decimal or "
                          "hexadecimal integer",
                          what);
        return false;
    }

    *out = (uint32_t)parsed;
    return true;
}

static void append_placeholder_diacritic(String *out, uint32_t num) {
    uint8_t len = 0;
    const char *bytes = rowcolumn_num_to_diacritic_utf8(num, &len);

    assert(bytes != NULL);

    str_append_data(*out, bytes, len);
}

// Parse a zero-based row or column number as a 1-based diacritic number.
//
// `parser`
//     Expression parser used for diagnostics.
// `what`
//     Human-readable value name used for diagnostics.
// `text`
//     Decimal byte span to parse.
// `len`
//     Byte length of `text`.
// `out`
//     Receives the 1-based diacritic number on success.
static bool parse_ph_diacritic_num(const ExprParser *parser, const char *what,
                                   const char *text, size_t len,
                                   uint32_t *out) {
    uint32_t zero_based_num = 0;

    if (!parse_uint32_decimal_span(parser, what, text, len, &zero_based_num))
        return false;

    if (zero_based_num >= ROWCOLUMN_DIACRITIC_MAX) {
        expression_errorf(parser,
                          "%s value %u is outside the supported "
                          "placeholder diacritic range",
                          what, (unsigned)zero_based_num);
        return false;
    }

    *out = zero_based_num + 1;
    return true;
}

// Append the placeholder code point plus the requested metadata diacritics.
//
// `out`
//     Output string receiving placeholder UTF-8 bytes.
// `row_num`
//     1-based row diacritic number, or 0 when omitted.
// `col_num`
//     1-based column diacritic number, or 0 when omitted.
// `image_id_num`
//     1-based image-ID high-byte diacritic number, or 0 when omitted.
static void append_placeholder_cell(String *out, uint32_t row_num,
                                    uint32_t col_num, uint32_t image_id_num) {
    static const char placeholder_bytes[] = "\xf4\x8e\xbb\xae";

    str_append_data(*out, placeholder_bytes, sizeof(placeholder_bytes) - 1);
    if (row_num != 0)
        append_placeholder_diacritic(out, row_num);
    if (col_num != 0)
        append_placeholder_diacritic(out, col_num);
    if (image_id_num != 0)
        append_placeholder_diacritic(out, image_id_num);
}

// Parse the second ph() argument as a zero-based column or column range.
//
// `parser`
//     Expression parser used for diagnostics.
// `arg`
//     Evaluated ph() column argument. A `start:end` string denotes an inclusive
//     zero-based column range.
// `start_out`
//     Receives the first 1-based column diacritic number on success.
// `end_out`
//     Receives the last 1-based column diacritic number on success.
static bool parse_ph_column_arg(const ExprParser *parser, const String *arg,
                                uint32_t *start_out, uint32_t *end_out) {
    const char *colon = strchr(arg->cstr, ':');

    if (colon == NULL) {
        if (!parse_ph_diacritic_num(parser, "ph() column", arg->cstr, arg->len,
                                    start_out))
            return false;

        *end_out = *start_out;
        return true;
    }

    if (strchr(colon + 1, ':') != NULL) {
        expression_errorf(parser,
                          "ph() column range must contain exactly one ':'");
        return false;
    }

    size_t start_len = (size_t)(colon - arg->cstr);
    size_t end_len = arg->len - start_len - 1;
    if (!parse_ph_diacritic_num(parser, "ph() column range start", arg->cstr,
                                start_len, start_out) ||
        !parse_ph_diacritic_num(parser, "ph() column range end", colon + 1,
                                end_len, end_out)) {
        return false;
    }

    if (*start_out > *end_out) {
        expression_errorf(parser, "ph() column range start must be less "
                                  "than or equal to the range end");
        return false;
    }

    return true;
}

// Build the literal byte sequence emitted by the imgneko placeholder command.
// Rows and columns are 0-based placeholder positions. When the column
// argument is a `start:end` string, the range end is inclusive.
static bool evaluate_ph_call(const ExprParser *parser, const StringArray *args,
                             String *out) {
    if (args->size > 3) {
        expression_errorf(parser, "ph() expects 0 to 3 arguments, got %zu",
                          args->size);
        return false;
    }

    uint32_t row_num = 0;
    if (args->size >= 1 &&
        !parse_ph_diacritic_num(parser, "ph() row", args->data[0].cstr,
                                args->data[0].len, &row_num)) {
        return false;
    }

    uint32_t col_start_num = 0;
    uint32_t col_end_num = 0;
    if (args->size >= 2 && !parse_ph_column_arg(parser, &args->data[1],
                                                &col_start_num, &col_end_num)) {
        return false;
    }

    uint64_t parsed_image_id = 0;
    uint32_t image_id = 0;
    uint32_t image_id_num = 0;
    if (args->size >= 3 &&
        !opt_parse_uint64_hex_or_decimal_span(
            args->data[2].cstr, args->data[2].len, &parsed_image_id)) {
        expression_errorf(
            parser,
            "ph() image id must be an unsigned decimal or hexadecimal integer");
        return false;
    }
    if (args->size >= 3 && parsed_image_id > UINT32_MAX) {
        expression_errorf(parser,
                          "ph() image id must be a 32-bit unsigned integer");
        return false;
    }
    image_id = (uint32_t)parsed_image_id;
    if (args->size >= 3)
        image_id_num = ((image_id >> 24) & 0xff) + 1;

    String result = str_empty;
    bool has_col = args->size >= 2;

    if (!has_col) {
        append_placeholder_cell(&result, row_num, /*col_num=*/0, image_id_num);
    } else {
        for (uint32_t col_num = col_start_num; col_num <= col_end_num;
             ++col_num)
            append_placeholder_cell(&result, row_num, col_num, image_id_num);
    }

    str_free(*out);
    *out = result;
    return true;
}

static bool evaluate_rgb_call(const ExprParser *parser, const StringArray *args,
                              String *out) {
    if (args->size != 1) {
        expression_errorf(parser, "rgb() expects 1 argument, got %zu",
                          args->size);
        return false;
    }

    uint32_t num = 0;
    if (!parse_uint32_hex_or_decimal_span(parser, "rgb() argument",
                                          args->data[0].cstr, args->data[0].len,
                                          &num)) {
        return false;
    }

    unsigned red = (unsigned)((num >> 16) & 0xff);
    unsigned green = (unsigned)((num >> 8) & 0xff);
    unsigned blue = (unsigned)(num & 0xff);
    char buffer[sizeof("255;255;255")];

    snprintf(buffer, sizeof(buffer), "%u;%u;%u", red, green, blue);
    str_free(*out);
    *out = str_from_cstr(buffer);
    return true;
}

// Evaluate a built-in expression function call.
//
// `parser`
//     Expression parser used for diagnostics.
// `function_name`
//     Function name parsed before the opening parenthesis.
// `args`
//     Evaluated argument values owned by the caller.
// `out`
//     Receives an owned result value on success. Any previous value is freed
//     before assignment.
static bool evaluate_function_call(const ExprParser *parser,
                                   const char *function_name,
                                   const StringArray *args, String *out) {
    if (strcmp(function_name, "rgb") == 0)
        return evaluate_rgb_call(parser, args, out);
    if (strcmp(function_name, "ph") == 0)
        return evaluate_ph_call(parser, args, out);

    expression_errorf(parser, "unknown function '%s'", function_name);
    return false;
}

static bool expr_parser_parse_expression(ExprParser *parser, String *out);

static bool expr_parser_parse_string_literal(ExprParser *parser, String *out) {
    String result = str_empty;

    assert(parser->text[parser->cursor] == '"');
    parser->cursor++;

    while (parser->text[parser->cursor] != '\0') {
        char ch = parser->text[parser->cursor++];

        if (ch == '"') {
            str_free(*out);
            *out = result;
            return true;
        }

        if (ch != '\\') {
            str_push(result, ch);
            continue;
        }

        char escape = parser->text[parser->cursor++];
        switch (escape) {
        case '"':
        case '\\':
            str_push(result, escape);
            break;
        // IMGNEKO_UNCOVERED_OK_START: run-and-check matches captured output
        // line by line, so these escapes cannot match useful output.
        case 'n':
            str_push(result, '\n');
            break;
        case 'r':
            str_push(result, '\r');
            break;
        // IMGNEKO_UNCOVERED_OK_END
        case 't':
            str_push(result, '\t');
            break;
        case 'x': {
            if (parser->text[parser->cursor] == '\0' ||
                parser->text[parser->cursor + 1] == '\0') {
                expression_errorf(parser,
                                  "string literal has an invalid \\xHH escape");
                str_free(result);
                return false;
            }

            int high = str_ascii_hex_digit_value(parser->text[parser->cursor]);
            int low =
                str_ascii_hex_digit_value(parser->text[parser->cursor + 1]);

            if (high < 0 || low < 0) {
                expression_errorf(parser,
                                  "string literal has an invalid \\xHH escape");
                str_free(result);
                return false;
            }

            unsigned char byte = (unsigned char)((high << 4) | low);
            if (byte == '\0') {
                expression_errorf(parser,
                                  "string literal escape \\x00 is unsupported");
                str_free(result);
                return false;
            }

            str_push(result, (char)byte);
            parser->cursor += 2;
            break;
        }
        default:
            expression_errorf(parser, "unknown string escape '\\%c'", escape);
            str_free(result);
            return false;
        }
    }

    expression_errorf(parser, "unterminated string literal");
    str_free(result);
    return false;
}

static bool expr_parser_parse_number_literal(ExprParser *parser, String *out) {
    size_t start = parser->cursor;

    if (parser->text[parser->cursor] == '0' &&
        (parser->text[parser->cursor + 1] == 'x' ||
         parser->text[parser->cursor + 1] == 'X')) {
        parser->cursor += 2;
        if (str_ascii_hex_digit_value(parser->text[parser->cursor]) < 0) {
            expression_expected_error(parser, "a hexadecimal digit");
            return false;
        }
        while (str_ascii_hex_digit_value(parser->text[parser->cursor]) >= 0)
            parser->cursor++;
    } else {
        while (str_char_is_ascii_digit(parser->text[parser->cursor]))
            parser->cursor++;
    }

    str_free(*out);
    *out = str_from_data(parser->text + start, parser->cursor - start);
    return true;
}

static bool expr_parser_parse_identifier_or_call(ExprParser *parser,
                                                 String *out) {
    size_t start = parser->cursor;

    parser->cursor++;
    while (str_char_is_ascii_alnum(parser->text[parser->cursor]) ||
           parser->text[parser->cursor] == '_') {
        parser->cursor++;
    }

    String name = str_from_data(parser->text + start, parser->cursor - start);
    expr_parser_skip_spaces(parser);

    if (parser->text[parser->cursor] != '(') {
        char next = parser->text[parser->cursor];

        if (next != '\0' && next != ',' && next != ')' && next != ':') {
            expression_unexpected_text_error(parser);
            str_free(name);
            return false;
        }

        const char *value = parser->ctx->lookup_variable(
            parser->ctx->lookup_user_data, name.cstr);

        if (value == NULL) {
            fprintf(stderr, "%s:%d: error: undefined variable [[%s]] in %s\n",
                    parser->ctx->path, parser->ctx->line_number, name.cstr,
                    parser->ctx->directive_name);
            str_free(name);
            return false;
        }

        str_free(*out);
        *out = str_from_cstr(value);
        str_free(name);
        return true;
    }

    parser->cursor++;
    StringArray args = arr_empty;
    expr_parser_skip_spaces(parser);

    if (parser->text[parser->cursor] != ')') {
        while (true) {
            String arg = str_empty;

            if (!expr_parser_parse_expression(parser, &arg)) {
                str_array_free(&args);
                str_free(name);
                return false;
            }

            arr_push(args, arg);
            expr_parser_skip_spaces(parser);

            if (parser->text[parser->cursor] == ',') {
                parser->cursor++;
                expr_parser_skip_spaces(parser);
                continue;
            }
            if (parser->text[parser->cursor] == ')')
                break;

            expression_expected_error(parser,
                                      "',' or ')' in function argument list");
            str_array_free(&args);
            str_free(name);
            return false;
        }
    }

    assert(parser->text[parser->cursor] == ')');
    parser->cursor++;

    bool ok = evaluate_function_call(parser, name.cstr, &args, out);

    str_array_free(&args);
    str_free(name);
    return ok;
}

static bool expr_parser_parse_primary(ExprParser *parser, String *out) {
    expr_parser_skip_spaces(parser);

    char ch = parser->text[parser->cursor];
    if (ch == '"')
        return expr_parser_parse_string_literal(parser, out);
    if (str_char_is_ascii_digit(ch))
        return expr_parser_parse_number_literal(parser, out);
    if (str_char_is_ascii_alpha(ch) || ch == '_')
        return expr_parser_parse_identifier_or_call(parser, out);

    expression_expected_error(parser, "an expression");
    return false;
}

static bool expr_parser_parse_expression(ExprParser *parser, String *out) {
    return expr_parser_parse_primary(parser, out);
}

bool run_and_check_evaluate_expression(const RunAndCheckExprContext *ctx,
                                       const char *text, String *out) {
    ExprParser parser = {
        .ctx = ctx,
        .text = text,
    };

    String value = str_empty;
    if (!expr_parser_parse_expression(&parser, &value))
        return false;

    expr_parser_skip_spaces(&parser);
    if (parser.text[parser.cursor] != '\0') {
        expression_unexpected_text_error(&parser);
        str_free(value);
        return false;
    }

    str_free(*out);
    *out = value;
    return true;
}
