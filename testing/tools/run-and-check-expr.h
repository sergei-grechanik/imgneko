// SPDX-License-Identifier: MIT-0

#ifndef TESTING_TOOLS_RUN_AND_CHECK_EXPR_H
#define TESTING_TOOLS_RUN_AND_CHECK_EXPR_H

#include <stdbool.h>
#include <stddef.h>

#include "util/string.h"

// Look up a captured variable by name. The returned pointer must stay valid
// until expression evaluation finishes.
typedef const char *RunAndCheckVariableLookupFn(const void *user_data,
                                                const char *name);

// Context used to evaluate a run-and-check `[[...]]` expression and report
// diagnostics that point back to the containing CHECK directive.
typedef struct RunAndCheckExprContext {
    const char *path;
    int line_number;
    const char *directive_name;
    RunAndCheckVariableLookupFn *lookup_variable;
    const void *lookup_user_data;
} RunAndCheckExprContext;

// Evaluate a complete `[[...]]` variable-use expression.
//
// `ctx`
//     Source location, directive name, and variable lookup callback used by the
//     evaluator.
// `text`
//     Expression text without the surrounding `[[...]]`.
// `out`
//     Receives the owned literal bytes produced by the expression on success.
//     The caller frees them with str_free. Any previous value is freed before
//     assignment.
//
// Returns true on success. Returns false after printing a diagnostic when the
// expression evaluation fails.
bool run_and_check_evaluate_expression(const RunAndCheckExprContext *ctx,
                                       const char *text, String *out);

#endif
