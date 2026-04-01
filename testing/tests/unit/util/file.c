// Enable POSIX APIs used in this file (mkstemp).
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "test_main.h"
#include "util/file.h"

#define STR(text) (text), (sizeof(text) - 1)

// Print one failure message for a subtest and return a failing status code.
static int fail_message(const char *subtest, const char *message) {
    fprintf(stderr, "%s: %s\n", subtest, message);
    return 1;
}

// Compare one StringArray entry against an expected string.
static int expect_line_eq(const char *subtest, const StringArray *lines,
                          size_t index, const char *expected,
                          size_t expected_len) {
    if (index >= lines->size) {
        fprintf(stderr, "%s: missing line %zu\n", subtest, index);
        return 1;
    }

    if (lines->data[index].len != expected_len) {
        fprintf(stderr, "%s: line %zu expected length %zu, got %zu\n", subtest,
                index, expected_len, lines->data[index].len);
        return 1;
    }

    if (memcmp(lines->data[index].cstr, expected, expected_len + 1) != 0) {
        fprintf(stderr, "%s: line %zu contents differ\n", subtest, index);
        return 1;
    }

    return 0;
}

// Create a temporary file containing `contents`. The caller owns the returned
// path and frees it with str_free after unlinking it.
static String write_temp_file(const char *subtest, const char *contents) {
    char template[] = "/tmp/imgneko-file-lines.XXXXXX";
    int fd = mkstemp(template);
    String path = str_empty;
    size_t len = strlen(contents);
    size_t written = 0;

    if (fd < 0) {
        fprintf(stderr, "%s: mkstemp failed: %s\n", subtest, strerror(errno));
        return path;
    }

    while (written < len) {
        ssize_t nwrite = write(fd, contents + written, len - written);

        if (nwrite < 0) {
            fprintf(stderr, "%s: write failed: %s\n", subtest, strerror(errno));
            close(fd);
            unlink(template);
            return path;
        }

        written += (size_t)nwrite;
    }

    close(fd);
    path = str_from_cstr(template);
    return path;
}

static int test_read_all_lines(TestContext *ctx) {
    const char *name = ctx->test_name;
    String path = str_empty;
    StringArray lines = arr_empty;
    int status = 0;

    path = write_temp_file(name, "alpha\nbeta\r\ngamma");
    if (path.len == 0) {
        status = 1;
        goto cleanup;
    }

    if (!file_read_lines(&lines, path.cstr, -1)) {
        status = fail_message(name, "file_read_lines failed");
        goto cleanup;
    }

    if (lines.size != 3) {
        status = fail_message(name, "expected three lines");
        goto cleanup;
    }

    status = expect_line_eq(name, &lines, 0, STR("alpha\n"));
    if (status != 0)
        goto cleanup;
    status = expect_line_eq(name, &lines, 1, STR("beta\r\n"));
    if (status != 0)
        goto cleanup;
    status = expect_line_eq(name, &lines, 2, STR("gamma"));

cleanup:
    str_array_free(&lines);
    if (path.len != 0)
        unlink(path.cstr);
    str_free(path);
    return status;
}

static int test_read_tail_lines(TestContext *ctx) {
    const char *name = ctx->test_name;
    String path = str_empty;
    StringArray lines = arr_empty;
    int status = 0;

    path = write_temp_file(name, "one\ntwo\nthree\nfour\n");
    if (path.len == 0) {
        status = 1;
        goto cleanup;
    }

    if (!file_read_lines(&lines, path.cstr, 2)) {
        status = fail_message(name, "file_read_lines failed");
        goto cleanup;
    }

    if (lines.size != 2) {
        status = fail_message(name, "expected two tail lines");
        goto cleanup;
    }

    status = expect_line_eq(name, &lines, 0, STR("three\n"));
    if (status != 0)
        goto cleanup;
    status = expect_line_eq(name, &lines, 1, STR("four\n"));

cleanup:
    str_array_free(&lines);
    if (path.len != 0)
        unlink(path.cstr);
    str_free(path);
    return status;
}

static int test_read_stream_lines(TestContext *ctx) {
    const char *name = ctx->test_name;
    String path = str_empty;
    StringArray lines = arr_empty;
    FILE *stream = NULL;
    int status = 0;

    path = write_temp_file(name, "zero\none\ntwo\n");
    if (path.len == 0) {
        status = 1;
        goto cleanup;
    }

    stream = fopen(path.cstr, "r");
    if (stream == NULL) {
        fprintf(stderr, "%s: fopen failed: %s\n", name, strerror(errno));
        status = 1;
        goto cleanup;
    }

    if (!file_read_stream_lines(&lines, stream, 2)) {
        status = fail_message(name, "file_read_stream_lines failed");
        goto cleanup;
    }

    if (lines.size != 2) {
        status = fail_message(name, "expected two tail lines from stream");
        goto cleanup;
    }

    status = expect_line_eq(name, &lines, 0, STR("one\n"));
    if (status != 0)
        goto cleanup;
    status = expect_line_eq(name, &lines, 1, STR("two\n"));

cleanup:
    if (stream != NULL)
        fclose(stream);
    str_array_free(&lines);
    if (path.len != 0)
        unlink(path.cstr);
    str_free(path);
    return status;
}

static int test_read_zero_tail_lines(TestContext *ctx) {
    const char *name = ctx->test_name;
    String path = str_empty;
    StringArray lines = arr_empty;
    int status = 0;

    path = write_temp_file(name, "one\ntwo\n");
    if (path.len == 0) {
        status = 1;
        goto cleanup;
    }

    if (!file_read_lines(&lines, path.cstr, 0)) {
        status = fail_message(name, "file_read_lines failed");
        goto cleanup;
    }

    if (lines.size != 0)
        status = fail_message(name, "expected zero lines");

cleanup:
    str_array_free(&lines);
    if (path.len != 0)
        unlink(path.cstr);
    str_free(path);
    return status;
}

static int test_missing_file(TestContext *ctx) {
    const char *name = ctx->test_name;
    StringArray lines = arr_empty;
    bool ok = file_read_lines(&lines, "/tmp/imgneko-definitely-missing", -1);

    str_array_free(&lines);
    if (ok)
        return fail_message(name, "missing-file read unexpectedly succeeded");
    if (errno != ENOENT)
        return fail_message(name, "missing-file read set the wrong errno");
    return 0;
}

int main(int argc, char **argv) {
    const Subtest subtests[] = {
        PREFIXED_TEST(test_read_all_lines),
        PREFIXED_TEST(test_read_tail_lines),
        PREFIXED_TEST(test_read_stream_lines),
        PREFIXED_TEST(test_read_zero_tail_lines),
        PREFIXED_TEST(test_missing_file),
    };

    return run_subtests(argc, argv, subtests, ARRAY_SIZE(subtests));
}
