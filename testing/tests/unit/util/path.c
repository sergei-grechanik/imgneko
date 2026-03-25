// Enable POSIX APIs used in this file (getcwd).
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "test_main.h"
#include "util/path.h"

#define STR(text) (text), (sizeof(text) - 1)

// Print one failure message for a subtest and return a failing status code.
static int fail_message(const char *subtest, const char *message) {
    fprintf(stderr, "%s: %s\n", subtest, message);
    return 1;
}

// Compare two strings for exact equality, including the NUL terminator.
static int expect_string_eq(const char *subtest, const char *actual_data,
                            size_t actual_len, const char *expected_data,
                            size_t expected_len) {
    if (actual_len != expected_len) {
        fprintf(stderr, "%s: expected length %zu, got %zu\n", subtest,
                expected_len, actual_len);
        return 1;
    }

    if (memcmp(actual_data, expected_data, actual_len + 1) != 0) {
        fprintf(stderr, "%s: string contents differ\n", subtest);
        return 1;
    }

    return 0;
}

// Verify that appending one segment adds exactly one separator between
// components.
static int test_append_segment(TestContext *ctx) {
    const char *name = ctx->test_name;
    String appended = str_from_cstr("/tmp/example");
    String appended_with_slash = str_from_cstr("/tmp/example/");
    int status = 0;

    path_append(&appended, "child");
    path_append(&appended_with_slash, "child");

    status = expect_string_eq(name, appended.cstr, appended.len,
                              STR("/tmp/example/child"));
    if (status != 0)
        goto cleanup;

    status =
        expect_string_eq(name, appended_with_slash.cstr,
                         appended_with_slash.len, STR("/tmp/example/child"));

cleanup:
    str_free(appended_with_slash);
    str_free(appended);
    return status;
}

// Verify that path_join returns the same normalized single-separator result.
static int test_join_segments(TestContext *ctx) {
    const char *name = ctx->test_name;
    String joined = path_join("/tmp/example", "child");
    int status = expect_string_eq(name, joined.cstr, joined.len,
                                  STR("/tmp/example/child"));

    str_free(joined);
    return status;
}

// Verify that trimming removes trailing slashes but preserves the root path.
static int test_trim_trailing_slashes(TestContext *ctx) {
    const char *name = ctx->test_name;
    String root = str_from_cstr("/");
    String nested = str_from_cstr("/tmp/example///");
    int status = 0;

    path_trim_trailing_slashes(&root);
    path_trim_trailing_slashes(&nested);

    status = expect_string_eq(name, root.cstr, root.len, STR("/"));
    if (status != 0)
        goto cleanup;

    status =
        expect_string_eq(name, nested.cstr, nested.len, STR("/tmp/example"));

cleanup:
    str_free(nested);
    str_free(root);
    return status;
}

// Verify that absolute-path resolution handles both relative and absolute
// inputs, and safely replaces a previously returned String.
static int test_resolve_absolute(TestContext *ctx) {
    const char *name = ctx->test_name;
    char cwd[4096];
    String expected = str_empty;
    String resolved = str_empty;
    int status = 0;

    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        status = fail_message(name, "getcwd failed");
        goto cleanup;
    }

    expected = str_from_cstr(cwd);
    path_append(&expected, "child");
    if (!path_resolve_absolute(&resolved, "child")) {
        status = fail_message(name, "failed to resolve a relative path");
        goto cleanup;
    }

    status = expect_string_eq(name, resolved.cstr, resolved.len, expected.cstr,
                              expected.len);
    if (status != 0)
        goto cleanup;

    if (!path_resolve_absolute(&resolved, "/tmp/example///")) {
        status = fail_message(name, "failed to resolve an absolute path");
        goto cleanup;
    }

    status = expect_string_eq(name, resolved.cstr, resolved.len,
                              STR("/tmp/example"));

cleanup:
    str_free(resolved);
    str_free(expected);
    return status;
}

int main(int argc, char **argv) {
    const Subtest subtests[] = {
        PREFIXED_TEST(test_append_segment),
        PREFIXED_TEST(test_join_segments),
        PREFIXED_TEST(test_trim_trailing_slashes),
        PREFIXED_TEST(test_resolve_absolute),
    };

    return run_subtests(argc, argv, subtests, ARRAY_SIZE(subtests));
}
