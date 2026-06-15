// SPDX-License-Identifier: MIT-0

// Implementation of the small expression parser.

#include "util/expr.h"

#include <assert.h>
#include <stdlib.h>

// Cursor and diagnostic sink used while parsing a source span.
typedef struct ExprParser {
    StrSpan text;
    size_t cursor;
    ExprParseError *error;
} ExprParser;

static bool expr_parser_parse_expression(ExprParser *parser, Expr *out);

void expr_parse_error_clear(ExprParseError *error) {
    if (error == NULL)
        return;

    str_free(error->message);
    error->offset = 0;
}

// Return true when the parser cursor has consumed the source span.
static bool expr_parser_at_end(const ExprParser *parser) {
    return parser->cursor == parser->text.len;
}

// Return the current byte or NUL when the parser is at end of input.
static char expr_parser_peek(const ExprParser *parser) {
    if (expr_parser_at_end(parser))
        return '\0';
    return parser->text.data[parser->cursor];
}

// Advance past ASCII whitespace at the parser cursor.
static void expr_parser_skip_spaces(ExprParser *parser) {
    while (!expr_parser_at_end(parser) &&
           str_char_is_ascii_space(expr_parser_peek(parser))) {
        ++parser->cursor;
    }
}

// Store a parse diagnostic, taking ownership of `message`.
static void expr_parser_set_error(ExprParser *parser, size_t offset,
                                  String message) {
    ExprParseError *error = parser->error;

    if (error == NULL) {
        str_free(message);
        return;
    }

    str_free(error->message);
    error->offset = offset;
    error->message = message;
}

// Build a standard "expected ..., got ..." parse diagnostic message.
static String expr_parser_expected_message(const ExprParser *parser,
                                           const char *expected) {
    String message = str_from_cstr("expected ");
    str_append_cstr(message, expected);
    if (expr_parser_at_end(parser)) {
        str_append_cstr(message, ", got end of expression");
    } else {
        char got_char = expr_parser_peek(parser);
        str_span_sanitize_for_diagnostic(got_text, 16, str_span(&got_char, 1));

        str_append_cstr(message, ", got '");
        str_append_cstr(message, got_text);
        str_push(message, '\'');
    }
    return message;
}

// Store a standard expected-token parse diagnostic at the current cursor.
static void expr_parser_expected_error(ExprParser *parser,
                                       const char *expected) {
    expr_parser_set_error(parser, parser->cursor,
                          expr_parser_expected_message(parser, expected));
}

// Store a diagnostic for extra source text after a complete expression.
static void expr_parser_unexpected_text_error(ExprParser *parser) {
    StrSpan unexpected_text =
        str_span_slice(parser->text, parser->cursor, parser->text.len);
    str_span_sanitize_for_diagnostic(text, 80, unexpected_text);
    String message = str_from_cstr("unexpected text ");

    str_push(message, '\'');
    str_append_cstr(message, text);
    str_push(message, '\'');
    str_append_cstr(message, " after expression");
    expr_parser_set_error(parser, parser->cursor, message);
}

// Store a static parse diagnostic at the current cursor.
static void expr_parser_static_error(ExprParser *parser, const char *message) {
    expr_parser_set_error(parser, parser->cursor, str_from_cstr(message));
}

// Consume one exact byte from the parser cursor when present.
static bool expr_parser_consume_char(ExprParser *parser, char ch) {
    if (expr_parser_at_end(parser) || expr_parser_peek(parser) != ch)
        return false;

    ++parser->cursor;
    return true;
}

// Allocate a child expression node, aborting consistently on OOM.
static Expr *expr_alloc_node(void) {
    Expr *expr = malloc(sizeof(*expr));

    // IMGNEKO_UNCOVERED_OK[2 lines]
    if (expr == NULL)
        arr__abort_oom();

    *expr = (Expr){0};
    return expr;
}

// Parse a decimal or hexadecimal integer token.
static bool expr_parser_parse_number(ExprParser *parser, Expr *out) {
    size_t start = parser->cursor;
    ExprKind kind = EXPR_DECIMAL_INTEGER;

    if (expr_parser_peek(parser) == '0' &&
        parser->cursor + 1 < parser->text.len &&
        (parser->text.data[parser->cursor + 1] == 'x' ||
         parser->text.data[parser->cursor + 1] == 'X')) {
        kind = EXPR_HEX_INTEGER;
        parser->cursor += 2;
        if (expr_parser_at_end(parser) ||
            str_ascii_hex_digit_value(expr_parser_peek(parser)) < 0) {
            expr_parser_expected_error(parser, "a hexadecimal digit");
            return false;
        }
        while (!expr_parser_at_end(parser) &&
               str_ascii_hex_digit_value(expr_parser_peek(parser)) >= 0) {
            ++parser->cursor;
        }
    } else {
        while (!expr_parser_at_end(parser) &&
               str_char_is_ascii_digit(expr_parser_peek(parser))) {
            ++parser->cursor;
        }
    }

    *out = (Expr){
        .kind = kind,
        .token = str_span_slice(parser->text, start, parser->cursor),
    };
    return true;
}

// Parse a web-style `#rrggbb` color token.
static bool expr_parser_parse_color(ExprParser *parser, Expr *out) {
    size_t start = parser->cursor;

    assert(expr_parser_peek(parser) == '#');
    ++parser->cursor;
    for (size_t i = 0; i < 6; ++i) {
        if (expr_parser_at_end(parser) ||
            str_ascii_hex_digit_value(expr_parser_peek(parser)) < 0) {
            expr_parser_expected_error(parser, "a hexadecimal color digit");
            return false;
        }
        ++parser->cursor;
    }

    *out = (Expr){
        .kind = EXPR_COLOR,
        .token = str_span_slice(parser->text, start, parser->cursor),
    };
    return true;
}

// Parse a quoted string token and validate supported escapes.
static bool expr_parser_parse_string(ExprParser *parser, Expr *out) {
    size_t start = parser->cursor;
    char quote = expr_parser_peek(parser);

    assert(quote == '"' || quote == '\'');
    ++parser->cursor;

    while (!expr_parser_at_end(parser)) {
        char ch = parser->text.data[parser->cursor++];

        if (ch == quote) {
            *out = (Expr){
                .kind = EXPR_STRING,
                .token = str_span_slice(parser->text, start, parser->cursor),
            };
            return true;
        }

        if (ch != '\\')
            continue;

        if (expr_parser_at_end(parser)) {
            expr_parser_static_error(parser, "unterminated string literal");
            return false;
        }

        char escape = parser->text.data[parser->cursor++];
        switch (escape) {
        case '"':
        case '\'':
        case '\\':
        case 'n':
        case 'r':
        case 't':
            break;
        case 'x': {
            if (parser->cursor + 1 >= parser->text.len) {
                expr_parser_static_error(
                    parser, "string literal has an invalid \\xHH escape");
                return false;
            }

            int high =
                str_ascii_hex_digit_value(parser->text.data[parser->cursor]);
            int low = str_ascii_hex_digit_value(
                parser->text.data[parser->cursor + 1]);
            if (high < 0 || low < 0) {
                expr_parser_static_error(
                    parser, "string literal has an invalid \\xHH escape");
                return false;
            }

            if (((high << 4) | low) == 0) {
                expr_parser_static_error(
                    parser, "string literal escape \\x00 is unsupported");
                return false;
            }

            parser->cursor += 2;
            break;
        }
        default: {
            char escaped[] = {'\\', escape};
            str_span_sanitize_for_diagnostic(
                escaped_text, 16, str_span(escaped, sizeof(escaped)));
            String message = str_from_cstr("unknown string escape ");

            str_push(message, '\'');
            str_append_cstr(message, escaped_text);
            str_push(message, '\'');
            expr_parser_set_error(parser, parser->cursor - 1, message);
            return false;
        }
        }
    }

    expr_parser_static_error(parser, "unterminated string literal");
    return false;
}

// Parse an identifier token, or a call when it is followed by arguments.
static bool expr_parser_parse_identifier_or_call(ExprParser *parser,
                                                 Expr *out) {
    size_t start = parser->cursor;
    ExprArray args = arr_empty;

    assert(str_char_is_ascii_alpha(expr_parser_peek(parser)) ||
           expr_parser_peek(parser) == '_');
    ++parser->cursor;

    while (!expr_parser_at_end(parser) &&
           (str_char_is_ascii_alnum(expr_parser_peek(parser)) ||
            expr_parser_peek(parser) == '_')) {
        ++parser->cursor;
    }

    StrSpan name = str_span_slice(parser->text, start, parser->cursor);
    expr_parser_skip_spaces(parser);
    if (!expr_parser_consume_char(parser, '(')) {
        *out = (Expr){
            .kind = EXPR_IDENTIFIER,
            .token = name,
        };
        return true;
    }

    expr_parser_skip_spaces(parser);
    if (!expr_parser_consume_char(parser, ')')) {
        while (true) {
            Expr *arg = expr_alloc_node();

            if (!expr_parser_parse_expression(parser, arg)) {
                expr_deinit(arg);
                free(arg);
                goto fail;
            }
            arr_push(args, arg);

            expr_parser_skip_spaces(parser);
            if (expr_parser_consume_char(parser, ',')) {
                expr_parser_skip_spaces(parser);
                continue;
            }
            if (expr_parser_consume_char(parser, ')'))
                break;

            expr_parser_expected_error(parser,
                                       "',' or ')' in function argument list");
            goto fail;
        }
    }

    *out = (Expr){
        .kind = EXPR_CALL,
        .token = name,
        .args = args,
    };
    return true;

fail:
    for (size_t i = 0; i < args.size; ++i) {
        expr_deinit(args.data[i]);
        free(args.data[i]);
    }
    arr_free(args);
    return false;
}

// Parse the next primary expression token or call.
static bool expr_parser_parse_primary(ExprParser *parser, Expr *out) {
    expr_parser_skip_spaces(parser);
    char ch = expr_parser_peek(parser);

    if (ch == '"' || ch == '\'')
        return expr_parser_parse_string(parser, out);
    if (ch == '#')
        return expr_parser_parse_color(parser, out);
    if (str_char_is_ascii_digit(ch))
        return expr_parser_parse_number(parser, out);
    if (str_char_is_ascii_alpha(ch) || ch == '_')
        return expr_parser_parse_identifier_or_call(parser, out);

    expr_parser_expected_error(parser, "an expression");
    return false;
}

// Parse one expression from the current parser cursor.
static bool expr_parser_parse_expression(ExprParser *parser, Expr *out) {
    return expr_parser_parse_primary(parser, out);
}

bool expr_parse(StrSpan text, Expr *out, ExprParseError *error_out) {
    ExprParser parser = {
        .text = text,
        .error = error_out,
    };
    Expr root = {0};

    expr_parse_error_clear(error_out);
    if (!expr_parser_parse_expression(&parser, &root))
        return false;

    expr_parser_skip_spaces(&parser);
    if (!expr_parser_at_end(&parser)) {
        expr_parser_unexpected_text_error(&parser);
        expr_deinit(&root);
        return false;
    }

    expr_deinit(out);
    *out = root;
    return true;
}

Expr expr_copy(Expr expr) {
    Expr copy = expr;

    copy.args = (ExprArray)arr_empty;
    for (size_t i = 0; i < expr.args.size; ++i) {
        Expr *arg = expr_alloc_node();

        *arg = expr_copy(*expr.args.data[i]);
        arr_push(copy.args, arg);
    }

    return copy;
}

void expr_deinit(Expr *expr) {
    for (size_t i = 0; i < expr->args.size; ++i) {
        expr_deinit(expr->args.data[i]);
        free(expr->args.data[i]);
    }
    arr_free(expr->args);

    expr->kind = EXPR_INVALID;
    expr->token = str_span_empty;
}

String expr_string_literal_value(const Expr *expr) {
    assert(expr->kind == EXPR_STRING);
    assert(expr->token.len >= 2);

    StrSpan token = expr->token;
    String value = str_empty;

    for (size_t i = 1; i + 1 < token.len; ++i) {
        char ch = token.data[i];

        if (ch != '\\') {
            str_push(value, ch);
            continue;
        }

        assert(i + 1 < token.len);
        char escape = token.data[++i];
        switch (escape) {
        case '"':
        case '\'':
        case '\\':
            str_push(value, escape);
            break;
        case 'n':
            str_push(value, '\n');
            break;
        case 'r':
            str_push(value, '\r');
            break;
        case 't':
            str_push(value, '\t');
            break;
        case 'x': {
            assert(i + 2 < token.len);
            int high = str_ascii_hex_digit_value(token.data[i + 1]);
            int low = str_ascii_hex_digit_value(token.data[i + 2]);
            assert(high >= 0);
            assert(low >= 0);
            assert(((high << 4) | low) != 0);

            str_push(value, (char)((high << 4) | low));
            i += 2;
            break;
        }
        // IMGNEKO_UNCOVERED_OK_START: Parsed string literals validate escapes
        // before callers can decode them.
        default:
            assert(false && "invalid string escape in parsed expression");
            break;
            // IMGNEKO_UNCOVERED_OK_END
        }
    }

    return value;
}
