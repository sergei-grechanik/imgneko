// SPDX-License-Identifier: MIT-0

// Enable POSIX APIs used in this file (mkstemp).
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "test_main.h"
#include "util/common.h"
#include "util/io.h"
#include "util/string.h"

#define STR(text) (text), (sizeof(text) - 1)

// Print a failure message for a subtest and return a failing status code.
static int fail_message(const char *subtest, const char *message) {
    fprintf(stderr, "%s: %s\n", subtest, message);
    return 1;
}

// Create a temporary output file. The caller closes the returned fd and unlinks
// the returned path.
static int create_temp_file(const char *subtest, String *path_out) {
    char template[] = "/tmp/imgneko-buffered-writer.XXXXXX";
    int fd = mkstemp(template);

    if (fd < 0) {
        fprintf(stderr, "%s: mkstemp failed: %s\n", subtest, strerror(errno));
        return -1;
    }

    str_free(*path_out);
    *path_out = str_from_cstr(template);
    return fd;
}

// Verify that the complete contents of an already-open temporary file match the
// expected byte string.
static int expect_file_content(const char *subtest, int fd,
                               const char *expected, size_t expected_len) {
    char buffer[4096];
    String actual = str_empty;
    int status = 0;

    if (lseek(fd, 0, SEEK_SET) < 0) {
        fprintf(stderr, "%s: lseek failed: %s\n", subtest, strerror(errno));
        return 1;
    }

    while (true) {
        ssize_t nread = read(fd, buffer, sizeof(buffer));

        if (nread < 0) {
            fprintf(stderr, "%s: read failed: %s\n", subtest, strerror(errno));
            status = 1;
            goto cleanup;
        }
        if (nread == 0)
            break;

        str_append_data(actual, buffer, (size_t)nread);
    }

    if (actual.len != expected_len) {
        fprintf(stderr, "%s: expected length %zu, got %zu\n", subtest,
                expected_len, actual.len);
        status = 1;
        goto cleanup;
    }

    if (memcmp(actual.cstr, expected, expected_len) != 0)
        status = fail_message(subtest, "file contents differ");

cleanup:
    str_free(actual);
    return status;
}

// Verify normal buffered writes, explicit flushing, printf formatting, empty
// flushes, zero-byte writes, and make-room flush behavior.
static int test_buffer_flush_and_printf(TestContext *ctx) {
    const char *name = ctx->test_name;
    String path = str_empty;
    BufferedWriter writer = {0};
    int fd = -1;
    int status = 0;

    fd = create_temp_file(name, &path);
    if (fd < 0) {
        status = 1;
        goto cleanup;
    }

    writer = buffered_writer_for_fd(fd);
    if (writer.fd != fd || writer.len != 0 || writer.buffer == NULL) {
        status = fail_message(name, "writer did not initialize correctly");
        goto cleanup;
    }

    printf("Chunk size for a file: %zu\n", writer.capacity);

    buffered_writer_flush(&writer);
    buffered_writer_write(&writer, STR("alpha"));
    if (writer.len != strlen("alpha")) {
        status = fail_message(name, "write was not buffered");
        goto cleanup;
    }

    buffered_writer_make_room(&writer, writer.capacity - writer.len);
    if (writer.len != strlen("alpha")) {
        status = fail_message(name, "make_room flushed despite enough room");
        goto cleanup;
    }

    buffered_writer_make_room(&writer, writer.capacity - writer.len + 1);
    if (writer.len != 0) {
        status = fail_message(name, "make_room did not flush");
        goto cleanup;
    }

    buffered_writer_write(&writer, "", 0);
    buffered_writer_write(&writer, STR("beta"));
    buffered_writer_printf(&writer, "-%d", 42);
    buffered_writer_flush(&writer);

    status = expect_file_content(name, fd, STR("alphabeta-42"));

cleanup:
    buffered_writer_free(&writer);
    if (fd >= 0)
        close(fd);
    if (path.len != 0)
        unlink(path.cstr);
    str_free(path);
    return status;
}

// Verify that chunks larger than the internal buffer flush existing data and
// are written directly, while later smaller chunks are buffered again.
static int test_large_write(TestContext *ctx) {
    const char *name = ctx->test_name;
    String path = str_empty;
    String large = str_empty;
    String expected = str_empty;
    BufferedWriter writer = {0};
    int fd = -1;
    int status = 0;

    fd = create_temp_file(name, &path);
    if (fd < 0) {
        status = 1;
        goto cleanup;
    }

    writer = buffered_writer_for_fd(fd);
    for (size_t i = 0; i < writer.capacity + 17; ++i)
        str_push(large, (char)('a' + (i % 26)));

    buffered_writer_write(&writer, STR("head:"));
    buffered_writer_write(&writer, large.cstr, large.len);
    if (writer.len != 0) {
        status = fail_message(name, "large write was unexpectedly buffered");
        goto cleanup;
    }
    buffered_writer_write(&writer, STR(":tail"));
    buffered_writer_flush(&writer);

    str_append_cstr(expected, "head:");
    str_append_str(expected, large);
    str_append_cstr(expected, ":tail");
    status = expect_file_content(name, fd, expected.cstr, expected.len);

cleanup:
    buffered_writer_free(&writer);
    if (fd >= 0)
        close(fd);
    if (path.len != 0)
        unlink(path.cstr);
    str_free(expected);
    str_free(large);
    str_free(path);
    return status;
}

// Verify that formatted output larger than the remaining buffer space is
// emitted correctly and leaves the writer ready for more buffered writes.
static int test_large_printf(TestContext *ctx) {
    const char *name = ctx->test_name;
    String path = str_empty;
    String large = str_empty;
    String expected = str_empty;
    BufferedWriter writer = {0};
    int fd = -1;
    int status = 0;

    fd = create_temp_file(name, &path);
    if (fd < 0) {
        status = 1;
        goto cleanup;
    }

    writer = buffered_writer_for_fd(fd);
    for (size_t i = 0; i < writer.capacity + 17; ++i)
        str_push(large, (char)('0' + (i % 10)));

    buffered_writer_write(&writer, STR("prefix:"));
    buffered_writer_printf(&writer, "%s", large.cstr);
    if (writer.len != 0) {
        status =
            fail_message(name, "large formatted output was unexpectedly kept");
        goto cleanup;
    }
    buffered_writer_printf(&writer, ":%s", "suffix");
    buffered_writer_flush(&writer);

    str_append_cstr(expected, "prefix:");
    str_append_str(expected, large);
    str_append_cstr(expected, ":suffix");
    status = expect_file_content(name, fd, expected.cstr, expected.len);

cleanup:
    buffered_writer_free(&writer);
    if (fd >= 0)
        close(fd);
    if (path.len != 0)
        unlink(path.cstr);
    str_free(expected);
    str_free(large);
    str_free(path);
    return status;
}

// Verify that the writer falls back to a constant buffer size when fpathconf()
// cannot report a descriptor-specific value.
static int test_capacity_fallback(TestContext *ctx) {
    const char *name = ctx->test_name;
    BufferedWriter writer = buffered_writer_for_fd(-1);
    int status = 0;

    if (writer.capacity != BUFFERED_WRITER_FALLBACK_CAPACITY)
        status = fail_message(name, "writer did not use fallback capacity");

    buffered_writer_free(&writer);
    return status;
}

int main(int argc, char **argv) {
    const Subtest subtests[] = {
        PREFIXED_TEST(test_buffer_flush_and_printf),
        PREFIXED_TEST(test_capacity_fallback),
        PREFIXED_TEST(test_large_write),
        PREFIXED_TEST(test_large_printf),
    };

    return run_subtests(argc, argv, subtests, ARRAY_SIZE(subtests));
}
