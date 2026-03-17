#ifndef UTIL_TEST_MAIN_H
#define UTIL_TEST_MAIN_H

#include <stddef.h>

#include "util/common.h"

typedef struct TestContext {
    const char *test_name;
} TestContext;

typedef struct Subtest {
    const char *name;
    int (*func)(TestContext *ctx);
} Subtest;

// Verify that test_func starts with "test_" and produce the displayed subtest
// name without that prefix.
#define TEST__SUBTEST_NAME(test_func)                                          \
    ((#test_func) + 5 +                                                        \
     0 * sizeof(char[((#test_func)[0] == 't' && (#test_func)[1] == 'e' &&      \
                      (#test_func)[2] == 's' && (#test_func)[3] == 't' &&      \
                      (#test_func)[4] == '_')                                  \
                         ? 1                                                   \
                         : -1]))

// Build a Subtest entry from a function named with the required test_ prefix.
#define PREFIXED_TEST(test_func)                                               \
    { .name = TEST__SUBTEST_NAME(test_func), .func = (test_func), }

// Run named subtests selected by argv and return the combined status code.
int run_subtests(int argc, char **argv, const Subtest *subtests,
                 size_t subtest_count);

#endif
