// SPDX-License-Identifier: MIT-0

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_main.h"
#include "util/common.h"

// Return one failure message for the active subtest.
static int fail_message(const char *subtest, const char *message) {
    fprintf(stderr, "%s: %s\n", subtest, message);
    return 1;
}

// Exercise an expected-failure C subtest, with an override that lets nested
// runner tests turn it into an unexpected success.
static int test_marked_xfail(TestContext *ctx) {
    const char *force_success = getenv("IMGNEKO_TEST_FORCE_SUCCESS");

    if (force_success != NULL && strcmp(force_success, "1") == 0)
        return 0;

    return fail_message(ctx->test_name, "intentional expected failure");
}

// This subtest must never run through the runner because it is marked
// disabled. If it runs, fail loudly.
static int test_marked_disabled(TestContext *ctx) {
    return fail_message(ctx->test_name, "disabled subtest ran unexpectedly");
}

int main(int argc, char **argv) {
    const Subtest subtests[] = {
        PREFIXED_XFAIL_TEST(test_marked_xfail),
        PREFIXED_DISABLED_TEST(test_marked_disabled),
    };

    return run_subtests(argc, argv, subtests, ARRAY_SIZE(subtests));
}
