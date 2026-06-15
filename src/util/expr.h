// SPDX-License-Identifier: MIT-0

// Small expression parser shared by command-line and test tooling code.

#ifndef UTIL_EXPR_H
#define UTIL_EXPR_H

#include <stdbool.h>
#include <stddef.h>

#include "util/array.h"
#include "util/string.h"

// Kind of a parsed expression node.
typedef enum ExprKind {
    EXPR_INVALID = 0,
    // Bare identifier, usually interpreted by callers as a variable name.
    EXPR_IDENTIFIER,
    // Function call with the function name in `token` and arguments in `args`.
    EXPR_CALL,
    // Base-10 unsigned integer token.
    EXPR_DECIMAL_INTEGER,
    // 0x-prefixed unsigned hexadecimal integer token.
    EXPR_HEX_INTEGER,
    // Web-style #rrggbb color token.
    EXPR_COLOR,
    // Single- or double-quoted string literal token, including quotes.
    EXPR_STRING,
} ExprKind;

typedef struct Expr Expr;

// Owning array of child expression node pointers.
DEFINE_ARRAY_TYPE(ExprArray, Expr *)

// A parsed expression node.
//
// `token` is a span into the source passed to expr_parse(). For calls, `token`
// is the function name. For literals and identifiers, `token` is the complete
// token text. Callers must keep the source text alive while using the node.
struct Expr {
    ExprKind kind;
    StrSpan token;
    ExprArray args;
};

// Return true when `expr` is a call to `name`.
ARRLIB_INLINE bool expr_is_call(const Expr *expr, const char *name) {
    return expr->kind == EXPR_CALL && str_span_equals_cstr(expr->token, name);
}

// Parse diagnostic produced by expr_parse().
//
// `message` is an owned human-readable error. Destroy it with
// expr_parse_error_clear() after use.
typedef struct ExprParseError {
    size_t offset;
    String message;
} ExprParseError;

// Parse a complete expression.
//
// Supported grammar:
// - function calls: `f(g(a, b), c)`
// - identifiers: `name`
// - decimal integers: `123`
// - hexadecimal integers: `0x123` or `0X123`
// - colors: `#ff0012`
// - string literals: `"str\n"` or `'str\n'`
//
// Whitespace is allowed around expressions, after a function name before `(`,
// and around commas and arguments.
//
// `text`
//     Source text to parse. It does not need to be null-terminated.
// `out`
//     Receives the parsed AST on success. The AST borrows token spans from
//     `text`, and any previous initialized value in `out` is freed before
//     assignment.
// `error_out`
//     Optional parse diagnostic populated on failure. Any previous diagnostic
//     in an initialized `error_out` is cleared before parsing.
//
// Returns true on success. Returns false on parse failure and leaves `out`
// unchanged.
bool expr_parse(StrSpan text, Expr *out, ExprParseError *error_out);

// Deep-copy an expression node and its child nodes.
//
// The returned node has the same token spans as `expr`. The caller owns the
// returned node and destroys it with expr_deinit().
Expr expr_copy(Expr expr);

// Destroy an expression node and its child nodes. This releases storage owned
// by the node, but does not free the Expr object itself.
void expr_deinit(Expr *expr);

// Clear a parse diagnostic and release its owned message.
void expr_parse_error_clear(ExprParseError *error);

// Decode a parsed string literal into owned bytes.
//
// `expr` must be an EXPR_STRING node produced by expr_parse(). The returned
// String is owned by the caller and must be freed with str_free().
String expr_string_literal_value(const Expr *expr);

#endif
