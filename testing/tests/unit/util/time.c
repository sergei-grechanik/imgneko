// Enable POSIX APIs used in this file (timespec).
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <time.h>

#include "test_main.h"
#include "util/time.h"

// Print one failure message for a subtest and return a failing status code.
static int fail_message(const char *subtest, const char *message) {
    fprintf(stderr, "%s: %s\n", subtest, message);
    return 1;
}

// Verify one timeout result exactly.
static int expect_timeout_eq(const char *subtest, struct timespec actual,
                             time_t expected_sec, long expected_nsec) {
    if (actual.tv_sec != expected_sec) {
        fprintf(stderr, "%s: expected tv_sec %ld, got %ld\n", subtest,
                (long)expected_sec, (long)actual.tv_sec);
        return 1;
    }

    if (actual.tv_nsec != expected_nsec) {
        fprintf(stderr, "%s: expected tv_nsec %ld, got %ld\n", subtest,
                expected_nsec, actual.tv_nsec);
        return 1;
    }

    return 0;
}

// Verify expired deadlines and ordinary fractional timeouts.
static int test_timeout_until_deadline(TestContext *ctx) {
    const char *name = ctx->test_name;
    struct timespec expired = time_timeout_until_deadline(5.0, 5.0);
    struct timespec past = time_timeout_until_deadline(4.0, 5.0);
    struct timespec fractional = time_timeout_until_deadline(12.75, 10.25);
    int status = 0;

    status = expect_timeout_eq(name, expired, 0, 0);
    if (status != 0)
        return status;

    status = expect_timeout_eq(name, past, 0, 0);
    if (status != 0)
        return status;

    return expect_timeout_eq(name, fractional, 2, 500000000L);
}

// Verify that a tiny positive timeout still rounds up to a minimal non-zero
// wait instead of collapsing to zero.
static int test_timeout_until_deadline_rounds_up_minimum(TestContext *ctx) {
    const char *name = ctx->test_name;
    struct timespec timeout = time_timeout_until_deadline(10.0 + 0.4e-9, 10.0);

    if (timeout.tv_sec != 0 || timeout.tv_nsec != 1)
        return fail_message(name, "tiny positive timeout did not round up");

    return 0;
}

int main(int argc, char **argv) {
    const Subtest subtests[] = {
        PREFIXED_TEST(test_timeout_until_deadline),
        PREFIXED_TEST(test_timeout_until_deadline_rounds_up_minimum),
    };

    return run_subtests(argc, argv, subtests,
                        sizeof(subtests) / sizeof(subtests[0]));
}
