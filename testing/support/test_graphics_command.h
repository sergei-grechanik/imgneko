// SPDX-License-Identifier: MIT-0

// Shared assertions for graphics-command unit tests.

#ifndef TEST_GRAPHICS_COMMAND_H
#define TEST_GRAPHICS_COMMAND_H

#include "test_main.h"
#include "util/string.h"

// Compare comma-separated graphics-command headers without depending on token
// order.
//
// `ctx`
//     Test context used for diagnostics.
// `actual`
//     Header produced by the operation under test.
// `expected`
//     Expected header tokens in any order.
// `description`
//     Human-readable description of the comparison.
int test_expect_graphics_command_header_tokens(TestContext *ctx, StrSpan actual,
                                               StrSpan expected,
                                               const char *description);

#endif
