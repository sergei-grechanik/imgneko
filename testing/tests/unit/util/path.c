// SPDX-License-Identifier: MIT-0

// Keep Darwin extension declarations, such as mkdtemp(), visible when strict
// POSIX feature-test macros are enabled.
#define _DARWIN_C_SOURCE
// Enable POSIX APIs used in this file (getcwd).
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "test_main.h"
#include "util/common.h"
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

// Verify that `path` names an existing directory.
static int expect_directory_exists(const char *subtest, const char *path) {
    struct stat st;

    if (stat(path, &st) != 0) {
        fprintf(stderr, "%s: stat failed for %s\n", subtest, path);
        return 1;
    }

    if (!S_ISDIR(st.st_mode)) {
        fprintf(stderr, "%s: %s is not a directory\n", subtest, path);
        return 1;
    }

    return 0;
}

// Best-effort removal for a directory created during the test.
static int cleanup_directory(const char *subtest, const char *path) {
    if (path == NULL || access(path, F_OK) != 0)
        return 0;

    if (rmdir(path) != 0) {
        fprintf(stderr, "%s: rmdir failed for %s\n", subtest, path);
        return 1;
    }

    return 0;
}

// Verify that appending one segment adds exactly one separator between
// components.
static int test_append_segment(TestContext *ctx) {
    const char *name = ctx->test_name;
    String empty = str_empty;
    String appended = str_from_cstr("/tmp/example");
    String appended_with_slash = str_from_cstr("/tmp/example/");
    int status = 0;

    path_append(&empty, "child");
    path_append(&appended, "child");
    path_append(&appended_with_slash, "child");

    status = expect_string_eq(name, empty.cstr, empty.len, STR("child"));
    if (status != 0)
        goto cleanup;

    status = expect_string_eq(name, appended.cstr, appended.len,
                              STR("/tmp/example/child"));
    if (status != 0)
        goto cleanup;

    status =
        expect_string_eq(name, appended_with_slash.cstr,
                         appended_with_slash.len, STR("/tmp/example/child"));

cleanup:
    str_free(empty);
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

// Verify that mkdir_p creates each missing parent directory in order.
static int test_mkdir_p_creates_nested_directories(TestContext *ctx) {
    const char *name = ctx->test_name;
    char template[] = "/tmp/imgneko-path.XXXXXX";
    char *temp_dir = NULL;
    String level_one = str_empty;
    String level_two = str_empty;
    String level_three = str_empty;
    int status = 0;

    temp_dir = mkdtemp(template);
    if (temp_dir == NULL) {
        status = fail_message(name, "mkdtemp failed");
        goto cleanup;
    }

    level_one = path_join(temp_dir, "one");
    level_two = path_join(level_one.cstr, "two");
    level_three = path_join(level_two.cstr, "three");

    if (!mkdir_p(level_three.cstr)) {
        status = fail_message(name, "mkdir_p failed for a nested path");
        goto cleanup;
    }

    status = expect_directory_exists(name, level_one.cstr);
    if (status != 0)
        goto cleanup;

    status = expect_directory_exists(name, level_two.cstr);
    if (status != 0)
        goto cleanup;

    status = expect_directory_exists(name, level_three.cstr);

cleanup:
    if (temp_dir != NULL) {
        int cleanup_status = 0;

        cleanup_status |= cleanup_directory(name, level_three.cstr);
        cleanup_status |= cleanup_directory(name, level_two.cstr);
        cleanup_status |= cleanup_directory(name, level_one.cstr);
        cleanup_status |= cleanup_directory(name, temp_dir);
        if (status == 0)
            status = cleanup_status;
    }
    str_free(level_three);
    str_free(level_two);
    str_free(level_one);
    return status;
}

// Verify that repeated separators do not create empty path components or fail.
static int test_mkdir_p_handles_double_slashes(TestContext *ctx) {
    const char *name = ctx->test_name;
    char template[] = "/tmp/imgneko-path.XXXXXX";
    char *temp_dir = NULL;
    String repeated = str_empty;
    String level_one = str_empty;
    String level_two = str_empty;
    String level_three = str_empty;
    int status = 0;

    temp_dir = mkdtemp(template);
    if (temp_dir == NULL) {
        status = fail_message(name, "mkdtemp failed");
        goto cleanup;
    }

    repeated = path_join(temp_dir, "alpha//beta///gamma//");
    if (!mkdir_p(repeated.cstr)) {
        status =
            fail_message(name, "mkdir_p failed for a path with double slashes");
        goto cleanup;
    }

    level_one = path_join(temp_dir, "alpha");
    level_two = path_join(level_one.cstr, "beta");
    level_three = path_join(level_two.cstr, "gamma");

    status = expect_directory_exists(name, level_one.cstr);
    if (status != 0)
        goto cleanup;

    status = expect_directory_exists(name, level_two.cstr);
    if (status != 0)
        goto cleanup;

    status = expect_directory_exists(name, level_three.cstr);

cleanup:
    if (temp_dir != NULL) {
        int cleanup_status = 0;

        cleanup_status |= cleanup_directory(name, level_three.cstr);
        cleanup_status |= cleanup_directory(name, level_two.cstr);
        cleanup_status |= cleanup_directory(name, level_one.cstr);
        cleanup_status |= cleanup_directory(name, temp_dir);
        if (status == 0)
            status = cleanup_status;
    }
    str_free(level_three);
    str_free(level_two);
    str_free(level_one);
    str_free(repeated);
    return status;
}

// Verify that mkdir_p creates relative paths under the current working
// directory.
static int test_mkdir_p_handles_relative_paths(TestContext *ctx) {
    const char *name = ctx->test_name;
    char template[] = "/tmp/imgneko-path.XXXXXX";
    char cwd[4096];
    char *temp_dir = NULL;
    String saved_cwd = str_empty;
    String level_one = str_empty;
    String level_two = str_empty;
    String level_three = str_empty;
    int status = 0;

    temp_dir = mkdtemp(template);
    if (temp_dir == NULL) {
        status = fail_message(name, "mkdtemp failed");
        goto cleanup;
    }

    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        status = fail_message(name, "getcwd failed");
        goto cleanup;
    }
    saved_cwd = str_from_cstr(cwd);

    if (chdir(temp_dir) != 0) {
        status = fail_message(name, "failed to chdir into the temp directory");
        goto cleanup;
    }

    if (!mkdir_p("a/b/c")) {
        status = fail_message(name, "mkdir_p failed for a relative path");
        goto cleanup;
    }

    level_one = path_join(temp_dir, "a");
    level_two = path_join(level_one.cstr, "b");
    level_three = path_join(level_two.cstr, "c");

    status = expect_directory_exists(name, level_one.cstr);
    if (status != 0)
        goto cleanup;

    status = expect_directory_exists(name, level_two.cstr);
    if (status != 0)
        goto cleanup;

    status = expect_directory_exists(name, level_three.cstr);

cleanup:
    if (saved_cwd.len > 0 && chdir(saved_cwd.cstr) != 0 && status == 0)
        status = fail_message(name, "failed to restore the working directory");
    if (temp_dir != NULL) {
        int cleanup_status = 0;

        cleanup_status |= cleanup_directory(name, level_three.cstr);
        cleanup_status |= cleanup_directory(name, level_two.cstr);
        cleanup_status |= cleanup_directory(name, level_one.cstr);
        cleanup_status |= cleanup_directory(name, temp_dir);
        if (status == 0)
            status = cleanup_status;
    }
    str_free(level_three);
    str_free(level_two);
    str_free(level_one);
    str_free(saved_cwd);
    return status;
}

// Verify that mkdir_p leaves `.` and `..` components to the filesystem's path
// resolution while still creating the final directory tree.
static int test_mkdir_p_handles_dot_components(TestContext *ctx) {
    const char *name = ctx->test_name;
    char template[] = "/tmp/imgneko-path.XXXXXX";
    char cwd[4096];
    char *temp_dir = NULL;
    String saved_cwd = str_empty;
    String work_dir = str_empty;
    String level_one = str_empty;
    String level_two = str_empty;
    String level_three = str_empty;
    int status = 0;

    temp_dir = mkdtemp(template);
    if (temp_dir == NULL) {
        status = fail_message(name, "mkdtemp failed");
        goto cleanup;
    }

    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        status = fail_message(name, "getcwd failed");
        goto cleanup;
    }
    saved_cwd = str_from_cstr(cwd);

    work_dir = path_join(temp_dir, "work");
    if (mkdir(work_dir.cstr, 0755) != 0) {
        status = fail_message(name, "failed to create the working directory");
        goto cleanup;
    }

    if (chdir(work_dir.cstr) != 0) {
        status =
            fail_message(name, "failed to chdir into the working directory");
        goto cleanup;
    }

    if (!mkdir_p("../a/.//b/../b/c")) {
        status = fail_message(name, "mkdir_p failed for . and .. components");
        goto cleanup;
    }

    level_one = path_join(temp_dir, "a");
    level_two = path_join(level_one.cstr, "b");
    level_three = path_join(level_two.cstr, "c");

    status = expect_directory_exists(name, level_one.cstr);
    if (status != 0)
        goto cleanup;

    status = expect_directory_exists(name, level_two.cstr);
    if (status != 0)
        goto cleanup;

    status = expect_directory_exists(name, level_three.cstr);

cleanup:
    if (saved_cwd.len > 0 && chdir(saved_cwd.cstr) != 0 && status == 0)
        status = fail_message(name, "failed to restore the working directory");
    if (temp_dir != NULL) {
        int cleanup_status = 0;

        cleanup_status |= cleanup_directory(name, level_three.cstr);
        cleanup_status |= cleanup_directory(name, level_two.cstr);
        cleanup_status |= cleanup_directory(name, level_one.cstr);
        cleanup_status |= cleanup_directory(name, work_dir.cstr);
        cleanup_status |= cleanup_directory(name, temp_dir);
        if (status == 0)
            status = cleanup_status;
    }
    str_free(level_three);
    str_free(level_two);
    str_free(level_one);
    str_free(work_dir);
    str_free(saved_cwd);
    return status;
}

// Verify that absolute paths with repeated leading separators are treated like
// ordinary absolute paths.
static int test_mkdir_p_handles_multiple_leading_slashes(TestContext *ctx) {
    const char *name = ctx->test_name;
    char template[] = "/tmp/imgneko-path.XXXXXX";
    char *temp_dir = NULL;
    String repeated = str_empty;
    String level_one = str_empty;
    String level_two = str_empty;
    int status = 0;

    temp_dir = mkdtemp(template);
    if (temp_dir == NULL) {
        status = fail_message(name, "mkdtemp failed");
        goto cleanup;
    }

    repeated = str_from_cstr("//");
    str_append_cstr(repeated, temp_dir + 1);
    path_append(&repeated, "alpha//beta");

    if (!mkdir_p(repeated.cstr)) {
        status =
            fail_message(name, "mkdir_p failed for a path with leading //");
        goto cleanup;
    }

    level_one = path_join(temp_dir, "alpha");
    level_two = path_join(level_one.cstr, "beta");

    status = expect_directory_exists(name, level_one.cstr);
    if (status != 0)
        goto cleanup;

    status = expect_directory_exists(name, level_two.cstr);

cleanup:
    if (temp_dir != NULL) {
        int cleanup_status = 0;

        cleanup_status |= cleanup_directory(name, level_two.cstr);
        cleanup_status |= cleanup_directory(name, level_one.cstr);
        cleanup_status |= cleanup_directory(name, temp_dir);
        if (status == 0)
            status = cleanup_status;
    }
    str_free(level_two);
    str_free(level_one);
    str_free(repeated);
    return status;
}

// Verify that mkdir_p rejects the empty path and reports ENOENT.
static int test_mkdir_p_rejects_empty_path(TestContext *ctx) {
    const char *name = ctx->test_name;

    errno = 0;
    if (mkdir_p(""))
        return fail_message(name,
                            "mkdir_p unexpectedly accepted an empty path");
    if (errno != ENOENT)
        return fail_message(name,
                            "mkdir_p did not report ENOENT for empty path");

    return 0;
}

// Verify that mkdir_p accepts the root path without trying to create extra
// components.
static int test_mkdir_p_accepts_root(TestContext *ctx) {
    const char *name = ctx->test_name;

    if (!mkdir_p("/"))
        return fail_message(name, "mkdir_p failed for the root path");

    return 0;
}

// Verify that mkdir_p reports the failing filesystem errno when an
// intermediate path component is a non-directory.
static int test_mkdir_p_preserves_errno_on_failure(TestContext *ctx) {
    const char *name = ctx->test_name;
    char template[] = "/tmp/imgneko-path.XXXXXX";
    char *temp_dir = NULL;
    String file_path = str_empty;
    String child_path = str_empty;
    FILE *stream = NULL;
    int status = 0;

    temp_dir = mkdtemp(template);
    if (temp_dir == NULL) {
        status = fail_message(name, "mkdtemp failed");
        goto cleanup;
    }

    file_path = path_join(temp_dir, "file");
    stream = fopen(file_path.cstr, "w");
    if (stream == NULL) {
        status = fail_message(name, "failed to create a blocking file");
        goto cleanup;
    }
    fclose(stream);
    stream = NULL;

    child_path = path_join(file_path.cstr, "child");
    errno = 0;
    if (mkdir_p(child_path.cstr)) {
        status =
            fail_message(name, "mkdir_p unexpectedly succeeded through a file");
        goto cleanup;
    }
    if (errno != ENOTDIR) {
        status = fail_message(name, "mkdir_p did not preserve ENOTDIR");
        goto cleanup;
    }

cleanup:
    if (stream != NULL)
        fclose(stream);
    if (file_path.len > 0 && unlink(file_path.cstr) != 0 && status == 0)
        status = fail_message(name, "failed to remove the blocking file");
    if (temp_dir != NULL && rmdir(temp_dir) != 0 && status == 0)
        status = fail_message(name, "failed to remove the temp directory");
    str_free(child_path);
    str_free(file_path);
    return status;
}

int main(int argc, char **argv) {
    const Subtest subtests[] = {
        PREFIXED_TEST(test_append_segment),
        PREFIXED_TEST(test_join_segments),
        PREFIXED_TEST(test_trim_trailing_slashes),
        PREFIXED_TEST(test_resolve_absolute),
        PREFIXED_TEST(test_mkdir_p_creates_nested_directories),
        PREFIXED_TEST(test_mkdir_p_handles_double_slashes),
        PREFIXED_TEST(test_mkdir_p_handles_relative_paths),
        PREFIXED_TEST(test_mkdir_p_handles_dot_components),
        PREFIXED_TEST(test_mkdir_p_handles_multiple_leading_slashes),
        PREFIXED_TEST(test_mkdir_p_rejects_empty_path),
        PREFIXED_TEST(test_mkdir_p_accepts_root),
        PREFIXED_TEST(test_mkdir_p_preserves_errno_on_failure),
    };

    return run_subtests(argc, argv, subtests, ARRAY_SIZE(subtests));
}
