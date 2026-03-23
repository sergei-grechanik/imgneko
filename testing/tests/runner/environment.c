#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_main.h"
#include "util/string.h"

// Emit one failure message for the current subtest and return a failing status.
static int fail_message(const char *subtest, const char *message) {
    fprintf(stderr, "%s: %s\n", subtest, message);
    return 1;
}

// Join two path segments into a newly allocated String. The caller owns the
// returned String and frees it with str_free.
static String join_two_paths(const char *left, const char *right) {
    String result = str_from_cstr(left);

    if (result.len > 0 && result.cstr[result.len - 1] != '/')
        str_push(result, '/');
    str_append_cstr(result, right);
    return result;
}

// Verify that C subtests receive the same stable output-tree environment as
// executable tests, with a path specific to the selected subtest.
static int test_output_env(TestContext *ctx) {
    const char *subtest = ctx->test_name;
    const char *build_dir = getenv("IMGNEKO_BUILD_DIR");
    const char *output_dir = getenv("IMGNEKO_TEST_OUTPUT_DIR");
    const char *output_file = getenv("IMGNEKO_TEST_OUTPUT_FILE");
    String expected_dir = str_empty;
    String expected_file = str_empty;
    int status = 0;

    if (build_dir == NULL || output_dir == NULL || output_file == NULL) {
        status = fail_message(subtest, "required test output env vars are unset");
        goto cleanup;
    }

    expected_dir = join_two_paths(build_dir, "test-outputs");
    expected_file = join_two_paths(expected_dir.cstr,
                                   "runner/environment.c/output_env.out");

    if (strcmp(output_dir, expected_dir.cstr) != 0) {
        status =
            fail_message(subtest, "IMGNEKO_TEST_OUTPUT_DIR has the wrong value");
        goto cleanup;
    }

    if (strcmp(output_file, expected_file.cstr) != 0) {
        status = fail_message(subtest,
                              "IMGNEKO_TEST_OUTPUT_FILE has the wrong value");
    }

cleanup:
    str_free(expected_file);
    str_free(expected_dir);
    return status;
}

int main(int argc, char **argv) {
    const Subtest subtests[] = {
        PREFIXED_TEST(test_output_env),
    };

    return run_subtests(argc, argv, subtests, ARRAY_SIZE(subtests));
}
