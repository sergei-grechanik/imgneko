// SPDX-License-Identifier: MIT-0

#ifndef UTIL_TEST_MAIN_H
#define UTIL_TEST_MAIN_H

#include <stddef.h>

typedef struct TestContext {
    const char *test_name;
} TestContext;

// Optional execution expectation attached to a discovered test.
typedef enum TestMarker {
    TEST_MARKER_NONE,
    TEST_MARKER_XFAIL,
    TEST_MARKER_DISABLED,
} TestMarker;

typedef struct Subtest {
    const char *name;
    int (*func)(TestContext *ctx);
    TestMarker marker;
} Subtest;

// Return IMGNEKO_TEST_OUTPUT_DIR after verifying that it is set to an
// existing directory. On failure, print one test-style error message and
// return NULL.
const char *test_get_output_dir(const TestContext *ctx);

// Verify that test_func starts with "test_" and produce the displayed subtest
// name without that prefix.
// IMGNEKO_UNCOVERED_OK_START
#define TEST__SUBTEST_NAME(test_func)                                          \
    ((#test_func) + 5 +                                                        \
     0 * sizeof(char[((#test_func)[0] == 't' && (#test_func)[1] == 'e' &&      \
                      (#test_func)[2] == 's' && (#test_func)[3] == 't' &&      \
                      (#test_func)[4] == '_')                                  \
                         ? 1                                                   \
                         : -1]))
// IMGNEKO_UNCOVERED_OK_END

// Build a Subtest entry from a function named with the required test_ prefix.
#define PREFIXED_TEST(test_func)                                               \
    {                                                                          \
        .name = TEST__SUBTEST_NAME(test_func), .func = (test_func),            \
        .marker = TEST_MARKER_NONE,                                            \
    }

// Build an expected-failure Subtest entry from a function with the required
// test_ prefix.
#define PREFIXED_XFAIL_TEST(test_func)                                         \
    {                                                                          \
        .name = TEST__SUBTEST_NAME(test_func), .func = (test_func),            \
        .marker = TEST_MARKER_XFAIL,                                           \
    }

// Build a disabled Subtest entry from a function with the required test_
// prefix.
#define PREFIXED_DISABLED_TEST(test_func)                                      \
    {                                                                          \
        .name = TEST__SUBTEST_NAME(test_func), .func = (test_func),            \
        .marker = TEST_MARKER_DISABLED,                                        \
    }

// Run named subtests selected by argv and return the combined status code.
int run_subtests(int argc, char **argv, const Subtest *subtests,
                 size_t subtest_count);

#endif
