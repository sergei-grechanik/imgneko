// SPDX-License-Identifier: MIT-0

// Unit tests for serialization behavior not covered by shared command cases.

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "imgneko/graphics_command.h"
#include "test_graphics_command.h"
#include "test_main.h"
#include "test_reader.h"
#include "util/common.h"
#include "util/string.h"

typedef struct CountingReader {
    size_t read_count;
} CountingReader;

// Record any payload read so the header-only API can be checked for purity.
static ImgnekoReaderStatus
counting_reader_func(void *ctx, char *out, size_t out_cap, size_t *len_out) {
    CountingReader *reader = ctx;

    (void)out;
    (void)out_cap;

    ++reader->read_count;
    *len_out = 0;
    return IMGNEKO_READER_EOF;
}

// Serialize a header and compare its token set without assuming key order.
//
// `ctx`
//     Test context used for diagnostics.
// `command`
//     Command whose control-data fields are serialized.
// `expected`
//     Comma-separated expected tokens in any order.
// `description`
//     Human-readable description of the case.
static int expect_header(TestContext *ctx, const ImgnekoCommand *command,
                         const char *expected, const char *description) {
    char out[2048];
    char message[256];
    size_t len = SIZE_MAX;
    ImgnekoErrorDetail detail = {
        .message = message,
        .message_cap = sizeof(message),
        .message_len = SIZE_MAX,
    };

    memset(out, 'x', sizeof(out));
    memset(message, 'x', sizeof(message));
    ImgnekoCommandError status = imgneko_command_header_to_buffer(
        command, out, sizeof(out), &len, &detail);
    if (status != IMGNEKO_COMMAND_OK) {
        fprintf(stderr, "%s: %s: serialization failed with %d (%.*s)\n",
                ctx->test_name, description, status, (int)sizeof(message),
                message);
        return 1;
    }
    if (detail.message_len != 0 || message[0] != '\0')
        return test_fail_message(ctx, "successful serialization left detail");
    if (len >= sizeof(out))
        return test_fail_message(ctx, "serialized header exceeds test buffer");
    if (out[len] != 'x')
        return test_fail_message(ctx, "serialized header was null-terminated");

    return test_expect_graphics_command_header_tokens(
        ctx, str_span(out, len), str_span_from_cstr(expected), description);
}

// Verify omission flags do not suppress keys when their associated
// discriminator has a value for which the flag has no effect.
static int test_irrelevant_omission_flags(TestContext *ctx) {
    const struct {
        ImgnekoCommand command;
        const char *expected;
        const char *description;
    } cases[] = {
        {
            .command =
                {
                    .kind = IMGNEKO_COMMAND_PUT,
                    .kind_implicit = true,
                    .image_id = 1,
                },
            .expected = "a=p,i=1",
            .description = "irrelevant implicit command kind",
        },
        {
            .command =
                {
                    .kind = IMGNEKO_COMMAND_TRANSMIT,
                    .data.transmit.transmission =
                        {
                            .medium = IMGNEKO_TRANSMISSION_MEDIUM_FILE,
                            .medium_implicit = true,
                        },
                },
            .expected = "a=t,t=f",
            .description = "irrelevant implicit transmission medium",
        },
        {
            .command =
                {
                    .kind = IMGNEKO_COMMAND_DELETE,
                    .data.delete_cmd =
                        {
                            .target = IMGNEKO_DELETE_VISIBLE_PLACEMENTS,
                            .target_implicit = true,
                            .delete_data = true,
                        },
                },
            .expected = "a=d,d=A",
            .description = "irrelevant implicit delete target",
        },
    };

    for (size_t i = 0; i < ARRAY_SIZE(cases); ++i) {
        if (expect_header(ctx, &cases[i].command, cases[i].expected,
                          cases[i].description))
            return 1;
    }

    return 0;
}

// Verify required-size reporting, prefix writes to undersized buffers,
// exact-fit writes, guard bytes, and argument validation.
static int test_buffer_contract(TestContext *ctx) {
    ImgnekoCommand command = {
        .kind = IMGNEKO_COMMAND_PUT,
        .image_id = 7,
        .data.put.placement.cols = 2,
    };
    char reference[128];
    char message[128];
    size_t reference_len = 0;
    ImgnekoErrorDetail detail = {
        .message = message,
        .message_cap = sizeof(message),
        .message_len = SIZE_MAX,
    };

    ImgnekoCommandError status = imgneko_command_header_to_buffer(
        &command, reference, sizeof(reference), &reference_len, &detail);
    if (test_expect_status(ctx, status, IMGNEKO_COMMAND_OK,
                           "reference serialization") ||
        detail.message_len != 0 || message[0] != '\0')
        return 1;

    size_t len = SIZE_MAX;
    status = imgneko_command_header_to_buffer(&command, NULL, 0, &len, &detail);
    char expected_message[128];
    snprintf(expected_message, sizeof(expected_message),
             "header requires %zu bytes, but buffer has 0", reference_len);
    if (test_expect_status(ctx, status, IMGNEKO_COMMAND_BUFFER_TOO_SMALL,
                           "size query") ||
        test_expect_size(ctx, len, reference_len, "size query length") ||
        strcmp(message, expected_message) != 0)
        return 1;

    ImgnekoErrorDetail size_detail = {
        .message = NULL,
        .message_cap = 0,
        .message_len = SIZE_MAX,
    };
    len = SIZE_MAX;
    status =
        imgneko_command_header_to_buffer(&command, NULL, 0, &len, &size_detail);
    if (test_expect_status(ctx, status, IMGNEKO_COMMAND_BUFFER_TOO_SMALL,
                           "size query detail status") ||
        test_expect_size(ctx, len, reference_len,
                         "size query detail required length") ||
        size_detail.message_len == 0)
        return 1;

    char tiny_message[2] = {'x', 'x'};
    ImgnekoErrorDetail tiny_detail = {
        .message = tiny_message,
        .message_cap = sizeof(tiny_message),
    };
    len = SIZE_MAX;
    status =
        imgneko_command_header_to_buffer(&command, NULL, 0, &len, &tiny_detail);
    if (test_expect_status(ctx, status, IMGNEKO_COMMAND_BUFFER_TOO_SMALL,
                           "truncated size-query detail status") ||
        test_expect_size(ctx, tiny_detail.message_len, size_detail.message_len,
                         "truncated size-query detail length") ||
        tiny_message[0] == '\0' || tiny_message[1] != '\0')
        return 1;

    char out[128];
    memset(out, 'x', sizeof(out));
    len = SIZE_MAX;
    status = imgneko_command_header_to_buffer(&command, out, reference_len - 1,
                                              &len, NULL);
    if (test_expect_status(ctx, status, IMGNEKO_COMMAND_BUFFER_TOO_SMALL,
                           "undersized output") ||
        test_expect_size(ctx, len, reference_len,
                         "undersized required length") ||
        test_expect_buffer_output(ctx, out, reference_len - 1, sizeof(out),
                                  reference, reference_len - 1, 'x',
                                  "undersized truncated data"))
        return 1;

    memset(out, 'x', sizeof(out));
    len = SIZE_MAX;
    status = imgneko_command_header_to_buffer(&command, out, reference_len,
                                              &len, NULL);
    if (test_expect_status(ctx, status, IMGNEKO_COMMAND_OK,
                           "exact-fit output") ||
        test_expect_buffer_output(ctx, out, len, sizeof(out), reference,
                                  reference_len, 'x', "exact-fit data"))
        return 1;

    len = SIZE_MAX;
    status = imgneko_command_header_to_buffer(&command, NULL, 1, &len, NULL);
    if (test_expect_status(ctx, status, IMGNEKO_COMMAND_INVALID_ARGUMENT,
                           "missing nonempty output") ||
        test_expect_size(ctx, len, 0, "missing output length"))
        return 1;

    len = SIZE_MAX;
    status =
        imgneko_command_header_to_buffer(NULL, out, sizeof(out), &len, NULL);
    if (test_expect_status(ctx, status, IMGNEKO_COMMAND_INVALID_ARGUMENT,
                           "missing command") ||
        test_expect_size(ctx, len, 0, "missing command length"))
        return 1;

    status = imgneko_command_header_to_buffer(&command, out, sizeof(out), NULL,
                                              NULL);
    if (test_expect_status(ctx, status, IMGNEKO_COMMAND_INVALID_ARGUMENT,
                           "missing length output"))
        return 1;

    ImgnekoErrorDetail malformed_detail = {
        .message = NULL,
        .message_cap = 1,
    };
    len = SIZE_MAX;
    status = imgneko_command_header_to_buffer(&command, out, sizeof(out), &len,
                                              &malformed_detail);
    if (test_expect_status(ctx, status, IMGNEKO_COMMAND_INVALID_ARGUMENT,
                           "missing detail storage") ||
        test_expect_size(ctx, len, 0, "missing detail storage length"))
        return 1;

    return 0;
}

// Verify the documented bound with the command variant and scalar values that
// produce the longest possible control-data header.
static int test_maximum_header_size(TestContext *ctx) {
    ImgnekoCommand command = {
        .kind = IMGNEKO_COMMAND_TRANSMIT_AND_PUT,
        .image_id = UINT32_MAX,
        .image_number = UINT32_MAX,
        .quietness = UINT32_MAX,
        .data.transmit_and_put =
            {
                .transmission =
                    {
                        .medium = IMGNEKO_TRANSMISSION_MEDIUM_DIRECT,
                        .data_size = UINT32_MAX,
                        .data_offset = UINT32_MAX,
                        .more_data = UINT32_MAX,
                        .format = UINT32_MAX,
                        .compression = IMGNEKO_PAYLOAD_COMPRESSION_ZLIB,
                        .pixel_width = UINT32_MAX,
                        .pixel_height = UINT32_MAX,
                        .usage_hints = UINT32_MAX,
                    },
                .placement =
                    {
                        .placement_id = UINT32_MAX,
                        .virtual_placement = UINT32_MAX,
                        .cols = UINT32_MAX,
                        .rows = UINT32_MAX,
                        .do_not_move_cursor = UINT32_MAX,
                        .src_x = INT32_MIN,
                        .src_y = INT32_MIN,
                        .src_w = UINT32_MAX,
                        .src_h = UINT32_MAX,
                        .cell_x_offset = INT32_MIN,
                        .cell_y_offset = INT32_MIN,
                        .z_index = INT32_MIN,
                        .parent_image_id = UINT32_MAX,
                        .parent_placement_id = UINT32_MAX,
                        .parent_x_offset = INT32_MIN,
                        .parent_y_offset = INT32_MIN,
                    },
            },
    };
    char out[IMGNEKO_COMMAND_HEADER_MAX_SIZE + 1];
    memset(out, 'x', sizeof(out));
    size_t len = 0;

    ImgnekoCommandError status = imgneko_command_header_to_buffer(
        &command, out, IMGNEKO_COMMAND_HEADER_MAX_SIZE, &len, NULL);
    if (test_expect_status(ctx, status, IMGNEKO_COMMAND_OK,
                           "maximum header serialization") ||
        test_expect_size(ctx, len, IMGNEKO_COMMAND_HEADER_MAX_SIZE,
                         "maximum header length") ||
        out[IMGNEKO_COMMAND_HEADER_MAX_SIZE] != 'x')
        return 1;

    return 0;
}

// Verify header-only serialization never invokes a direct payload reader.
static int test_header_does_not_read_payload(TestContext *ctx) {
    CountingReader source = {0};
    ImgnekoCommand command = {
        .kind = IMGNEKO_COMMAND_TRANSMIT,
        .image_id = 7,
        .data.transmit.transmission =
            {
                .medium = IMGNEKO_TRANSMISSION_MEDIUM_DIRECT,
                .payload.direct =
                    {
                        .read = counting_reader_func,
                        .ctx = &source,
                    },
            },
    };

    if (expect_header(ctx, &command, "a=t,i=7,t=d",
                      "header with borrowed payload"))
        return 1;

    return test_expect_size(ctx, source.read_count, 0, "header payload reads");
}

int main(int argc, char **argv) {
    const Subtest subtests[] = {
        PREFIXED_TEST(test_irrelevant_omission_flags),
        PREFIXED_TEST(test_buffer_contract),
        PREFIXED_TEST(test_maximum_header_size),
        PREFIXED_TEST(test_header_does_not_read_payload),
    };

    return run_subtests(argc, argv, subtests, ARRAY_SIZE(subtests));
}
