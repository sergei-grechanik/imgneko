#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_main.h"

// Emit one failure message for the current subtest and return a failing status.
static int fail_message(const char *subtest, const char *message) {
    fprintf(stderr, "%s: %s\n", subtest, message);
    return 1;
}

// Verify the runner output env and emit deterministic stdout/stderr so the
// runner can validate per-test output capture for C tests too.
static int test_emit_output(TestContext *ctx) {
    const char *subtest = ctx->test_name;
    const char *output_dir = getenv("IMGNEKO_TEST_OUTPUT_DIR");
    const char *output_file = getenv("IMGNEKO_TEST_OUTPUT_FILE");

    if (output_dir == NULL || output_file == NULL)
        return fail_message(subtest, "required test output env vars are unset");

    if (output_dir[0] != '/')
        return fail_message(subtest, "IMGNEKO_TEST_OUTPUT_DIR is not absolute");

    if (strncmp(output_file, output_dir, strlen(output_dir)) != 0 ||
        output_file[strlen(output_dir)] != '/') {
        return fail_message(subtest,
                            "IMGNEKO_TEST_OUTPUT_FILE is not under the output "
                            "directory");
    }

    if (getenv("IMGNEKO_TEST_SHOULD_FAIL") != NULL &&
        strcmp(getenv("IMGNEKO_TEST_SHOULD_FAIL"), "1") == 0) {
        for (int i = 1; i <= 25; ++i)
            printf("c failure line %d\n", i);
        return 1;
    }

    printf("%s\n", "output C test stdout marker");
    fprintf(stderr, "%s\n", "output C test stderr marker");
    return 0;
}

int main(int argc, char **argv) {
    const Subtest subtests[] = {
        PREFIXED_TEST(test_emit_output),
    };

    return run_subtests(argc, argv, subtests, ARRAY_SIZE(subtests));
}
