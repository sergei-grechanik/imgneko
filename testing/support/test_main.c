#include <stdio.h>
#include <string.h>

#include "test_main.h"

// Map a subtest marker to the suffix printed by `--list`, or NULL for normal
// tests.
static const char *test_marker_name(TestMarker marker) {
    switch (marker) { // IMGNEKO_UNCOVERED_OK
    case TEST_MARKER_NONE:
        return NULL;
    case TEST_MARKER_XFAIL:
        return "XFAIL";
    case TEST_MARKER_DISABLED:
        return "DISABLED";
    }

    return NULL; // IMGNEKO_UNCOVERED_OK
}

// Run named subtests selected by argv and return the combined status code.
int run_subtests(int argc, char **argv, const Subtest *subtests,
                 size_t subtest_count) {
    TestContext ctx;
    int status = 0;

    if (argc > 1 && strcmp(argv[1], "--list") == 0) {
        for (size_t i = 0; i < subtest_count; ++i) {
            const char *marker_name = test_marker_name(subtests[i].marker);

            if (marker_name == NULL)
                puts(subtests[i].name);
            else
                printf("%s %s\n", subtests[i].name, marker_name);
        }
        return 0;
    }

    if (argc < 2 || strcmp(argv[1], "--all") == 0) {
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
