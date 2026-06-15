// SPDX-License-Identifier: MIT-0

#include <stdio.h>
#include <string.h>

#include "test_main.h"
#include "util/common.h"
#include "util/expr.h"

#define STR(text) (text), (sizeof(text) - 1)

// Print a subtest failure message and return a failing status code.
static int fail_message(const char *subtest, const char *message) {
    fprintf(stderr, "%s: %s\n", subtest, message);
    return 1;
}

// Compare a span against expected bytes. Expression tokens are borrowed spans,
// so tests must compare length-aware views.
static int expect_span_eq(const char *subtest, StrSpan actual,
                          const char *expected_data, size_t expected_len) {
    if (actual.len != expected_len) {
        fprintf(stderr, "%s: expected span length %zu, got %zu\n", subtest,
                expected_len, actual.len);
        return 1;
    }

    if (memcmp(actual.data, expected_data, actual.len) != 0) {
        fprintf(stderr, "%s: span contents differ\n", subtest);
        return 1;
    }

    return 0;
}

// Compare an owned string against expected bytes, including embedded escapes
// decoded by expression string literals.
static int expect_string_eq(const char *subtest, String actual,
                            const char *expected_data, size_t expected_len) {
    if (actual.len != expected_len) {
        fprintf(stderr, "%s: expected string length %zu, got %zu\n", subtest,
                expected_len, actual.len);
        return 1;
    }

    if (memcmp(actual.cstr, expected_data, actual.len) != 0) {
        fprintf(stderr, "%s: string contents differ\n", subtest);
        return 1;
    }

    return 0;
}

// Parse an expression or print the parser diagnostic as the subtest failure.
static int parse_or_fail(const char *subtest, const char *text, Expr *expr) {
    ExprParseError error = {0};

    if (expr_parse(str_span(text, strlen(text)), expr, &error)) {
        expr_parse_error_clear(&error);
        return 0;
    }

    fprintf(stderr, "%s: parse failed: %s\n", subtest, error.message.cstr);
    expr_parse_error_clear(&error);
    return 1;
}

// Nested calls should preserve each function and identifier token while
// building a recursive argument tree.
static int test_nested_call_ast(TestContext *ctx) {
    const char *name = ctx->test_name;
    Expr expr = {0};
    int status = 0;

    status = parse_or_fail(name, " f(g(a, b), c) ", &expr);
    if (status != 0)
        goto cleanup;

    const Expr *root = &expr;
    if (root->kind != EXPR_CALL || root->args.size != 2) {
        status = fail_message(name, "root call shape is wrong");
        goto cleanup;
    }
    if (!expr_is_call(root, "f") || expr_is_call(root, "g")) {
        status = fail_message(name, "expr_is_call classified the root wrong");
        goto cleanup;
    }
    status = expect_span_eq(name, root->token, STR("f"));
    if (status != 0)
        goto cleanup;

    const Expr *nested = root->args.data[0];
    if (nested->kind != EXPR_CALL || nested->args.size != 2) {
        status = fail_message(name, "nested call shape is wrong");
        goto cleanup;
    }
    if (!expr_is_call(nested, "g") || expr_is_call(nested, "f")) {
        status =
            fail_message(name, "expr_is_call classified the nested call wrong");
        goto cleanup;
    }
    status = expect_span_eq(name, nested->token, STR("g"));
    if (status != 0)
        goto cleanup;

    status = expect_span_eq(name, nested->args.data[0]->token, STR("a"));
    if (status != 0)
        goto cleanup;
    status = expect_span_eq(name, nested->args.data[1]->token, STR("b"));
    if (status != 0)
        goto cleanup;

    if (root->args.data[1]->kind != EXPR_IDENTIFIER) {
        status =
            fail_message(name, "second root argument is not an identifier");
        goto cleanup;
    }
    status = expect_span_eq(name, root->args.data[1]->token, STR("c"));

cleanup:
    expr_deinit(&expr);
    return status;
}

// Literal nodes should classify the token kind without losing the original
// spelling, including uppercase hex prefixes and color literals.
static int test_literal_kinds(TestContext *ctx) {
    const char *name = ctx->test_name;
    const struct {
        const char *text;
        ExprKind kind;
        const char *token;
    } cases[] = {
        {.text = "0", .kind = EXPR_DECIMAL_INTEGER, .token = "0"},
        {.text = "123", .kind = EXPR_DECIMAL_INTEGER, .token = "123"},
        {.text = "0Xab", .kind = EXPR_HEX_INTEGER, .token = "0Xab"},
        {.text = "#ff0012", .kind = EXPR_COLOR, .token = "#ff0012"},
        {.text = "_name", .kind = EXPR_IDENTIFIER, .token = "_name"},
    };

    for (size_t i = 0; i < ARRAY_SIZE(cases); ++i) {
        Expr expr = {0};
        int status = parse_or_fail(name, cases[i].text, &expr);

        if (status == 0 && expr.kind != cases[i].kind)
            status = fail_message(name, "literal kind is wrong");
        if (status == 0)
            status = expect_span_eq(name, expr.token, cases[i].token,
                                    strlen(cases[i].token));
        if (status == 0 && expr.kind != EXPR_CALL &&
            expr_is_call(&expr, cases[i].token)) {
            status = fail_message(name, "expr_is_call accepted a literal");
        }
        expr_deinit(&expr);
        if (status != 0)
            return status;
    }

    return 0;
}

// Both quote styles use the same escape rules and decode to owned string
// values for callers that need literal bytes instead of token text.
static int test_string_literal_values(TestContext *ctx) {
    const char *name = ctx->test_name;
    Expr double_expr = {0};
    Expr single_expr = {0};
    String double_value = str_empty;
    String single_value = str_empty;
    int status = 0;

    status = parse_or_fail(name, "\"str\\n\\r\\x41\"", &double_expr);
    if (status != 0)
        goto cleanup;
    status = parse_or_fail(name, "'it\\'s\\tok'", &single_expr);
    if (status != 0)
        goto cleanup;

    if (double_expr.kind != EXPR_STRING || single_expr.kind != EXPR_STRING) {
        status = fail_message(name, "string literal kind is wrong");
        goto cleanup;
    }

    double_value = expr_string_literal_value(&double_expr);
    single_value = expr_string_literal_value(&single_expr);

    status = expect_string_eq(name, double_value, STR("str\n\rA"));
    if (status != 0)
        goto cleanup;
    status = expect_string_eq(name, single_value, STR("it's\tok"));

cleanup:
    str_free(single_value);
    str_free(double_value);
    expr_deinit(&single_expr);
    expr_deinit(&double_expr);
    return status;
}

// Copying an expression must duplicate the child node graph while preserving
// the borrowed token spans supplied by the caller.
static int test_expr_copy_duplicates_children(TestContext *ctx) {
    const char *name = ctx->test_name;
    Expr expr = {0};
    Expr copy = {0};
    int status = 0;

    status = parse_or_fail(name, "outer(inner(1), #abcdef)", &expr);
    if (status != 0)
        goto cleanup;

    copy = expr_copy(expr);
    if (copy.args.data[0] == expr.args.data[0]) {
        status = fail_message(name, "copy reused an original child node");
        goto cleanup;
    }

    expr_deinit(&expr);
    status = expect_span_eq(name, copy.token, STR("outer"));
    if (status != 0)
        goto cleanup;
    status = expect_span_eq(name, copy.args.data[0]->token, STR("inner"));
    if (status != 0)
        goto cleanup;
    status = expect_span_eq(name, copy.args.data[1]->token, STR("#abcdef"));

cleanup:
    expr_deinit(&copy);
    expr_deinit(&expr);
    return status;
}

// Representative parser errors should carry clear messages that callers can
// embed into their own source-location diagnostics.
static int test_parse_errors(TestContext *ctx) {
    const char *name = ctx->test_name;
    const struct {
        const char *text;
        const char *message;
    } cases[] = {
        {.text = "  ",
         .message = "expected an expression, got end of expression"},
        {.text = "0x",
         .message = "expected a hexadecimal digit, got end of expression"},
        {.text = "0xz", .message = "expected a hexadecimal digit, got 'z'"},
        {.text = "f(1 2)",
         .message = "expected ',' or ')' in function argument list, got '2'"},
        {.text = "\001", .message = "expected an expression, got '<01>'"},
        {.text = "1\001", .message = "unexpected text '<01>' after expression"},
        {.text = "\"\\q\"", .message = "unknown string escape '\\q'"},
        {.text = "\"abc\\", .message = "unterminated string literal"},
        {.text = "'unterminated", .message = "unterminated string literal"},
        {.text = "#ff00zz",
         .message = "expected a hexadecimal color digit, got 'z'"},
    };

    for (size_t i = 0; i < ARRAY_SIZE(cases); ++i) {
        Expr expr = {0};
        ExprParseError error = {0};

        if (expr_parse(str_span(cases[i].text, strlen(cases[i].text)), &expr,
                       &error)) {
            expr_deinit(&expr);
            expr_parse_error_clear(&error);
            return fail_message(name, "invalid expression parsed");
        }

        int status = expect_string_eq(name, error.message, cases[i].message,
                                      strlen(cases[i].message));
        expr_parse_error_clear(&error);
        if (status != 0)
            return status;
    }

    return 0;
}

// Callers that only need a boolean parse result can omit the diagnostic sink.
static int test_null_parse_error_sink(TestContext *ctx) {
    Expr expr = {0};

    if (expr_parse(str_span("!", 1), &expr, NULL)) {
        expr_deinit(&expr);
        return fail_message(ctx->test_name, "invalid expression parsed");
    }

    expr_parse_error_clear(NULL);
    expr_deinit(&expr);
    return 0;
}

int main(int argc, char **argv) {
    const Subtest subtests[] = {
        PREFIXED_TEST(test_nested_call_ast),
        PREFIXED_TEST(test_literal_kinds),
        PREFIXED_TEST(test_string_literal_values),
        PREFIXED_TEST(test_expr_copy_duplicates_children),
        PREFIXED_TEST(test_parse_errors),
        PREFIXED_TEST(test_null_parse_error_sink),
    };

    return run_subtests(argc, argv, subtests, ARRAY_SIZE(subtests));
}
