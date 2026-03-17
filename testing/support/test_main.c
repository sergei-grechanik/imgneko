#include <stdio.h>
#include <string.h>

#include "test_main.h"

// Run named subtests selected by argv and return the combined status code.
int run_subtests(int argc, char **argv, const Subtest *subtests,
                 size_t subtest_count) {
    TestContext ctx;
    int status = 0;

    if (argc > 1 && strcmp(argv[1], "--list") == 0) {
        for (size_t i = 0; i < subtest_count; ++i)
            puts(subtests[i].name);
        return 0;
    }

    if (argc == 1 || (argc > 1 && strcmp(argv[1], "--all") == 0)) {
        for (size_t i = 0; i < subtest_count; ++i) {
            ctx.test_name = subtests[i].name;
            status |= subtests[i].func(&ctx);
        }
        return status;
    }

    for (int i = 1; i < argc; ++i) {
        size_t j;

        for (j = 0; j < subtest_count; ++j) {
            if (strcmp(argv[i], subtests[j].name) != 0)
                continue;

            ctx.test_name = subtests[j].name;
            status |= subtests[j].func(&ctx);
            break;
        }

        if (j == subtest_count) {
            fprintf(stderr, "unknown subtest: %s\n", argv[i]);
            return 1;
        }
    }

    return status;
}
