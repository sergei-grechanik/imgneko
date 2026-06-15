// SPDX-License-Identifier: MIT-0

#include "run-and-check-expr.h"

#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "imgneko/rowcolumn_diacritics.h"
#include "util/expr.h"
#include "util/options.h"

// Evaluation context for a run-and-check variable-use expression.
typedef struct RunAndCheckExprEval {
    const RunAndCheckExprContext *ctx;
    const char *text;
} RunAndCheckExprEval;

static void expression_errorf(const RunAndCheckExprEval *eval,
                              const char *format, ...) {
    const RunAndCheckExprContext *ctx = eval->ctx;
    cstr_sanitize_for_diagnostic(expr_text, 80, eval->text);

    fprintf(stderr, "%s:%d: error: invalid expression '%s' in %s: ", ctx->path,
            ctx->line_number, expr_text, ctx->directive_name);

    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);

    fputc('\n', stderr);
}

// Parse a decimal byte span as a uint32 value.
//
// `eval`
//     Expression evaluation context used for diagnostics.
// `what`
//     Human-readable value name used in diagnostics.
// `text`
//     Decimal byte span to parse.
// `len`
//     Byte length of `text`.
// `out`
//     Receives the parsed integer on success.
static bool parse_uint32_decimal_span(const RunAndCheckExprEval *eval,
                                      const char *what, const char *text,
                                      size_t len, uint32_t *out) {
    int64_t parsed = 0;

    if (!opt_parse_int64_span(text, len, &parsed) || parsed < 0 ||
        parsed > UINT32_MAX) {
        expression_errorf(eval, "%s must be an unsigned 32-bit decimal integer",
                          what);
        return false;
    }

    *out = (uint32_t)parsed;
    return true;
}

// Parse a decimal or 0x-prefixed hexadecimal byte span as a uint32 value.
//
// `eval`
//     Expression evaluation context used for diagnostics.
// `what`
//     Human-readable value name used in diagnostics.
// `text`
//     Decimal or hexadecimal byte span to parse.
// `len`
//     Byte length of `text`.
// `out`
//     Receives the parsed integer on success.
static bool parse_uint32_hex_or_decimal_span(const RunAndCheckExprEval *eval,
                                             const char *what, const char *text,
                                             size_t len, uint32_t *out) {
    uint64_t parsed = 0;

    if (!opt_parse_uint64_hex_or_decimal_span(text, len, &parsed) ||
        parsed > UINT32_MAX) {
        expression_errorf(eval,
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
// `eval`
//     Expression evaluation context used for diagnostics.
// `what`
//     Human-readable value name used for diagnostics.
// `text`
//     Decimal byte span to parse.
// `len`
//     Byte length of `text`.
// `out`
//     Receives the 1-based diacritic number on success.
static bool parse_ph_diacritic_num(const RunAndCheckExprEval *eval,
                                   const char *what, const char *text,
                                   size_t len, uint32_t *out) {
    uint32_t zero_based_num = 0;

    if (!parse_uint32_decimal_span(eval, what, text, len, &zero_based_num))
        return false;

    if (zero_based_num >= ROWCOLUMN_DIACRITIC_MAX) {
        expression_errorf(eval,
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
// `eval`
//     Expression evaluation context used for diagnostics.
// `arg`
//     Evaluated ph() column argument. A `start:end` string denotes an inclusive
//     zero-based column range.
// `start_out`
//     Receives the first 1-based column diacritic number on success.
// `end_out`
//     Receives the last 1-based column diacritic number on success.
static bool parse_ph_column_arg(const RunAndCheckExprEval *eval,
                                const String *arg, uint32_t *start_out,
                                uint32_t *end_out) {
    const char *colon = strchr(arg->cstr, ':');

    if (colon == NULL) {
        if (!parse_ph_diacritic_num(eval, "ph() column", arg->cstr, arg->len,
                                    start_out))
            return false;

        *end_out = *start_out;
        return true;
    }

    if (strchr(colon + 1, ':') != NULL) {
        expression_errorf(eval,
                          "ph() column range must contain exactly one ':'");
        return false;
    }

    size_t start_len = (size_t)(colon - arg->cstr);
    size_t end_len = arg->len - start_len - 1;
    if (!parse_ph_diacritic_num(eval, "ph() column range start", arg->cstr,
                                start_len, start_out) ||
        !parse_ph_diacritic_num(eval, "ph() column range end", colon + 1,
                                end_len, end_out)) {
        return false;
    }

    if (*start_out > *end_out) {
        expression_errorf(eval, "ph() column range start must be less "
                                "than or equal to the range end");
        return false;
    }

    return true;
}

// Build the literal byte sequence emitted by the imgneko placeholder command.
// Rows and columns are 0-based placeholder positions. When the column
// argument is a `start:end` string, the range end is inclusive.
static bool evaluate_ph_call(const RunAndCheckExprEval *eval,
                             const StringArray *args, String *out) {
    if (args->size > 3) {
        expression_errorf(eval, "ph() expects 0 to 3 arguments, got %zu",
                          args->size);
        return false;
    }

    uint32_t row_num = 0;
    if (args->size >= 1 &&
        !parse_ph_diacritic_num(eval, "ph() row", args->data[0].cstr,
                                args->data[0].len, &row_num)) {
        return false;
    }

    uint32_t col_start_num = 0;
    uint32_t col_end_num = 0;
    if (args->size >= 2 && !parse_ph_column_arg(eval, &args->data[1],
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
            eval,
            "ph() image id must be an unsigned decimal or hexadecimal integer");
        return false;
    }
    if (args->size >= 3 && parsed_image_id > UINT32_MAX) {
        expression_errorf(eval,
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

static bool evaluate_rgb_call(const RunAndCheckExprEval *eval,
                              const StringArray *args, String *out) {
    if (args->size != 1) {
        expression_errorf(eval, "rgb() expects 1 argument, got %zu",
                          args->size);
        return false;
    }

    uint32_t num = 0;
    if (!parse_uint32_hex_or_decimal_span(eval, "rgb() argument",
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
// `eval`
//     Expression evaluation context used for diagnostics.
// `function_name`
//     Function name token parsed before the opening parenthesis.
// `args`
//     Evaluated argument values owned by the caller.
// `out`
//     Receives an owned result value on success. Any previous value is freed
//     before assignment.
static bool evaluate_function_call(const RunAndCheckExprEval *eval,
                                   StrSpan function_name,
                                   const StringArray *args, String *out) {
    if (str_span_equals_cstr(function_name, "rgb"))
        return evaluate_rgb_call(eval, args, out);
    if (str_span_equals_cstr(function_name, "ph"))
        return evaluate_ph_call(eval, args, out);

    str_span_sanitize_for_diagnostic(name, 80, function_name);
    expression_errorf(eval, "unknown function '%s'", name);
    return false;
}

static bool evaluate_expr(const RunAndCheckExprEval *eval, const Expr *expr,
                          String *out);

static bool evaluate_identifier(const RunAndCheckExprEval *eval,
                                const Expr *expr, String *out) {
    String name = str_from_data(expr->token.data, expr->token.len);
    const RunAndCheckExprContext *ctx = eval->ctx;
    const char *value = ctx->lookup_variable(ctx->lookup_user_data, name.cstr);

    if (value == NULL) {
        str_sanitize_for_diagnostic(name_text, 80, name);
        fprintf(stderr, "%s:%d: error: undefined variable [[%s]] in %s\n",
                ctx->path, ctx->line_number, name_text, ctx->directive_name);
        str_free(name);
        return false;
    }

    str_free(*out);
    *out = str_from_cstr(value);
    str_free(name);
    return true;
}

static bool evaluate_call(const RunAndCheckExprEval *eval, const Expr *expr,
                          String *out) {
    StringArray args = arr_empty;

    for (size_t i = 0; i < expr->args.size; ++i) {
        String arg = str_empty;

        if (!evaluate_expr(eval, expr->args.data[i], &arg)) {
            str_array_free(&args);
            return false;
        }

        arr_push(args, arg);
    }

    bool ok = evaluate_function_call(eval, expr->token, &args, out);

    str_array_free(&args);
    return ok;
}

// Evaluate a parsed expression to an owned string value.
//
// `eval`
//     Expression evaluation context used for diagnostics and variable lookup.
// `expr`
//     Parsed expression node to evaluate.
// `out`
//     Receives an owned result value on success. Any previous value is freed
//     before assignment.
static bool evaluate_expr(const RunAndCheckExprEval *eval, const Expr *expr,
                          String *out) {
    switch (expr->kind) { // IMGNEKO_UNCOVERED_OK: EXPR_INVALID is impossible.
    case EXPR_IDENTIFIER:
        return evaluate_identifier(eval, expr, out);
    case EXPR_CALL:
        return evaluate_call(eval, expr, out);
    case EXPR_DECIMAL_INTEGER:
    case EXPR_HEX_INTEGER:
    case EXPR_COLOR:
        str_free(*out);
        *out = str_from_data(expr->token.data, expr->token.len);
        return true;
    case EXPR_STRING:
        str_free(*out);
        *out = expr_string_literal_value(expr);
        return true;
    // IMGNEKO_UNCOVERED_OK_START
    case EXPR_INVALID:
        break;
    }

    assert(false && "invalid expression node");
    return false;
    // IMGNEKO_UNCOVERED_OK_END
}

bool run_and_check_evaluate_expression(const RunAndCheckExprContext *ctx,
                                       const char *text, String *out) {
    RunAndCheckExprEval eval = {
        .ctx = ctx,
        .text = text,
    };
    Expr expr = {0};
    ExprParseError error = {0};
    String value = str_empty;
    bool ok = false;

    if (!expr_parse(str_span(text, strlen(text)), &expr, &error)) {
        expression_errorf(&eval, "%s", error.message.cstr);
        goto cleanup;
    }

    if (!evaluate_expr(&eval, &expr, &value))
        goto cleanup;

    str_free(*out);
    *out = value;
    value = str_empty;
    ok = true;

cleanup:
    str_free(value);
    expr_parse_error_clear(&error);
    expr_deinit(&expr);
    return ok;
}
