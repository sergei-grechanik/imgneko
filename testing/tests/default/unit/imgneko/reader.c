// SPDX-License-Identifier: MIT-0

// Enable POSIX APIs used by these tests.
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "imgneko/reader.h"
#include "test_main.h"
#include "test_reader.h"
#include "util/common.h"
#include "util/string.h"

#define STR(text) (text), (sizeof(text) - 1)

// Write the full byte span to a descriptor used by a test fixture.
static int write_full(const TestContext *ctx, int fd, const char *data,
                      size_t len) {
    for (size_t offset = 0; offset < len;) {
        ssize_t written = write(fd, data + offset, len - offset);

        if (written < 0) {
            fprintf(stderr, "%s: write failed: %s\n", ctx->test_name,
                    strerror(errno));
            return 1;
        }
        if (written == 0)
            return test_fail_message(ctx, "write returned zero");

        offset += (size_t)written;
    }

    return 0;
}

// Verify reader statuses have non-empty, distinguishable diagnostics and an
// unknown value has a distinct usable fallback for failure reporting.
static int test_reader_status_strings(TestContext *ctx) {
    static const ImgnekoReaderStatus statuses[] = {
        IMGNEKO_READER_OK,
        IMGNEKO_READER_EOF,
        IMGNEKO_READER_BUFFER_TOO_SMALL,
        IMGNEKO_READER_ERROR,
        IMGNEKO_READER_WORKSPACE_TOO_SMALL,
    };
    const char *status_strings[ARRAY_SIZE(statuses)] = {0};
    const char *unknown_status = NULL;

    for (size_t i = 0; i < ARRAY_SIZE(statuses); ++i) {
        status_strings[i] = imgneko_reader_status_string(statuses[i]);

        if (status_strings[i] == NULL || status_strings[i][0] == '\0') {
            return test_fail_message(ctx,
                                     "reader status has an empty diagnostic");
        }
        for (size_t j = 0; j < i; ++j) {
            if (strcmp(status_strings[i], status_strings[j]) == 0) {
                return test_fail_message(
                    ctx, "reader statuses share the same diagnostic");
            }
        }
    }

    unknown_status = imgneko_reader_status_string((ImgnekoReaderStatus)99);
    if (unknown_status == NULL || unknown_status[0] == '\0') {
        return test_fail_message(ctx,
                                 "unknown reader status lacks a diagnostic");
    }
    for (size_t i = 0; i < ARRAY_SIZE(status_strings); ++i) {
        if (strcmp(unknown_status, status_strings[i]) == 0) {
            return test_fail_message(
                ctx, "unknown reader status matches a known diagnostic");
        }
    }

    return 0;
}

// Verify memory reader chunking, zero-capacity retry sizing, and sticky EOF.
static int test_memory_reader(TestContext *ctx) {
    ImgnekoMemoryReader memory = {0};
    ImgnekoReader reader;
    char out[4];
    size_t len = 0;
    int status;

    imgneko_memory_reader_init(&memory, STR("abcdef"));
    reader = imgneko_memory_reader_as_reader(&memory);

    status = imgneko_reader_read(reader, NULL, 0, &len);
    if (test_expect_status(ctx, status, IMGNEKO_READER_BUFFER_TOO_SMALL,
                           "zero-capacity read") ||
        test_expect_size(ctx, len, 1, "zero-capacity retry size"))
        return 1;

    status = imgneko_reader_read(reader, out, 2, &len);
    if (test_expect_status(ctx, status, IMGNEKO_READER_OK, "first read") ||
        test_expect_data(ctx, out, len, STR("ab"), "first read data"))
        return 1;

    status = imgneko_reader_read(reader, out, 3, &len);
    if (test_expect_status(ctx, status, IMGNEKO_READER_OK, "second read") ||
        test_expect_data(ctx, out, len, STR("cde"), "second read data"))
        return 1;

    status = imgneko_reader_read(reader, out, sizeof(out), &len);
    if (test_expect_status(ctx, status, IMGNEKO_READER_OK, "third read") ||
        test_expect_data(ctx, out, len, STR("f"), "third read data"))
        return 1;

    status = imgneko_reader_read(reader, out, sizeof(out), &len);
    if (test_expect_status(ctx, status, IMGNEKO_READER_EOF, "first EOF"))
        return 1;

    status = imgneko_reader_read(reader, out, sizeof(out), &len);
    return test_expect_status(ctx, status, IMGNEKO_READER_EOF, "sticky EOF");
}

// Verify public reader wrapper validation for caller-visible errors.
static int test_reader_validation(TestContext *ctx) {
    ImgnekoMemoryReader memory = {0};
    ImgnekoReader reader;
    char out[4];
    size_t len = 123;
    int status;

    status = imgneko_reader_read((ImgnekoReader){0}, out, sizeof(out), &len);
    if (test_expect_status(ctx, status, IMGNEKO_READER_ERROR,
                           "missing callback") ||
        test_expect_size(ctx, len, 0, "missing callback length"))
        return 1;

    imgneko_memory_reader_init(&memory, STR("abc"));
    reader = imgneko_memory_reader_as_reader(&memory);

    status = imgneko_reader_read(reader, NULL, 1, &len);
    if (test_expect_status(ctx, status, IMGNEKO_READER_ERROR,
                           "missing non-empty output"))
        return 1;

    status = imgneko_reader_read(reader, out, sizeof(out), NULL);
    if (test_expect_status(ctx, status, IMGNEKO_READER_ERROR,
                           "missing len_out"))
        return 1;

    return 0;
}

// Verify reader initialization and open failures that should leave readers
// inert instead of producing usable streams.
static int test_reader_init_failures(TestContext *ctx) {
    char template[] = "/tmp/imgneko-reader-missing.XXXXXX";
    ImgnekoFileReader file_reader = {0};
    ImgnekoReader reader;
    int fd = -1;
    int status;

    status = imgneko_file_reader_init(&file_reader, NULL);
    if (test_expect_status(ctx, status, -1, "null file reader path"))
        return 1;

    fd = mkstemp(template);
    if (fd < 0) {
        fprintf(stderr, "%s: mkstemp failed: %s\n", ctx->test_name,
                strerror(errno));
        return 1;
    }
    close(fd);
    fd = -1;
    unlink(template);

    status = imgneko_file_reader_init(&file_reader, template);
    if (test_expect_status(ctx, status, -1, "missing file reader path"))
        return 1;

    reader = imgneko_file_reader_as_reader(NULL);
    if (reader.read != imgneko_fd_reader_func || reader.ctx != NULL)
        return test_fail_message(ctx, "null file reader view is malformed");

    return 0;
}

// Verify that fd readers do not own the descriptor and make EOF sticky.
static int test_fd_reader(TestContext *ctx) {
    int fds[2] = {-1, -1};
    ImgnekoFdReader fd_reader = {0};
    ImgnekoReader reader;
    char out[4];
    size_t len = 0;
    int status;
    int result = 0;

    if (pipe(fds) < 0) {
        fprintf(stderr, "%s: pipe failed: %s\n", ctx->test_name,
                strerror(errno));
        return 1;
    }

    if (write_full(ctx, fds[1], STR("wxyz")) != 0) {
        result = 1;
        goto cleanup;
    }
    close(fds[1]);
    fds[1] = -1;

    imgneko_fd_reader_init(&fd_reader, fds[0]);
    reader = imgneko_fd_reader_as_reader(&fd_reader);

    status = imgneko_reader_read(reader, NULL, 0, &len);
    if (test_expect_status(ctx, status, IMGNEKO_READER_BUFFER_TOO_SMALL,
                           "fd zero-capacity read") ||
        test_expect_size(ctx, len, 1, "fd zero-capacity retry size")) {
        result = 1;
        goto cleanup;
    }

    status = imgneko_reader_read(reader, out, 3, &len);
    if (test_expect_status(ctx, status, IMGNEKO_READER_OK, "fd first read") ||
        test_expect_data(ctx, out, len, STR("wxy"), "fd first read data")) {
        result = 1;
        goto cleanup;
    }

    status = imgneko_reader_read(reader, out, sizeof(out), &len);
    if (test_expect_status(ctx, status, IMGNEKO_READER_OK, "fd second read") ||
        test_expect_data(ctx, out, len, STR("z"), "fd second read data")) {
        result = 1;
        goto cleanup;
    }

    status = imgneko_reader_read(reader, out, sizeof(out), &len);
    if (test_expect_status(ctx, status, IMGNEKO_READER_EOF, "fd first EOF")) {
        result = 1;
        goto cleanup;
    }

    status = imgneko_reader_read(reader, out, sizeof(out), &len);
    result =
        test_expect_status(ctx, status, IMGNEKO_READER_EOF, "fd sticky EOF");

cleanup:
    if (fds[0] >= 0)
        close(fds[0]);
    if (fds[1] >= 0)
        close(fds[1]);
    return result;
}

// Verify that a read error from the underlying descriptor is reported as a
// reader error.
static int test_fd_reader_read_error(TestContext *ctx) {
    int fds[2] = {-1, -1};
    ImgnekoFdReader fd_reader = {0};
    ImgnekoReader reader;
    char out[1];
    size_t len = 0;
    int status;

    if (pipe(fds) < 0) {
        fprintf(stderr, "%s: pipe failed: %s\n", ctx->test_name,
                strerror(errno));
        return 1;
    }

    int closed_fd = fds[0];
    close(fds[0]);
    fds[0] = -1;
    close(fds[1]);
    fds[1] = -1;

    imgneko_fd_reader_init(&fd_reader, closed_fd);
    reader = imgneko_fd_reader_as_reader(&fd_reader);

    status = imgneko_reader_read(reader, out, sizeof(out), &len);
    return test_expect_status(ctx, status, IMGNEKO_READER_ERROR,
                              "closed fd read");
}

// Verify file reader open/deinit ownership around a path-backed reader.
static int test_file_reader(TestContext *ctx) {
    char template[] = "/tmp/imgneko-reader.XXXXXX";
    int fd = -1;
    ImgnekoFileReader file_reader = {0};
    ImgnekoReader reader;
    String output = str_empty;
    char out[4];
    size_t len = 0;
    int status;
    int result = 0;

    fd = mkstemp(template);
    if (fd < 0) {
        fprintf(stderr, "%s: mkstemp failed: %s\n", ctx->test_name,
                strerror(errno));
        return 1;
    }

    if (write_full(ctx, fd, STR("file-data")) != 0) {
        result = 1;
        goto cleanup;
    }
    close(fd);
    fd = -1;

    if (imgneko_file_reader_init(&file_reader, template) != 0) {
        result = test_fail_message(ctx, "file reader init failed");
        goto cleanup;
    }

    reader = imgneko_file_reader_as_reader(&file_reader);
    if (test_drain_reader(ctx, reader, 4, &output) != 0) {
        result = 1;
        goto cleanup;
    }

    if (test_expect_data(ctx, output.cstr, output.len, STR("file-data"),
                         "file reader output")) {
        result = 1;
        goto cleanup;
    }

    imgneko_file_reader_deinit(&file_reader);
    status = imgneko_reader_read(reader, out, sizeof(out), &len);
    result = test_expect_status(ctx, status, IMGNEKO_READER_ERROR,
                                "read after deinit");

cleanup:
    if (fd >= 0)
        close(fd);
    if (template[0] != '\0')
        unlink(template);
    imgneko_file_reader_deinit(&file_reader);
    str_free(output);
    return result;
}

int main(int argc, char **argv) {
    const Subtest subtests[] = {
        PREFIXED_TEST(test_reader_status_strings),
        PREFIXED_TEST(test_memory_reader),
        PREFIXED_TEST(test_reader_validation),
        PREFIXED_TEST(test_reader_init_failures),
        PREFIXED_TEST(test_fd_reader),
        PREFIXED_TEST(test_fd_reader_read_error),
        PREFIXED_TEST(test_file_reader),
    };

    return run_subtests(argc, argv, subtests, ARRAY_SIZE(subtests));
}
