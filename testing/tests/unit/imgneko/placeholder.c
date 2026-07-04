// SPDX-License-Identifier: MIT-0

#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "imgneko/placeholder.h"
#include "imgneko/rowcolumn_diacritics.h"
#include "test_main.h"
#include "util/common.h"

#define RESET "\x1b[0m"
#define RESET_LEN 4
#define PLACE PLACEHOLDER_UTF8
#define END_CHUNK (&chunk_boundary_marker)

static const char chunk_boundary_marker = '\0';

// Captures bytes written through an ImgnekoWriter and can simulate writer
// failures.
typedef struct Capture {
    char data[8192];
    // Number of bytes currently stored in `data`.
    size_t len;
    // Number of times the writer callback has been called.
    size_t call_count;
    // End offsets for each successful writer callback.
    size_t chunk_ends[256];
    // Number of recorded chunk end offsets.
    size_t chunk_count;
    // First call number that should fail with -1. Zero disables this failure.
    size_t fail_after_calls;
} Capture;

// State for a dynamic formatting callback that can be switched to failure mode.
typedef struct DynamicFormatContext {
    bool fail;
} DynamicFormatContext;

// State for a formatting callback that fills exactly `len` bytes.
typedef struct FixedLengthFormatContext {
    size_t len;
} FixedLengthFormatContext;

// State for a positioner callback that returns configured data or failures for
// selected position flags.
typedef struct ConfiguredPositionerContext {
    PlaceholderPositionFlags fail_flags;
    int return_len;
} ConfiguredPositionerContext;

// State for a writer that counts rendered bytes without retaining them.
typedef struct CountingWriter {
    size_t len;
} CountingWriter;

// ImgnekoWriter callback that records full write spans and exercises writer
// failure paths.
static int capture_writer(void *ctx, const char *data, size_t len) {
    Capture *capture = ctx;

    ++capture->call_count;
    if (capture->fail_after_calls != 0 &&
        capture->call_count >= capture->fail_after_calls)
        return -1;

    if (capture->len + len > sizeof(capture->data))
        return -1;
    if (capture->chunk_count >= ARRAY_SIZE(capture->chunk_ends))
        return -1;

    if (len != 0)
        memcpy(capture->data + capture->len, data, len);
    capture->len += len;
    capture->chunk_ends[capture->chunk_count] = capture->len;
    ++capture->chunk_count;
    return 0;
}

// Return an ImgnekoWriter writing to a Capture object.
static ImgnekoWriter capture_as_writer(Capture *capture) {
    return (ImgnekoWriter){
        .write = capture_writer,
        .ctx = capture,
    };
}

// ImgnekoWriter callback that only accumulates the number of bytes written.
static int counting_writer(void *ctx, const char *data, size_t len) {
    CountingWriter *counter = ctx;

    (void)data;

    counter->len += len;
    return 0;
}

// Return an ImgnekoWriter that counts bytes in a CountingWriter object.
static ImgnekoWriter counting_as_writer(CountingWriter *counter) {
    return (ImgnekoWriter){
        .write = counting_writer,
        .ctx = counter,
    };
}

// Formatting callback that derives a background color from the current cell.
static int dynamic_format(void *ctx, const Placeholder *placeholder,
                          uint32_t col, uint32_t row, char *out,
                          size_t out_cap) {
    DynamicFormatContext *format = ctx;
    int prefix_len;
    size_t required_len;

    (void)placeholder;

    if (format != NULL && format->fail)
        return -1;

    // snprintf appends a null terminator (which we don't need, so we replace it
    // with `m` afterwards) and reports the length of the string without the
    // terminator.
    prefix_len =
        snprintf(out, out_cap, "\033[48;5;%u", (unsigned)((col + row) & 0xFFu));
    if (prefix_len < 0)
        return -1;
    required_len = (size_t)prefix_len + 1;
    if (required_len > out_cap)
        return (int)required_len;

    out[prefix_len] = 'm';
    return (int)required_len;
}

// Formatting callback that always fails.
static int always_failing_format(void *ctx, const Placeholder *placeholder,
                                 uint32_t col, uint32_t row, char *out,
                                 size_t out_cap) {
    (void)ctx;
    (void)placeholder;
    (void)col;
    (void)row;
    (void)out;
    (void)out_cap;
    return -1;
}

// Formatting callback that fills a caller-selected number of bytes. When the
// output does not fit, it returns the required length so the renderer can flush
// and retry with more space.
static int fixed_length_format(void *ctx, const Placeholder *placeholder,
                               uint32_t col, uint32_t row, char *out,
                               size_t out_cap) {
    FixedLengthFormatContext *format = ctx;

    (void)placeholder;
    (void)col;
    (void)row;

    if (format->len > (size_t)INT_MAX)
        return -1;
    if (format->len > out_cap)
        return (int)format->len;

    memset(out, 'x', format->len);
    return (int)format->len;
}

// Positioner callback that emits visibly different line-start and line-end
// markers so tests can verify callback wiring.
static int custom_positioner(void *ctx, const Placeholder *placeholder,
                             uint32_t row, PlaceholderPositionFlags flags,
                             char *out, size_t out_cap) {
    const char *fmt = NULL;
    char final_byte = '\0';

    (void)ctx;
    (void)placeholder;

    if ((flags & PLACEHOLDER_POSITION_LINE_START) != 0) {
        fmt = "<%u:%u";
        final_byte = '>';
    } else if ((flags & PLACEHOLDER_POSITION_LINE_END) != 0) {
        fmt = "[%u:%u";
        final_byte = ']';
    } else {
        return 0;
    }

    int prefix_len =
        snprintf(out, out_cap, fmt, (unsigned)row, (unsigned)flags);
    if (prefix_len < 0)
        return -1;

    // snprintf appends a null terminator that we don't need, so replace it with
    // the omitted final byte when the output fits exactly.
    size_t required_len = (size_t)prefix_len + 1;
    if (required_len > out_cap)
        return (int)required_len;

    out[prefix_len] = final_byte;
    return (int)required_len;
}

// Positioner callback that returns a configured value for selected flags. This
// lets tests cover line-start, line-end, and oversized-positioner failures.
static int configured_positioner(void *ctx, const Placeholder *placeholder,
                                 uint32_t row, PlaceholderPositionFlags flags,
                                 char *out, size_t out_cap) {
    ConfiguredPositionerContext *positioner = ctx;

    (void)placeholder;
    (void)row;

    if ((flags & positioner->fail_flags) != 0) {
        if (positioner->return_len < 0)
            return positioner->return_len;
        if ((size_t)positioner->return_len > out_cap)
            return positioner->return_len;
        memset(out, 'p', (size_t)positioner->return_len);
        return positioner->return_len;
    }
    return 0;
}

// Return the common two-by-two placeholder used by most tests.
static Placeholder base_placeholder(void) {
    return (Placeholder){
        .image_id = 0x06020304,
        .placement_id = 0x050607,
        .rect = {.start_col = 0, .start_row = 0, .end_col = 2, .end_row = 2},
    };
}

// Fill a static formatting string with a repeated byte and a null terminator.
static void fill_static_format(char *out, size_t len) {
    memset(out, 'x', len);
    out[len] = '\0';
}

// Require every linefeed in rendered output to be immediately preceded by the
// ANSI reset sequence.
static int expect_newlines_preceded_by_reset(TestContext *ctx,
                                             const char *output, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        if (output[i] != '\n')
            continue;
        if (i < RESET_LEN ||
            memcmp(output + i - RESET_LEN, RESET, RESET_LEN) != 0) {
            fprintf(stderr, "%s: newline at %zu was not preceded by reset\n",
                    ctx->test_name, i);
            return 1;
        }
    }

    return 0;
}

// Require a placeholder operation to return the expected error code.
//
// Parameters:
// `ctx`
//     Current test context for diagnostics.
// `actual`
//     Error returned by the operation under test.
// `expected`
//     Error the operation should return.
// `what`
//     Short diagnostic label for the operation.
static int expect_error(TestContext *ctx, PlaceholderError actual,
                        PlaceholderError expected, const char *what) {
    if (actual == expected)
        return 0;

    fprintf(stderr, "%s: %s returned %s, expected %s\n", ctx->test_name, what,
            placeholder_error_string(actual),
            placeholder_error_string(expected));
    return 1;
}

// Verify that a render fails with one chunk size and succeeds with another.
//
// Parameters:
// `ctx`
//     Test context used for diagnostics.
// `placeholder`
//     Placeholder rendered in both attempts.
// `options`
//     Base options copied for both attempts. Only `chunk_size` is changed.
// `failing_chunk_size`
//     Chunk size that should return PLACEHOLDER_CHUNK_TOO_SMALL.
// `passing_chunk_size`
//     Larger chunk size that should render successfully.
// `what`
//     Short diagnostic label for the operation.
static int expect_chunk_size_boundary(TestContext *ctx,
                                      const Placeholder *placeholder,
                                      PlaceholderOptions options,
                                      size_t failing_chunk_size,
                                      size_t passing_chunk_size,
                                      const char *what) {
    char output[16384];
    size_t len = 0;

    options.chunk_size = failing_chunk_size;
    PlaceholderError error = placeholder_write_to_buffer(
        placeholder, &options, output, sizeof(output), &len);
    if (expect_error(ctx, error, PLACEHOLDER_CHUNK_TOO_SMALL, what))
        return 1;

    options.chunk_size = passing_chunk_size;
    error = placeholder_write_to_buffer(placeholder, &options, output,
                                        sizeof(output), &len);
    return expect_error(ctx, error, PLACEHOLDER_OK, what);
}

// Verify that the required buffer length reported by a failed render is exact.
//
// Parameters:
// `ctx`
//     Test context used for diagnostics.
// `placeholder`
//     Placeholder rendered in every attempt.
// `options`
//     Print options, or NULL to exercise default options.
// `what`
//     Short diagnostic label for the operation.
static int expect_buffer_len_round_trip(TestContext *ctx,
                                        const Placeholder *placeholder,
                                        const PlaceholderOptions *options,
                                        const char *what) {
    char *output = NULL;
    size_t required_len = 0;
    size_t exact_len = 0;
    CountingWriter counter = {0};
    int result = 1;

    PlaceholderError error = placeholder_write_to_buffer(
        placeholder, options, NULL, 0, &required_len);
    if (expect_error(ctx, error, PLACEHOLDER_WRITE_FAILED, what))
        goto cleanup;
    if (required_len == 0) {
        fprintf(stderr, "%s: %s reported empty required length\n",
                ctx->test_name, what);
        goto cleanup;
    }

    output = malloc(required_len);
    if (output == NULL) {
        fprintf(stderr, "%s: %s could not allocate %zu bytes\n", ctx->test_name,
                what, required_len);
        goto cleanup;
    }

    error = placeholder_write_to_buffer(placeholder, options, output,
                                        required_len, &exact_len);
    if (expect_error(ctx, error, PLACEHOLDER_OK, what))
        goto cleanup;
    if (exact_len != required_len) {
        fprintf(stderr, "%s: %s exact render length was %zu, expected %zu\n",
                ctx->test_name, what, exact_len, required_len);
        goto cleanup;
    }

    error =
        placeholder_write(placeholder, options, counting_as_writer(&counter));
    if (expect_error(ctx, error, PLACEHOLDER_OK, what))
        goto cleanup;
    if (counter.len != required_len) {
        fprintf(stderr, "%s: %s writer length was %zu, expected %zu\n",
                ctx->test_name, what, counter.len, required_len);
        goto cleanup;
    }

    error = placeholder_write_to_buffer(placeholder, options, output,
                                        required_len, NULL);
    if (expect_error(ctx, error, PLACEHOLDER_OK, what))
        goto cleanup;

    error = placeholder_write_to_buffer(placeholder, options, output,
                                        required_len - 1, NULL);
    if (expect_error(ctx, error, PLACEHOLDER_WRITE_FAILED, what))
        goto cleanup;

    result = 0;

cleanup:
    free(output);
    return result;
}

// Require rendered bytes to exactly match the null-terminated string fragments
// passed after `what`, in order. The fragment list must end with NULL.
static int expect_output(TestContext *ctx, const char *output, size_t len,
                         const char *what, ...) {
    size_t offset = 0;
    size_t fragment_index = 0;
    va_list args;

    va_start(args, what);
    for (;;) {
        const char *part = va_arg(args, const char *);
        if (part == NULL)
            break;

        size_t part_len = strlen(part);
        if (offset > len || part_len > len - offset) {
            va_end(args);
            fprintf(stderr,
                    "%s: %s output ended before fragment %zu at offset %zu\n",
                    ctx->test_name, what, fragment_index, offset);
            return 1;
        }
        if (memcmp(output + offset, part, part_len) != 0) {
            va_end(args);
            fprintf(stderr,
                    "%s: %s output differed in fragment %zu at offset %zu\n",
                    ctx->test_name, what, fragment_index, offset);
            return 1;
        }

        offset += part_len;
        ++fragment_index;
    }
    va_end(args);

    if (offset == len)
        return 0;

    fprintf(stderr, "%s: %s output had %zu trailing bytes\n", ctx->test_name,
            what, len - offset);
    return 1;
}

// Require null-terminated string fragments to have an exact combined length.
// The call site sits next to the chunk-size threshold explained by the size.
static int expect_fragments_len(TestContext *ctx, int line, size_t expected_len,
                                ...) {
    size_t len = 0;
    va_list args;

    va_start(args, expected_len);
    for (;;) {
        const char *part = va_arg(args, const char *);
        if (part == NULL)
            break;
        len += strlen(part);
    }
    va_end(args);

    if (len == expected_len)
        return 0;

    fprintf(stderr, "%s:%d: fragments were %zu bytes, expected %zu\n",
            ctx->test_name, line, len, expected_len);
    return 1;
}

#define EXPECT_FRAGMENTS_LEN(expected_len, ...)                                \
    expect_fragments_len(ctx, __LINE__, (expected_len), __VA_ARGS__, NULL)

// Require rendered bytes and writer-call boundaries to match the expected
// fragments. END_CHUNK marks the end offset of a successful writer callback.
static int expect_chunked_output(TestContext *ctx, const Capture *capture,
                                 const char *what, ...) {
    size_t offset = 0;
    size_t fragment_index = 0;
    size_t chunk_index = 0;
    va_list args;

    va_start(args, what);
    for (;;) {
        const char *part = va_arg(args, const char *);
        if (part == NULL)
            break;

        if (part == END_CHUNK) {
            if (chunk_index >= capture->chunk_count) {
                va_end(args);
                fprintf(stderr, "%s: %s had no chunk %zu\n", ctx->test_name,
                        what, chunk_index);
                return 1;
            }
            if (capture->chunk_ends[chunk_index] != offset) {
                va_end(args);
                fprintf(stderr, "%s: %s chunk %zu ended at %zu, expected %zu\n",
                        ctx->test_name, what, chunk_index,
                        capture->chunk_ends[chunk_index], offset);
                return 1;
            }
            ++chunk_index;
            continue;
        }

        size_t part_len = strlen(part);
        if (offset > capture->len || part_len > capture->len - offset) {
            va_end(args);
            fprintf(stderr,
                    "%s: %s output ended before fragment %zu at offset %zu\n",
                    ctx->test_name, what, fragment_index, offset);
            return 1;
        }
        if (memcmp(capture->data + offset, part, part_len) != 0) {
            va_end(args);
            fprintf(stderr,
                    "%s: %s output differed in fragment %zu at offset %zu\n",
                    ctx->test_name, what, fragment_index, offset);
            return 1;
        }

        offset += part_len;
        ++fragment_index;
    }
    va_end(args);

    if (offset != capture->len) {
        fprintf(stderr, "%s: %s output had %zu trailing bytes\n",
                ctx->test_name, what, capture->len - offset);
        return 1;
    }
    if (chunk_index != capture->chunk_count) {
        fprintf(stderr, "%s: %s checked %zu chunks, captured %zu\n",
                ctx->test_name, what, chunk_index, capture->chunk_count);
        return 1;
    }
    return 0;
}

// Require every successful writer callback to stay within the configured chunk
// size. This checks the PIPE_BUF-style contract independently from exact output
// contents.
static int expect_chunk_lengths_at_most(TestContext *ctx,
                                        const Capture *capture, size_t max_len,
                                        const char *what) {
    size_t previous_end = 0;

    for (size_t i = 0; i < capture->chunk_count; ++i) {
        size_t chunk_len = capture->chunk_ends[i] - previous_end;
        if (chunk_len > max_len) {
            fprintf(stderr,
                    "%s: %s chunk %zu was %zu bytes, expected at most %zu\n",
                    ctx->test_name, what, i, chunk_len, max_len);
            return 1;
        }
        previous_end = capture->chunk_ends[i];
    }

    return 0;
}

// Return the UTF-8 string for a row/column diacritic used in exact output
// expectations.
static const char *d(uint32_t num) {
    return rowcolumn_num_to_diacritic_utf8(num, NULL);
}

// Require the exact default rendering of base_placeholder(). The default final
// cursor position is bottom-right, so the final row has no trailing newline.
static int expect_default_base_output(TestContext *ctx, const char *output,
                                      size_t len, const char *what) {
    // clang-format off
    return expect_output(ctx, output, len, what,
        // Line 0
        RESET, "\033[38;2;2;3;4m", "\033[58;2;5;6;7m",
        PLACE, d(1), d(1), d(7), PLACE, d(1), d(2), d(7),
        RESET, "\n",
        // Line 1
        RESET, "\033[38;2;2;3;4m", "\033[58;2;5;6;7m",
        PLACE, d(2), d(1), d(7), PLACE, d(2), d(2), d(7),
        RESET,
        NULL);
    // clang-format on
}

// Check basic rendering with the default mode. This verifies the placeholder
// base character, ID color encoding, placement color encoding, row/column
// diacritics, inter-row newlines, and reset suffixes.
static int test_default_rendering(TestContext *ctx) {
    Placeholder placeholder = base_placeholder();
    char output[2048];
    size_t len = 0;

    PlaceholderError error = placeholder_write_to_buffer(
        &placeholder, NULL, output, sizeof(output), &len);
    if (expect_error(ctx, error, PLACEHOLDER_OK, "render"))
        return 1;

    return expect_default_base_output(ctx, output, len, "default");
}

// Check alternate modes and color encoding branches.
static int test_modes_and_color_options(TestContext *ctx) {
    char output[2048];
    size_t len = 0;
    Placeholder placeholder = base_placeholder();
    PlaceholderOptions options = placeholder_options_default();
    placeholder.image_id = 7;
    placeholder.placement_id = 8;
    options.mode.allow_256color_placement_id = true;
    PlaceholderError error = placeholder_write_to_buffer(
        &placeholder, &options, output, sizeof(output), &len);
    // clang-format off
    if (expect_error(ctx, error, PLACEHOLDER_OK, "render 256-color") ||
        expect_output(ctx, output, len, "256-color",
            // Line 0
            RESET, "\033[38;5;7m", "\033[58;5;8m",
            PLACE, d(1), d(1), PLACE, d(1), d(2),
            RESET, "\n",
            // Line 1
            RESET, "\033[38;5;7m", "\033[58;5;8m",
            PLACE, d(2), d(1), PLACE, d(2), d(2),
            RESET,
            NULL))
        return 1;
    // clang-format on

    placeholder.placement_id = 0;
    options.mode = placeholder_mode_minimal();
    error = placeholder_write_to_buffer(&placeholder, &options, output,
                                        sizeof(output), &len);
    // clang-format off
    if (expect_error(ctx, error, PLACEHOLDER_OK, "render minimal") ||
        expect_output(ctx, output, len, "minimal",
            // Line 0
            RESET, "\033[38;5;7m",
            PLACE, d(1), d(1), PLACE,
            RESET, "\n",
            // Line 1
            RESET, "\033[38;5;7m",
            PLACE, d(2), d(1), PLACE,
            RESET,
            NULL))
        return 1;
    // clang-format on

    options.mode.skip_zero_placement_id = false;
    error = placeholder_write_to_buffer(&placeholder, &options, output,
                                        sizeof(output), &len);
    // clang-format off
    if (expect_error(ctx, error, PLACEHOLDER_OK, "zero placement color") ||
        expect_output(ctx, output, len, "zero placement color",
            // Line 0
            RESET, "\033[38;5;7m", "\033[58;2;0;0;0m",
            PLACE, d(1), d(1), PLACE,
            RESET, "\n",
            // Line 1
            RESET, "\033[38;5;7m", "\033[58;2;0;0;0m",
            PLACE, d(2), d(1), PLACE,
            RESET,
            NULL))
        return 1;
    // clang-format on

    placeholder.placement_id = 0x010203;
    options.mode.allow_256color_placement_id = true;
    error = placeholder_write_to_buffer(&placeholder, &options, output,
                                        sizeof(output), &len);
    // clang-format off
    if (expect_error(ctx, error, PLACEHOLDER_OK, "large placement truecolor") ||
        expect_output(ctx, output, len, "large placement truecolor",
            // Line 0
            RESET, "\033[38;5;7m", "\033[58;2;1;2;3m",
            PLACE, d(1), d(1), PLACE,
            RESET, "\n",
            // Line 1
            RESET, "\033[38;5;7m", "\033[58;2;1;2;3m",
            PLACE, d(2), d(1), PLACE,
            RESET,
            NULL))
        return 1;
    // clang-format on

    placeholder.image_id = 7;
    placeholder.placement_id = 0;
    options.mode.allow_256color_image_id = false;
    error = placeholder_write_to_buffer(&placeholder, &options, output,
                                        sizeof(output), &len);
    // clang-format off
    if (expect_error(ctx, error, PLACEHOLDER_OK, "forced image truecolor") ||
        expect_output(ctx, output, len, "forced image truecolor",
            // Line 0
            RESET, "\033[38;2;0;0;7m", "\033[58;5;0m",
            PLACE, d(1), d(1), PLACE,
            RESET, "\n",
            // Line 1
            RESET, "\033[38;2;0;0;7m", "\033[58;5;0m",
            PLACE, d(2), d(1), PLACE,
            RESET,
            NULL))
        return 1;
    // clang-format on

    options.mode = placeholder_mode_complete();
    error = placeholder_write_to_buffer(&placeholder, &options, output,
                                        sizeof(output), &len);
    // clang-format off
    if (expect_error(ctx, error, PLACEHOLDER_OK, "render complete") ||
        expect_output(ctx, output, len, "complete",
            // Line 0
            RESET, "\033[38;5;7m",
            PLACE, d(1), d(1), d(1), PLACE, d(1), d(2), d(1),
            RESET, "\n",
            // Line 1
            RESET, "\033[38;5;7m",
            PLACE, d(2), d(1), d(1), PLACE, d(2), d(2), d(1),
            RESET,
            NULL))
        return 1;
    // clang-format on

    return 0;
}

// Check static, dynamic, and failing user formatting paths.
static int test_user_formatting(TestContext *ctx) {
    Placeholder placeholder = base_placeholder();
    PlaceholderOptions options = placeholder_options_default();
    DynamicFormatContext format = {0};
    char output[2048];
    size_t len = 0;
    options.format = placeholder_format_static("\033[48;5;9m");
    PlaceholderError error = placeholder_write_to_buffer(
        &placeholder, &options, output, sizeof(output), &len);
    // clang-format off
    if (expect_error(ctx, error, PLACEHOLDER_OK, "static formatting") ||
        expect_output(ctx, output, len, "static formatting",
            // Line 0
            RESET, "\033[48;5;9m", "\033[38;2;2;3;4m", "\033[58;2;5;6;7m",
            PLACE, d(1), d(1), d(7), PLACE, d(1), d(2), d(7),
            RESET, "\n",
            // Line 1
            RESET, "\033[48;5;9m", "\033[38;2;2;3;4m", "\033[58;2;5;6;7m",
            PLACE, d(2), d(1), d(7), PLACE, d(2), d(2), d(7),
            RESET,
            NULL))
        return 1;
    // clang-format on

    options.format = placeholder_format_none();
    error = placeholder_write_to_buffer(&placeholder, &options, output,
                                        sizeof(output), &len);
    if (expect_error(ctx, error, PLACEHOLDER_OK, "no formatting") ||
        expect_default_base_output(ctx, output, len, "no formatting"))
        return 1;

    options.format = (PlaceholderFormat){.per_cell = true};
    error = placeholder_write_to_buffer(&placeholder, &options, output,
                                        sizeof(output), &len);
    if (expect_error(ctx, error, PLACEHOLDER_OK, "null per-cell formatting") ||
        expect_default_base_output(ctx, output, len,
                                   "null per-cell formatting"))
        return 1;

    options.format = placeholder_format_static(NULL);
    error = placeholder_write_to_buffer(&placeholder, &options, output,
                                        sizeof(output), &len);
    if (expect_error(ctx, error, PLACEHOLDER_OK, "null static formatting") ||
        expect_default_base_output(ctx, output, len, "null static formatting"))
        return 1;

    options.format = placeholder_format_static("");
    error = placeholder_write_to_buffer(&placeholder, &options, output,
                                        sizeof(output), &len);
    if (expect_error(ctx, error, PLACEHOLDER_OK, "empty static formatting") ||
        expect_default_base_output(ctx, output, len, "empty static formatting"))
        return 1;

    options.format = placeholder_format_dynamic_cell(dynamic_format, &format);
    error = placeholder_write_to_buffer(&placeholder, &options, output,
                                        sizeof(output), &len);
    // clang-format off
    if (expect_error(ctx, error, PLACEHOLDER_OK, "cell formatting") ||
        expect_output(ctx, output, len, "cell formatting",
            // Line 0
            RESET, "\033[38;2;2;3;4m", "\033[58;2;5;6;7m",
            "\033[48;5;0m", PLACE, d(1), d(1), d(7),
            "\033[48;5;1m", PLACE, d(1), d(2), d(7),
            RESET, "\n",
            // Line 1
            RESET, "\033[38;2;2;3;4m", "\033[58;2;5;6;7m",
            "\033[48;5;1m", PLACE, d(2), d(1), d(7),
            "\033[48;5;2m", PLACE, d(2), d(2), d(7),
            RESET,
            NULL))
        return 1;
    // clang-format on

    options.format = placeholder_format_dynamic_row(dynamic_format, &format);
    error = placeholder_write_to_buffer(&placeholder, &options, output,
                                        sizeof(output), &len);
    // clang-format off
    if (expect_error(ctx, error, PLACEHOLDER_OK, "dynamic row formatting") ||
        expect_output(ctx, output, len, "dynamic row formatting",
            // Line 0
            RESET, "\033[48;5;0m", "\033[38;2;2;3;4m", "\033[58;2;5;6;7m",
            PLACE, d(1), d(1), d(7), PLACE, d(1), d(2), d(7),
            RESET, "\n",
            // Line 1
            RESET, "\033[48;5;1m", "\033[38;2;2;3;4m", "\033[58;2;5;6;7m",
            PLACE, d(2), d(1), d(7), PLACE, d(2), d(2), d(7),
            RESET,
            NULL))
        return 1;
    // clang-format on

    format.fail = true;
    options.format = placeholder_format_dynamic_cell(dynamic_format, &format);
    error = placeholder_write_to_buffer(&placeholder, &options, output,
                                        sizeof(output), &len);
    if (expect_error(ctx, error, PLACEHOLDER_FORMAT_FAILED,
                     "failing formatting"))
        return 1;

    options.format = placeholder_format_dynamic_row(dynamic_format, &format);
    error = placeholder_write_to_buffer(&placeholder, &options, output,
                                        sizeof(output), &len);
    if (expect_error(ctx, error, PLACEHOLDER_FORMAT_FAILED,
                     "failing row formatting"))
        return 1;

    options.format =
        placeholder_format_dynamic_cell(always_failing_format, NULL);
    error = placeholder_write_to_buffer(&placeholder, &options, output,
                                        sizeof(output), &len);
    if (expect_error(ctx, error, PLACEHOLDER_FORMAT_FAILED,
                     "always failing cell formatting"))
        return 1;

    options.format =
        placeholder_format_dynamic_row(always_failing_format, NULL);
    error = placeholder_write_to_buffer(&placeholder, &options, output,
                                        sizeof(output), &len);
    if (expect_error(ctx, error, PLACEHOLDER_FORMAT_FAILED,
                     "always failing row formatting"))
        return 1;

    char too_large[1100];
    fill_static_format(too_large, sizeof(too_large) - 1);
    placeholder.rect.end_row = 1;
    options.format = placeholder_format_static(too_large);
    options.chunk_size = 2048;
    error = placeholder_write_to_buffer(&placeholder, &options, output,
                                        sizeof(output), &len);
    return expect_error(ctx, error, PLACEHOLDER_OK, "large static formatting");
}

// Check background SGR format factories and alternating-format composition.
static int test_background_format_helpers(TestContext *ctx) {
    Placeholder placeholder = base_placeholder();
    PlaceholderOptions options = placeholder_options_default();
    char bg256[PLACEHOLDER_FORMAT_BG_256_SIZE];
    char bgrgb[PLACEHOLDER_FORMAT_BG_RGB_SIZE];
    char output[2048];
    size_t len = 0;

    PlaceholderFormat format256 =
        placeholder_format_bg_256(255, bg256, sizeof(bg256));
    if (strcmp(bg256, "\033[48;5;255m") != 0 || format256.func == NULL ||
        format256.per_cell) {
        fprintf(stderr, "%s: bad 256-color background\n", ctx->test_name);
        return 1;
    }

    PlaceholderFormat formatrgb =
        placeholder_format_bg_rgb(255, 255, 255, bgrgb, sizeof(bgrgb));
    if (strcmp(bgrgb, "\033[48;2;255;255;255m") != 0 ||
        formatrgb.func == NULL || formatrgb.per_cell) {
        fprintf(stderr, "%s: bad RGB background\n", ctx->test_name);
        return 1;
    }

    PlaceholderAlternatingFormat alternating = {
        .first = format256,
        .second = formatrgb,
    };
    options.format = placeholder_format_checkerboard(&alternating);
    PlaceholderError error = placeholder_write_to_buffer(
        &placeholder, &options, output, sizeof(output), &len);
    // clang-format off
    if (expect_error(ctx, error, PLACEHOLDER_OK, "checkerboard formatting") ||
        expect_output(ctx, output, len, "checkerboard formatting",
            // Line 0
            RESET, "\033[38;2;2;3;4m", "\033[58;2;5;6;7m",
            "\033[48;5;255m", PLACE, d(1), d(1), d(7),
            "\033[48;2;255;255;255m", PLACE, d(1), d(2), d(7),
            RESET, "\n",
            // Line 1
            RESET, "\033[38;2;2;3;4m", "\033[58;2;5;6;7m",
            "\033[48;2;255;255;255m", PLACE, d(2), d(1), d(7),
            "\033[48;5;255m", PLACE, d(2), d(2), d(7),
            RESET,
            NULL))
        return 1;
    // clang-format on

    // Horizontal stripes alternate by row, so every cell on the same row gets
    // the same additional background format.
    PlaceholderFormat hstripe_format =
        placeholder_format_horizontal_stripes(&alternating);
    if (hstripe_format.per_cell) {
        fprintf(stderr, "%s: simple horizontal stripes should format rows\n",
                ctx->test_name);
        return 1;
    }
    options.format = hstripe_format;
    error = placeholder_write_to_buffer(&placeholder, &options, output,
                                        sizeof(output), &len);
    // clang-format off
    if (expect_error(ctx, error, PLACEHOLDER_OK, "horizontal stripe formatting") ||
        expect_output(ctx, output, len, "horizontal stripe formatting",
            // Line 0
            RESET, "\033[48;5;255m", "\033[38;2;2;3;4m",
            "\033[58;2;5;6;7m",
            PLACE, d(1), d(1), d(7),
            PLACE, d(1), d(2), d(7),
            RESET, "\n",
            // Line 1
            RESET, "\033[48;2;255;255;255m", "\033[38;2;2;3;4m",
            "\033[58;2;5;6;7m",
            PLACE, d(2), d(1), d(7),
            PLACE, d(2), d(2), d(7),
            RESET,
            NULL))
        return 1;
    // clang-format on

    // Horizontal stripes with a per-cell child must also run per cell,
    // otherwise nested column-varying formats would be collapsed to the row
    // start column.
    PlaceholderAlternatingFormat hstripes_with_cell_child = {
        .first = placeholder_format_vertical_stripes(&alternating),
        .second = formatrgb,
    };
    hstripe_format =
        placeholder_format_horizontal_stripes(&hstripes_with_cell_child);
    if (!hstripe_format.per_cell) {
        fprintf(stderr, "%s: nested horizontal stripes should format cells\n",
                ctx->test_name);
        return 1;
    }

    // The per-cell child may also be the second branch; both branches must be
    // considered when deciding whether row-level formatting is sufficient.
    PlaceholderAlternatingFormat hstripes_with_second_cell_child = {
        .first = formatrgb,
        .second = placeholder_format_vertical_stripes(&alternating),
    };
    hstripe_format =
        placeholder_format_horizontal_stripes(&hstripes_with_second_cell_child);
    if (!hstripe_format.per_cell) {
        fprintf(stderr,
                "%s: second nested horizontal stripe should format cells\n",
                ctx->test_name);
        return 1;
    }

    hstripe_format = placeholder_format_horizontal_stripes(NULL);
    if (hstripe_format.per_cell) {
        fprintf(stderr, "%s: null horizontal stripes should format rows\n",
                ctx->test_name);
        return 1;
    }

    // Vertical stripes alternate by column, producing the same color order on
    // every row.
    options.format = placeholder_format_vertical_stripes(&alternating);
    error = placeholder_write_to_buffer(&placeholder, &options, output,
                                        sizeof(output), &len);
    // clang-format off
    if (expect_error(ctx, error, PLACEHOLDER_OK, "vertical stripe formatting") ||
        expect_output(ctx, output, len, "vertical stripe formatting",
            // Line 0
            RESET, "\033[38;2;2;3;4m", "\033[58;2;5;6;7m",
            "\033[48;5;255m", PLACE, d(1), d(1), d(7),
            "\033[48;2;255;255;255m", PLACE, d(1), d(2), d(7),
            RESET, "\n",
            // Line 1
            RESET, "\033[38;2;2;3;4m", "\033[58;2;5;6;7m",
            "\033[48;5;255m", PLACE, d(2), d(1), d(7),
            "\033[48;2;255;255;255m", PLACE, d(2), d(2), d(7),
            RESET,
            NULL))
        return 1;
    // clang-format on

    // A NULL checkerboard context should behave like no additional formatting
    // instead of failing the whole placeholder render.
    options.format = placeholder_format_checkerboard(NULL);
    error = placeholder_write_to_buffer(&placeholder, &options, output,
                                        sizeof(output), &len);
    if (expect_error(ctx, error, PLACEHOLDER_OK, "null checkerboard") ||
        expect_default_base_output(ctx, output, len, "null checkerboard"))
        return 1;

    PlaceholderAlternatingFormat checkerboard_with_none = {
        .first = placeholder_format_none(),
        .second = formatrgb,
    };
    options.format = placeholder_format_checkerboard(&checkerboard_with_none);
    error = placeholder_write_to_buffer(&placeholder, &options, output,
                                        sizeof(output), &len);
    // clang-format off
    if (expect_error(ctx, error, PLACEHOLDER_OK, "checkerboard empty cell") ||
        expect_output(ctx, output, len, "checkerboard empty cell",
            // Line 0
            RESET, "\033[38;2;2;3;4m", "\033[58;2;5;6;7m",
            PLACE, d(1), d(1), d(7),
            "\033[48;2;255;255;255m", PLACE, d(1), d(2), d(7),
            RESET, "\n",
            // Line 1
            RESET, "\033[38;2;2;3;4m", "\033[58;2;5;6;7m",
            "\033[48;2;255;255;255m", PLACE, d(2), d(1), d(7),
            PLACE, d(2), d(2), d(7),
            RESET,
            NULL))
        return 1;
    // clang-format on

    PlaceholderAlternatingFormat checkerboard_with_static_null = {
        .first = placeholder_format_static(NULL),
        .second = formatrgb,
    };
    options.format =
        placeholder_format_checkerboard(&checkerboard_with_static_null);
    error = placeholder_write_to_buffer(&placeholder, &options, output,
                                        sizeof(output), &len);
    // clang-format off
    if (expect_error(ctx, error, PLACEHOLDER_OK, "checkerboard null static") ||
        expect_output(ctx, output, len, "checkerboard null static",
            // Line 0
            RESET, "\033[38;2;2;3;4m", "\033[58;2;5;6;7m",
            PLACE, d(1), d(1), d(7),
            "\033[48;2;255;255;255m", PLACE, d(1), d(2), d(7),
            RESET, "\n",
            // Line 1
            RESET, "\033[38;2;2;3;4m", "\033[58;2;5;6;7m",
            "\033[48;2;255;255;255m", PLACE, d(2), d(1), d(7),
            PLACE, d(2), d(2), d(7),
            RESET,
            NULL))
        return 1;
    // clang-format on

    return 0;
}

// Check built-in positioners and the custom positioner callback for single-line
// placeholders and placeholders with a distinct middle line.
static int test_positioners(TestContext *ctx) {
    Placeholder placeholder = base_placeholder();
    PlaceholderOptions options = placeholder_options_default();
    PlaceholderAbsPos abs_pos = {.origin_col = 4, .origin_row = 7};
    char output[4096];
    size_t len = 0;

    placeholder.rect.end_row = 1;
    options.positioner = placeholder_position_absolute(&abs_pos);
    PlaceholderError error = placeholder_write_to_buffer(
        &placeholder, &options, output, sizeof(output), &len);
    // clang-format off
    if (expect_error(ctx, error, PLACEHOLDER_OK,
                     "one-line absolute positioning") ||
        expect_output(ctx, output, len, "one-line absolute positioning",
            RESET, "\033[8;5H", "\033[38;2;2;3;4m", "\033[58;2;5;6;7m",
            PLACE, d(1), d(1), d(7), PLACE, d(1), d(2), d(7),
            RESET,
            NULL))
        return 1;
    // clang-format on

    placeholder = base_placeholder();
    placeholder.rect.end_row = 3;
    options.positioner = placeholder_position_absolute(&abs_pos);
    error = placeholder_write_to_buffer(&placeholder, &options, output,
                                        sizeof(output), &len);
    // clang-format off
    if (expect_error(ctx, error, PLACEHOLDER_OK,
                     "three-line absolute positioning") ||
        expect_output(ctx, output, len, "three-line absolute positioning",
            // Line 0
            RESET, "\033[8;5H", "\033[38;2;2;3;4m", "\033[58;2;5;6;7m",
            PLACE, d(1), d(1), d(7), PLACE, d(1), d(2), d(7),
            RESET,
            // Middle line
            RESET, "\033[9;5H", "\033[38;2;2;3;4m", "\033[58;2;5;6;7m",
            PLACE, d(2), d(1), d(7), PLACE, d(2), d(2), d(7),
            RESET,
            // Last line
            RESET, "\033[10;5H", "\033[38;2;2;3;4m", "\033[58;2;5;6;7m",
            PLACE, d(3), d(1), d(7), PLACE, d(3), d(2), d(7),
            RESET,
            NULL))
        return 1;
    // clang-format on

    // Absolute positioners keep their row-start behavior and use absolute
    // cursor movement for the configured final cursor position.
    placeholder = base_placeholder();
    placeholder.rect.end_row = 3;
    abs_pos.final_cursor = PLACEHOLDER_FINAL_CURSOR_TOP_RIGHT;
    options.positioner = placeholder_position_absolute(&abs_pos);
    error = placeholder_write_to_buffer(&placeholder, &options, output,
                                        sizeof(output), &len);
    // clang-format off
    if (expect_error(ctx, error, PLACEHOLDER_OK,
                     "absolute positioning final cursor") ||
        expect_output(ctx, output, len, "absolute positioning final cursor",
            // Line 0
            RESET, "\033[8;5H", "\033[38;2;2;3;4m", "\033[58;2;5;6;7m",
            PLACE, d(1), d(1), d(7), PLACE, d(1), d(2), d(7),
            RESET,
            // Middle line
            RESET, "\033[9;5H", "\033[38;2;2;3;4m", "\033[58;2;5;6;7m",
            PLACE, d(2), d(1), d(7), PLACE, d(2), d(2), d(7),
            RESET,
            // Last line
            RESET, "\033[10;5H", "\033[38;2;2;3;4m", "\033[58;2;5;6;7m",
            PLACE, d(3), d(1), d(7), PLACE, d(3), d(2), d(7),
            RESET, "\033[8;7H",
            NULL))
        return 1;
    // clang-format on
    abs_pos.final_cursor = PLACEHOLDER_FINAL_CURSOR_BOTTOM_RIGHT;

    placeholder = base_placeholder();
    placeholder.rect.end_row = 1;
    options.positioner = placeholder_position_at_cursor_with_save(NULL);
    error = placeholder_write_to_buffer(&placeholder, &options, output,
                                        sizeof(output), &len);
    // clang-format off
    if (expect_error(ctx, error, PLACEHOLDER_OK, "one-line cursor positioning") ||
        expect_output(ctx, output, len, "one-line cursor positioning",
            RESET, "\033[38;2;2;3;4m", "\033[58;2;5;6;7m",
            PLACE, d(1), d(1), d(7), PLACE, d(1), d(2), d(7),
            RESET,
            NULL))
        return 1;
    // clang-format on

    placeholder = base_placeholder();
    placeholder.rect.end_row = 3;
    options.positioner = placeholder_position_at_cursor_with_save(NULL);
    error = placeholder_write_to_buffer(&placeholder, &options, output,
                                        sizeof(output), &len);
    // clang-format off
    if (expect_error(ctx, error, PLACEHOLDER_OK,
                     "three-line cursor positioning") ||
        expect_output(ctx, output, len, "three-line cursor positioning",
            // Line 0
            RESET, "\033[s", "\033[38;2;2;3;4m", "\033[58;2;5;6;7m",
            PLACE, d(1), d(1), d(7), PLACE, d(1), d(2), d(7),
            RESET, "\033[u\033D",
            // Middle line
            RESET, "\033[s", "\033[38;2;2;3;4m", "\033[58;2;5;6;7m",
            PLACE, d(2), d(1), d(7), PLACE, d(2), d(2), d(7),
            RESET, "\033[u\033D",
            // Last line
            RESET, "\033[38;2;2;3;4m", "\033[58;2;5;6;7m",
            PLACE, d(3), d(1), d(7), PLACE, d(3), d(2), d(7),
            RESET,
            NULL))
        return 1;
    // clang-format on

    placeholder = base_placeholder();
    placeholder.rect.end_row = 1;
    options.positioner = placeholder_position_at_cursor_with_moves(NULL);
    error = placeholder_write_to_buffer(&placeholder, &options, output,
                                        sizeof(output), &len);
    // clang-format off
    if (expect_error(ctx, error, PLACEHOLDER_OK,
                     "one-line cursor positioning no save") ||
        expect_output(ctx, output, len, "one-line cursor positioning no save",
            RESET, "\033[38;2;2;3;4m", "\033[58;2;5;6;7m",
            PLACE, d(1), d(1), d(7), PLACE, d(1), d(2), d(7),
            RESET,
            NULL))
        return 1;
    // clang-format on

    placeholder = base_placeholder();
    placeholder.rect.end_row = 3;
    options.positioner = placeholder_position_at_cursor_with_moves(NULL);
    error = placeholder_write_to_buffer(&placeholder, &options, output,
                                        sizeof(output), &len);
    // clang-format off
    if (expect_error(ctx, error, PLACEHOLDER_OK,
                     "three-line cursor positioning no save") ||
        expect_output(ctx, output, len, "three-line cursor positioning no save",
            // Line 0
            RESET, "\033[38;2;2;3;4m", "\033[58;2;5;6;7m",
            PLACE, d(1), d(1), d(7), PLACE, d(1), d(2), d(7),
            RESET, "\033[2D\033D",
            // Middle line
            RESET, "\033[38;2;2;3;4m", "\033[58;2;5;6;7m",
            PLACE, d(2), d(1), d(7), PLACE, d(2), d(2), d(7),
            RESET, "\033[2D\033D",
            // Last line
            RESET, "\033[38;2;2;3;4m", "\033[58;2;5;6;7m",
            PLACE, d(3), d(1), d(7), PLACE, d(3), d(2), d(7),
            RESET,
            NULL))
        return 1;
    // clang-format on

    // Configured standard positioners emit the first-line prefix once and use
    // the configured final cursor only after the last row.
    PlaceholderPositionConfig position_config = {
        .first_line_start_prefix = "\033[4G",
        .final_cursor = PLACEHOLDER_FINAL_CURSOR_NEXT_LINE,
    };
    placeholder = base_placeholder();
    placeholder.rect.end_row = 2;
    options.positioner =
        placeholder_position_at_cursor_with_moves(&position_config);
    error = placeholder_write_to_buffer(&placeholder, &options, output,
                                        sizeof(output), &len);
    // clang-format off
    if (expect_error(ctx, error, PLACEHOLDER_OK,
                     "configured cursor positioning no save") ||
        expect_output(ctx, output, len, "configured cursor positioning no save",
            // Line 0
            RESET, "\033[4G", "\033[38;2;2;3;4m", "\033[58;2;5;6;7m",
            PLACE, d(1), d(1), d(7), PLACE, d(1), d(2), d(7),
            RESET, "\033[2D\033D",
            // Last line
            RESET, "\033[38;2;2;3;4m", "\033[58;2;5;6;7m",
            PLACE, d(2), d(1), d(7), PLACE, d(2), d(2), d(7),
            RESET, "\n\r",
            NULL))
        return 1;
    // clang-format on

    placeholder = base_placeholder();
    placeholder.rect.end_row = 1;
    options.positioner = (PlaceholderPositioner){.func = custom_positioner};
    error = placeholder_write_to_buffer(&placeholder, &options, output,
                                        sizeof(output), &len);
    // clang-format off
    if (expect_error(ctx, error, PLACEHOLDER_OK,
                     "one-line custom positioning") ||
        expect_output(ctx, output, len, "one-line custom positioning",
            RESET, "<0:13>", "\033[38;2;2;3;4m", "\033[58;2;5;6;7m",
            PLACE, d(1), d(1), d(7), PLACE, d(1), d(2), d(7),
            RESET, "[0:14]",
            NULL))
        return 1;
    // clang-format on

    placeholder = base_placeholder();
    placeholder.rect.end_row = 3;
    options.positioner = (PlaceholderPositioner){.func = custom_positioner};
    error = placeholder_write_to_buffer(&placeholder, &options, output,
                                        sizeof(output), &len);
    // clang-format off
    if (expect_error(ctx, error, PLACEHOLDER_OK,
                     "three-line custom positioning") ||
        expect_output(ctx, output, len, "three-line custom positioning",
            // Line 0
            RESET, "<0:5>", "\033[38;2;2;3;4m", "\033[58;2;5;6;7m",
            PLACE, d(1), d(1), d(7), PLACE, d(1), d(2), d(7),
            RESET, "[0:6]",
            // Middle line
            RESET, "<1:1>", "\033[38;2;2;3;4m", "\033[58;2;5;6;7m",
            PLACE, d(2), d(1), d(7), PLACE, d(2), d(2), d(7),
            RESET, "[1:2]",
            // Last line
            RESET, "<2:9>", "\033[38;2;2;3;4m", "\033[58;2;5;6;7m",
            PLACE, d(3), d(1), d(7), PLACE, d(3), d(2), d(7),
            RESET, "[2:10]",
            NULL))
        return 1;
    // clang-format on

    return 0;
}

static int test_positioner_edge_cases(TestContext *ctx) {
    Placeholder placeholder = base_placeholder();
    PlaceholderOptions options = placeholder_options_default();
    PlaceholderAbsPos abs_pos = {.origin_col = 4, .origin_row = 7};
    char output[4096];

    PlaceholderPositioner linefeeds = placeholder_position_linefeeds(NULL);
    if (linefeeds.func(NULL, &placeholder, 0, 0, output, sizeof(output)) != 0 ||
        linefeeds.func(NULL, &placeholder, 0, PLACEHOLDER_POSITION_LINE_END,
                       NULL, 1) >= 0 ||
        linefeeds.func(NULL, &placeholder, 0, PLACEHOLDER_POSITION_LINE_END,
                       NULL, 0) != 1 ||
        linefeeds.func(NULL, NULL, 0,
                       PLACEHOLDER_POSITION_LINE_END |
                           PLACEHOLDER_POSITION_LAST_LINE,
                       output, sizeof(output)) >= 0 ||
        linefeeds.func(NULL, &placeholder, 0, PLACEHOLDER_POSITION_LINE_END,
                       output, 0) != 1) {
        fprintf(stderr, "%s: bad linefeed positioner edge behavior\n",
                ctx->test_name);
        return 1;
    }

    PlaceholderPositioner abs = placeholder_position_absolute(&abs_pos);
    if (abs.func(abs.ctx, &placeholder, 0, PLACEHOLDER_POSITION_LINE_START,
                 NULL, 1) >= 0 ||
        abs.func(abs.ctx, &placeholder, 0, PLACEHOLDER_POSITION_LINE_START,
                 NULL, 0) <= 0 ||
        abs.func(abs.ctx, &placeholder, 0, 0, output, sizeof(output)) != 0 ||
        abs.func(abs.ctx, NULL, 0,
                 PLACEHOLDER_POSITION_LINE_END | PLACEHOLDER_POSITION_LAST_LINE,
                 output, sizeof(output)) >= 0) {
        fprintf(stderr, "%s: bad absolute positioner null output behavior\n",
                ctx->test_name);
        return 1;
    }
    abs_pos.final_cursor = (PlaceholderFinalCursor)-1;
    if (abs.func(abs.ctx, &placeholder, 0,
                 PLACEHOLDER_POSITION_LINE_END | PLACEHOLDER_POSITION_LAST_LINE,
                 output, sizeof(output)) >= 0) {
        fprintf(stderr, "%s: bad absolute positioner final cursor behavior\n",
                ctx->test_name);
        return 1;
    }
    abs_pos.final_cursor = PLACEHOLDER_FINAL_CURSOR_BOTTOM_RIGHT;

    options.positioner = placeholder_position_at_cursor_with_save(NULL);
    if (options.positioner.func(NULL, &placeholder, 0,
                                PLACEHOLDER_POSITION_LINE_START, output,
                                2) != 3 ||
        options.positioner.func(NULL, &placeholder, 0,
                                PLACEHOLDER_POSITION_LINE_END, output,
                                4) != 5 ||
        options.positioner.func(NULL, &placeholder, 0, 0, output,
                                sizeof(output)) != 0) {
        fprintf(stderr, "%s: bad cursor positioner edge behavior\n",
                ctx->test_name);
        return 1;
    }
    if (options.positioner.func(NULL, NULL, 0,
                                PLACEHOLDER_POSITION_LINE_END |
                                    PLACEHOLDER_POSITION_LAST_LINE,
                                output, sizeof(output)) >= 0) {
        fprintf(stderr, "%s: bad cursor positioner null placeholder behavior\n",
                ctx->test_name);
        return 1;
    }

    PlaceholderPositionConfig invalid_final_cursor = {
        .final_cursor = (PlaceholderFinalCursor)-1,
    };
    options.positioner =
        placeholder_position_at_cursor_with_save(&invalid_final_cursor);
    if (options.positioner.func(options.positioner.ctx, &placeholder, 0,
                                PLACEHOLDER_POSITION_LINE_START |
                                    PLACEHOLDER_POSITION_LAST_LINE,
                                output, sizeof(output)) != 0 ||
        options.positioner.func(options.positioner.ctx, &placeholder, 0,
                                PLACEHOLDER_POSITION_LINE_END |
                                    PLACEHOLDER_POSITION_LAST_LINE,
                                output, sizeof(output)) >= 0) {
        fprintf(stderr, "%s: bad cursor positioner invalid final cursor\n",
                ctx->test_name);
        return 1;
    }

    placeholder.rect.end_col = 333;
    options.positioner = placeholder_position_at_cursor_with_moves(NULL);
    if (options.positioner.func(NULL, &placeholder, 0,
                                PLACEHOLDER_POSITION_LINE_START, output,
                                sizeof(output)) != 0 ||
        options.positioner.func(NULL, &placeholder, 0,
                                PLACEHOLDER_POSITION_LINE_END, output,
                                3) <= 3 ||
        options.positioner.func(NULL, NULL, 0, PLACEHOLDER_POSITION_LINE_END,
                                output, sizeof(output)) >= 0 ||
        options.positioner.func(NULL, &placeholder, 0, 0, output,
                                sizeof(output)) != 0) {
        fprintf(stderr, "%s: bad cursor no-save edge behavior\n",
                ctx->test_name);
        return 1;
    }
    options.positioner =
        placeholder_position_at_cursor_with_moves(&invalid_final_cursor);
    if (options.positioner.func(options.positioner.ctx, &placeholder, 0,
                                PLACEHOLDER_POSITION_LINE_END |
                                    PLACEHOLDER_POSITION_LAST_LINE,
                                output, sizeof(output)) >= 0) {
        fprintf(stderr, "%s: bad cursor no-save final cursor behavior\n",
                ctx->test_name);
        return 1;
    }

    return 0;
}

// Check grapheme-only mode keeps placeholder graphemes and inter-row linefeeds
// but removes automatic SGR color and reset sequences.
static int test_grapheme_only(TestContext *ctx) {
    Placeholder placeholder = base_placeholder();
    PlaceholderOptions options = placeholder_options_default();
    char output[2048];
    size_t len = 0;
    options.grapheme_only = true;
    PlaceholderError error = placeholder_write_to_buffer(
        &placeholder, &options, output, sizeof(output), &len);
    // clang-format off
    if (expect_error(ctx, error, PLACEHOLDER_OK, "render grapheme-only") ||
        expect_output(ctx, output, len, "grapheme-only",
            // Line 0
            PLACE, d(1), d(1), d(7), PLACE, d(1), d(2), d(7),
            "\n",
            // Line 1
            PLACE, d(2), d(1), d(7), PLACE, d(2), d(2), d(7),
            NULL))
        return 1;
    // clang-format on

    return 0;
}

// Check coordinates beyond the row/column diacritic table. Cells that cannot
// establish a usable row anchor use the configured replacement symbol, while
// later columns keep only representable metadata.
static int test_unrepresentable_coordinates(TestContext *ctx) {
    Placeholder placeholder = {
        .image_id = 7,
        .placement_id = 0,
        .rect = {.start_col = ROWCOLUMN_DIACRITIC_MAX - 1,
                 .start_row = 0,
                 .end_col = ROWCOLUMN_DIACRITIC_MAX + 1,
                 .end_row = 1},
    };
    PlaceholderOptions options = placeholder_options_default();
    char output[4096];
    size_t len = 0;

    PlaceholderError error = placeholder_write_to_buffer(
        &placeholder, &options, output, sizeof(output), &len);
    // clang-format off
    if (expect_error(ctx, error, PLACEHOLDER_OK, "large trailing column") ||
        expect_output(ctx, output, len, "large trailing column",
            RESET, "\033[38;5;7m",
            PLACE, d(1), d(ROWCOLUMN_DIACRITIC_MAX), PLACE, d(1),
            RESET,
            NULL))
        return 1;
    // clang-format on

    placeholder.rect.start_col = ROWCOLUMN_DIACRITIC_MAX;
    placeholder.rect.end_col = ROWCOLUMN_DIACRITIC_MAX + 2;
    error = placeholder_write_to_buffer(&placeholder, &options, output,
                                        sizeof(output), &len);
    // clang-format off
    if (expect_error(ctx, error, PLACEHOLDER_OK, "large first column") ||
        expect_output(ctx, output, len, "large first column",
            RESET, "□", "□",
            NULL))
        return 1;
    // clang-format on

    options.mode.unrepresentable_cell_symbol = NULL;
    error = placeholder_write_to_buffer(&placeholder, &options, output,
                                        sizeof(output), &len);
    if (expect_error(ctx, error, PLACEHOLDER_UNREPRESENTABLE_CELL,
                     "null large first column symbol"))
        return 1;

    placeholder.rect.start_col = 0;
    placeholder.rect.end_col = 2;
    placeholder.rect.start_row = ROWCOLUMN_DIACRITIC_MAX - 1;
    placeholder.rect.end_row = ROWCOLUMN_DIACRITIC_MAX + 1;
    options.mode.unrepresentable_cell_symbol = "□";
    error = placeholder_write_to_buffer(&placeholder, &options, output,
                                        sizeof(output), &len);
    // clang-format off
    if (expect_error(ctx, error, PLACEHOLDER_OK, "default large row symbol") ||
        expect_output(ctx, output, len, "default large row symbol",
            RESET, "\033[38;5;7m",
            PLACE, d(ROWCOLUMN_DIACRITIC_MAX), d(1),
            PLACE, d(ROWCOLUMN_DIACRITIC_MAX), d(2),
            RESET, "\n",
            RESET, "□", "□",
            NULL))
        return 1;
    // clang-format on

    placeholder.rect.start_row = ROWCOLUMN_DIACRITIC_MAX;
    placeholder.rect.end_row = ROWCOLUMN_DIACRITIC_MAX + 1;
    options.mode.unrepresentable_cell_symbol = "x";
    error = placeholder_write_to_buffer(&placeholder, &options, output,
                                        sizeof(output), &len);
    if (expect_error(ctx, error, PLACEHOLDER_OK, "custom large row symbol") ||
        expect_output(ctx, output, len, "custom large row symbol", RESET, "xx",
                      NULL))
        return 1;

    placeholder.placement_id = 0x010203;
    error = placeholder_write_to_buffer(&placeholder, &options, output,
                                        sizeof(output), &len);
    if (expect_error(ctx, error, PLACEHOLDER_OK,
                     "large row symbol with placement ID") ||
        expect_output(ctx, output, len, "large row symbol with placement ID",
                      RESET, "xx", NULL))
        return 1;
    placeholder.placement_id = 0;

    options.mode.unrepresentable_cell_symbol = "";
    error = placeholder_write_to_buffer(&placeholder, &options, output,
                                        sizeof(output), &len);
    if (expect_error(ctx, error, PLACEHOLDER_OK, "empty large row symbol") ||
        expect_output(ctx, output, len, "empty large row symbol", RESET, NULL))
        return 1;

    options.mode.unrepresentable_cell_symbol = NULL;
    error = placeholder_write_to_buffer(&placeholder, &options, output,
                                        sizeof(output), &len);
    return expect_error(ctx, error, PLACEHOLDER_UNREPRESENTABLE_CELL,
                        "null large row symbol");
}

// Check validation errors for every public invalid-input category.
static int test_validation_errors(TestContext *ctx) {
    Placeholder placeholder = base_placeholder();
    PlaceholderMode mode = placeholder_mode_default();

    if (expect_error(ctx, placeholder_validate(NULL, &mode),
                     PLACEHOLDER_INVALID_ARGUMENT, "null placeholder") ||
        expect_error(ctx, placeholder_validate(&placeholder, NULL),
                     PLACEHOLDER_INVALID_ARGUMENT, "null mode"))
        return 1;

    placeholder.image_id = 0;
    if (expect_error(ctx, placeholder_validate(&placeholder, &mode),
                     PLACEHOLDER_INVALID_IMAGE_ID, "zero image ID"))
        return 1;

    placeholder = base_placeholder();
    placeholder.placement_id = 0x1000000;
    if (expect_error(ctx, placeholder_validate(&placeholder, &mode),
                     PLACEHOLDER_INVALID_PLACEMENT_ID, "large placement ID"))
        return 1;

    placeholder = base_placeholder();
    placeholder.rect.end_col = placeholder.rect.start_col;
    if (expect_error(ctx, placeholder_validate(&placeholder, &mode),
                     PLACEHOLDER_INVALID_RECTANGLE, "empty columns"))
        return 1;

    placeholder = base_placeholder();
    placeholder.rect.end_row = placeholder.rect.start_row;
    if (expect_error(ctx, placeholder_validate(&placeholder, &mode),
                     PLACEHOLDER_INVALID_RECTANGLE, "empty rows"))
        return 1;

    placeholder = base_placeholder();
    mode = placeholder_mode_default();
    mode.first_col_level = (PlaceholderDiacriticLevel)99;
    if (expect_error(ctx, placeholder_validate(&placeholder, &mode),
                     PLACEHOLDER_INVALID_MODE, "bad first level"))
        return 1;

    mode = placeholder_mode_default();
    mode.other_cols_level = (PlaceholderDiacriticLevel)99;
    if (expect_error(ctx, placeholder_validate(&placeholder, &mode),
                     PLACEHOLDER_INVALID_MODE, "bad level"))
        return 1;

    placeholder = base_placeholder();
    mode = placeholder_mode_default();
    mode.first_col_level = PLACEHOLDER_DIACRITIC_NONE;
    if (expect_error(ctx, placeholder_validate(&placeholder, &mode),
                     PLACEHOLDER_INCOMPLETE_FIRST_COLUMN, "none first level"))
        return 1;

    placeholder = base_placeholder();
    mode = placeholder_mode_default();
    mode.first_col_level = PLACEHOLDER_DIACRITIC_ROW;
    if (expect_error(ctx, placeholder_validate(&placeholder, &mode),
                     PLACEHOLDER_INCOMPLETE_FIRST_COLUMN,
                     "high image byte row mode"))
        return 1;

    mode.first_col_level = PLACEHOLDER_DIACRITIC_ROW_COL;
    if (expect_error(ctx, placeholder_validate(&placeholder, &mode),
                     PLACEHOLDER_INCOMPLETE_FIRST_COLUMN,
                     "high image byte row-col mode"))
        return 1;

    placeholder = base_placeholder();
    placeholder.image_id = 7;
    mode = placeholder_mode_default();
    mode.first_col_level = PLACEHOLDER_DIACRITIC_ROW;
    mode.other_cols_level = PLACEHOLDER_DIACRITIC_NONE;
    if (expect_error(ctx, placeholder_validate(&placeholder, &mode),
                     PLACEHOLDER_OK, "zero first column row mode"))
        return 1;

    placeholder = base_placeholder();
    placeholder.image_id = 7;
    placeholder.rect.start_col = 1;
    placeholder.rect.end_col = 2;
    mode = placeholder_mode_minimal();
    if (expect_error(ctx, placeholder_validate(&placeholder, &mode),
                     PLACEHOLDER_OK, "nonzero first column minimal mode"))
        return 1;

    mode = placeholder_mode_default();
    mode.first_col_level = PLACEHOLDER_DIACRITIC_ROW;
    if (expect_error(ctx, placeholder_validate(&placeholder, &mode),
                     PLACEHOLDER_INCOMPLETE_FIRST_COLUMN,
                     "nonzero first column row mode"))
        return 1;

    placeholder = base_placeholder();
    placeholder.image_id = 7;
    placeholder.rect.end_col = 1;
    mode.first_col_level = PLACEHOLDER_DIACRITIC_ROW_COL;
    mode.other_cols_level = PLACEHOLDER_DIACRITIC_ROW_COL;
    if (expect_error(ctx, placeholder_validate(&placeholder, &mode),
                     PLACEHOLDER_OK, "one-column row-column mode"))
        return 1;

    mode = placeholder_mode_default();
    placeholder.rect.start_row = ROWCOLUMN_DIACRITIC_MAX;
    placeholder.rect.end_row = ROWCOLUMN_DIACRITIC_MAX + 1;
    if (expect_error(ctx, placeholder_validate(&placeholder, &mode),
                     PLACEHOLDER_OK, "large row"))
        return 1;

    mode.unrepresentable_cell_symbol = NULL;
    if (expect_error(ctx, placeholder_validate(&placeholder, &mode),
                     PLACEHOLDER_UNREPRESENTABLE_CELL,
                     "large row null cell symbol"))
        return 1;

    placeholder = base_placeholder();
    mode = placeholder_mode_default();
    placeholder.rect.start_col = ROWCOLUMN_DIACRITIC_MAX;
    placeholder.rect.end_col = ROWCOLUMN_DIACRITIC_MAX + 1;
    if (expect_error(ctx, placeholder_validate(&placeholder, &mode),
                     PLACEHOLDER_OK, "large first column"))
        return 1;

    placeholder = base_placeholder();
    placeholder.rect.start_col = 0;
    placeholder.rect.end_col = ROWCOLUMN_DIACRITIC_MAX + 1;
    return expect_error(ctx, placeholder_validate(&placeholder, &mode),
                        PLACEHOLDER_OK, "large other column");
}

// Check exact output and writer-call boundaries for every chunk size up to the
// point where the whole placeholder fits in a single writer call. Smaller
// chunks must insert resets and reapply active formatting at exact boundaries.
static int test_chunk_size_steps_exact(TestContext *ctx) {
    Placeholder placeholder = {
        .image_id = 7,
        .placement_id = 0,
        .rect = {.start_col = 0, .start_row = 0, .end_col = 4, .end_row = 3},
    };
    FixedLengthFormatContext cell_format = {.len = 2};
    PlaceholderOptions options = placeholder_options_default();
    const char *fg = "\033[38;5;7m";
    const char *cell_fmt = "xx";
    Capture capture = {0};
    PlaceholderError error;
    const size_t max_chunk_size = 256;

#define CELL(row, col) cell_fmt, PLACE, d(row), d(col)
#define TWO_CELL(row, col) CELL(row, col), CELL(row, (col) + 1)
#define THREE_CELL(row, col) TWO_CELL(row, col), CELL(row, (col) + 2)
#define FOUR_CELL(row, col) TWO_CELL(row, col), TWO_CELL(row, (col) + 2)
#define ROW(start, row, end) RESET, start, fg, FOUR_CELL(row, 1), RESET, end

    options.format =
        placeholder_format_dynamic_cell(fixed_length_format, &cell_format);
    options.positioner = (PlaceholderPositioner){.func = custom_positioner};

    for (size_t chunk_size = 1; chunk_size <= max_chunk_size; ++chunk_size) {
        char what[64];

        snprintf(what, sizeof(what), "chunk size %zu", chunk_size);
        options.chunk_size = chunk_size;
        capture = (Capture){0};
        error = placeholder_write(&placeholder, &options,
                                  capture_as_writer(&capture));

        if (chunk_size <= 26) {
            if (expect_error(ctx, error, PLACEHOLDER_CHUNK_TOO_SMALL, what))
                return 1;
            continue;
        }

        if (EXPECT_FRAGMENTS_LEN(9, RESET, "<0:5>") ||
            EXPECT_FRAGMENTS_LEN(27, RESET, fg, CELL(1, 1), RESET))
            return 1;

        if (expect_error(ctx, error, PLACEHOLDER_OK, what) ||
            expect_chunk_lengths_at_most(ctx, &capture, chunk_size, what))
            return 1;

        // clang-format off
        if (chunk_size <= 31) {
            if (expect_chunked_output(ctx, &capture, what,
                RESET, "<0:5>", END_CHUNK,
                RESET, fg, CELL(1, 1), RESET, END_CHUNK,
                RESET, fg, CELL(1, 2), RESET, END_CHUNK,
                RESET, fg, CELL(1, 3), RESET, END_CHUNK,
                RESET, fg, CELL(1, 4), RESET, END_CHUNK,
                "[0:6]", END_CHUNK,
                RESET, "<1:1>", END_CHUNK,
                RESET, fg, CELL(2, 1), RESET, END_CHUNK,
                RESET, fg, CELL(2, 2), RESET, END_CHUNK,
                RESET, fg, CELL(2, 3), RESET, END_CHUNK,
                RESET, fg, CELL(2, 4), RESET, END_CHUNK,
                "[1:2]", END_CHUNK,
                RESET, "<2:9>", END_CHUNK,
                RESET, fg, CELL(3, 1), RESET, END_CHUNK,
                RESET, fg, CELL(3, 2), RESET, END_CHUNK,
                RESET, fg, CELL(3, 3), RESET, END_CHUNK,
                RESET, fg, CELL(3, 4), RESET, END_CHUNK,
                "[2:10]", END_CHUNK,
                NULL))
                return 1;
            continue;
        }

        if (EXPECT_FRAGMENTS_LEN(32,
                                 RESET, fg, CELL(1, 4), RESET, "[0:6]"))
            return 1;

        if (chunk_size == 32) {
            if (expect_chunked_output(ctx, &capture, what,
                RESET, "<0:5>", fg, CELL(1, 1), RESET, END_CHUNK,
                RESET, fg, CELL(1, 2), RESET, END_CHUNK,
                RESET, fg, CELL(1, 3), RESET, END_CHUNK,
                RESET, fg, CELL(1, 4), RESET, "[0:6]", END_CHUNK,
                RESET, "<1:1>", fg, CELL(2, 1), RESET, END_CHUNK,
                RESET, fg, CELL(2, 2), RESET, END_CHUNK,
                RESET, fg, CELL(2, 3), RESET, END_CHUNK,
                RESET, fg, CELL(2, 4), RESET, "[1:2]", END_CHUNK,
                RESET, "<2:9>", fg, CELL(3, 1), RESET, END_CHUNK,
                RESET, fg, CELL(3, 2), RESET, END_CHUNK,
                RESET, fg, CELL(3, 3), RESET, END_CHUNK,
                RESET, fg, CELL(3, 4), RESET, END_CHUNK,
                "[2:10]", END_CHUNK,
                NULL))
                return 1;
            continue;
        }

        if (EXPECT_FRAGMENTS_LEN(33,
                                 RESET, fg, CELL(3, 4), RESET, "[2:10]"))
            return 1;

        if (chunk_size <= 36) {
            if (expect_chunked_output(ctx, &capture, what,
                RESET, "<0:5>", fg, CELL(1, 1), RESET, END_CHUNK,
                RESET, fg, CELL(1, 2), RESET, END_CHUNK,
                RESET, fg, CELL(1, 3), RESET, END_CHUNK,
                RESET, fg, CELL(1, 4), RESET, "[0:6]", END_CHUNK,
                RESET, "<1:1>", fg, CELL(2, 1), RESET, END_CHUNK,
                RESET, fg, CELL(2, 2), RESET, END_CHUNK,
                RESET, fg, CELL(2, 3), RESET, END_CHUNK,
                RESET, fg, CELL(2, 4), RESET, "[1:2]", END_CHUNK,
                RESET, "<2:9>", fg, CELL(3, 1), RESET, END_CHUNK,
                RESET, fg, CELL(3, 2), RESET, END_CHUNK,
                RESET, fg, CELL(3, 3), RESET, END_CHUNK,
                RESET, fg, CELL(3, 4), RESET, "[2:10]", END_CHUNK,
                NULL))
                return 1;
            continue;
        }

        if (EXPECT_FRAGMENTS_LEN(37, RESET, fg, TWO_CELL(1, 2), RESET))
            return 1;

        if (chunk_size <= 41) {
            if (expect_chunked_output(ctx, &capture, what,
                RESET, "<0:5>", fg, CELL(1, 1), RESET, END_CHUNK,
                RESET, fg, TWO_CELL(1, 2), RESET, END_CHUNK,
                RESET, fg, CELL(1, 4), RESET, "[0:6]", END_CHUNK,
                RESET, "<1:1>", fg, CELL(2, 1), RESET, END_CHUNK,
                RESET, fg, TWO_CELL(2, 2), RESET, END_CHUNK,
                RESET, fg, CELL(2, 4), RESET, "[1:2]", END_CHUNK,
                RESET, "<2:9>", fg, CELL(3, 1), RESET, END_CHUNK,
                RESET, fg, TWO_CELL(3, 2), RESET, END_CHUNK,
                RESET, fg, CELL(3, 4), RESET, "[2:10]", END_CHUNK,
                NULL))
                return 1;
            continue;
        }

        if (EXPECT_FRAGMENTS_LEN(42,
                                 RESET, "<0:5>", fg, TWO_CELL(1, 1), RESET))
            return 1;

        if (chunk_size == 42) {
            if (expect_chunked_output(ctx, &capture, what,
                RESET, "<0:5>", fg, TWO_CELL(1, 1), RESET, END_CHUNK,
                RESET, fg, TWO_CELL(1, 3), RESET, "[0:6]", END_CHUNK,
                RESET, "<1:1>", fg, TWO_CELL(2, 1), RESET, END_CHUNK,
                RESET, fg, TWO_CELL(2, 3), RESET, "[1:2]", END_CHUNK,
                RESET, "<2:9>", fg, TWO_CELL(3, 1), RESET, END_CHUNK,
                RESET, fg, TWO_CELL(3, 3), RESET, END_CHUNK,
                "[2:10]", END_CHUNK,
                NULL))
                return 1;
            continue;
        }

        if (chunk_size <= 51) {
            if (expect_chunked_output(ctx, &capture, what,
                RESET, "<0:5>", fg, TWO_CELL(1, 1), RESET, END_CHUNK,
                RESET, fg, TWO_CELL(1, 3), RESET, "[0:6]", END_CHUNK,
                RESET, "<1:1>", fg, TWO_CELL(2, 1), RESET, END_CHUNK,
                RESET, fg, TWO_CELL(2, 3), RESET, "[1:2]", END_CHUNK,
                RESET, "<2:9>", fg, TWO_CELL(3, 1), RESET, END_CHUNK,
                RESET, fg, TWO_CELL(3, 3), RESET, "[2:10]", END_CHUNK,
                NULL))
                return 1;
            continue;
        }

        if (EXPECT_FRAGMENTS_LEN(52,
                                 RESET, "<0:5>", fg, THREE_CELL(1, 1), RESET))
            return 1;

        if (chunk_size <= 61) {
            if (expect_chunked_output(ctx, &capture, what,
                RESET, "<0:5>", fg, THREE_CELL(1, 1), RESET, END_CHUNK,
                RESET, fg, CELL(1, 4), RESET, "[0:6]", END_CHUNK,
                RESET, "<1:1>", fg, THREE_CELL(2, 1), RESET, END_CHUNK,
                RESET, fg, CELL(2, 4), RESET, "[1:2]", END_CHUNK,
                RESET, "<2:9>", fg, THREE_CELL(3, 1), RESET, END_CHUNK,
                RESET, fg, CELL(3, 4), RESET, "[2:10]", END_CHUNK,
                NULL))
                return 1;
            continue;
        }

        if (EXPECT_FRAGMENTS_LEN(62,
                                 RESET, "<0:5>", fg, FOUR_CELL(1, 1), RESET))
            return 1;

        if (chunk_size <= 66) {
            if (expect_chunked_output(ctx, &capture, what,
                RESET, "<0:5>", fg, FOUR_CELL(1, 1), RESET, END_CHUNK,
                "[0:6]", END_CHUNK,
                RESET, "<1:1>", fg, FOUR_CELL(2, 1), RESET, END_CHUNK,
                "[1:2]", END_CHUNK,
                RESET, "<2:9>", fg, FOUR_CELL(3, 1), RESET, END_CHUNK,
                "[2:10]", END_CHUNK,
                NULL))
                return 1;
            continue;
        }

        if (EXPECT_FRAGMENTS_LEN(67, ROW("<0:5>", 1, "[0:6]")))
            return 1;

        if (chunk_size == 67) {
            if (expect_chunked_output(ctx, &capture, what,
                ROW("<0:5>", 1, "[0:6]"), END_CHUNK,
                ROW("<1:1>", 2, "[1:2]"), END_CHUNK,
                RESET, "<2:9>", fg, FOUR_CELL(3, 1), RESET, END_CHUNK,
                "[2:10]", END_CHUNK,
                NULL))
                return 1;
            continue;
        }

        if (chunk_size <= 133) {
            if (expect_chunked_output(ctx, &capture, what,
                ROW("<0:5>", 1, "[0:6]"), END_CHUNK,
                ROW("<1:1>", 2, "[1:2]"), END_CHUNK,
                ROW("<2:9>", 3, "[2:10]"), END_CHUNK,
                NULL))
                return 1;
            continue;
        }

        if (EXPECT_FRAGMENTS_LEN(134,
                                 ROW("<0:5>", 1, "[0:6]"),
                                 ROW("<1:1>", 2, "[1:2]")))
            return 1;

        if (chunk_size <= 201) {
            if (expect_chunked_output(ctx, &capture, what,
                ROW("<0:5>", 1, "[0:6]"),
                ROW("<1:1>", 2, "[1:2]"), END_CHUNK,
                ROW("<2:9>", 3, "[2:10]"), END_CHUNK,
                NULL))
                return 1;
            continue;
        }

        if (EXPECT_FRAGMENTS_LEN(202,
                                 ROW("<0:5>", 1, "[0:6]"),
                                 ROW("<1:1>", 2, "[1:2]"),
                                 ROW("<2:9>", 3, "[2:10]")))
            return 1;

        if (expect_chunked_output(ctx, &capture, what,
            ROW("<0:5>", 1, "[0:6]"),
            ROW("<1:1>", 2, "[1:2]"),
            ROW("<2:9>", 3, "[2:10]"), END_CHUNK,
            NULL))
            return 1;
        // clang-format on
    }

    return 0;

#undef ROW
#undef FOUR_CELL
#undef THREE_CELL
#undef TWO_CELL
#undef CELL
}

// Check exact chunking steps for row-level formatting and a mode where the
// first column carries an extra image-ID byte diacritic.
static int test_row_format_chunk_size_steps_exact(TestContext *ctx) {
    Placeholder placeholder = {
        .image_id = 7,
        .placement_id = 0,
        .rect = {.start_col = 0, .start_row = 0, .end_col = 4, .end_row = 3},
    };
    FixedLengthFormatContext row_format = {.len = 2};
    PlaceholderOptions options = placeholder_options_default();
    const char *row_fmt = "xx";
    const char *fg = "\033[38;5;7m";
    Capture capture = {0};
    PlaceholderError error;
    const size_t max_chunk_size = 256;

#define FIRST_CELL(row) PLACE, d(row), d(1), d(1)
#define CELL(row, col) PLACE, d(row), d(col)
#define TWO_CELL(row, col) CELL(row, col), CELL(row, (col) + 1)
#define THREE_CELL(row, col) TWO_CELL(row, col), CELL(row, (col) + 2)
#define ROW(start, row, end)                                                   \
    RESET, start, row_fmt, fg, FIRST_CELL(row), THREE_CELL(row, 2), RESET, end

    options.mode.first_col_level = PLACEHOLDER_DIACRITIC_ROW_COL_IDBYTE;
    options.mode.other_cols_level = PLACEHOLDER_DIACRITIC_ROW_COL;
    options.format =
        placeholder_format_dynamic_row(fixed_length_format, &row_format);
    options.positioner = (PlaceholderPositioner){.func = custom_positioner};

    for (size_t chunk_size = 1; chunk_size <= max_chunk_size; ++chunk_size) {
        char what[64];

        snprintf(what, sizeof(what), "row chunk size %zu", chunk_size);
        options.chunk_size = chunk_size;
        capture = (Capture){0};
        error = placeholder_write(&placeholder, &options,
                                  capture_as_writer(&capture));

        if (chunk_size <= 28) {
            if (expect_error(ctx, error, PLACEHOLDER_CHUNK_TOO_SMALL, what))
                return 1;
            continue;
        }

        if (EXPECT_FRAGMENTS_LEN(29, RESET, row_fmt, fg, FIRST_CELL(1), RESET))
            return 1;

        if (expect_error(ctx, error, PLACEHOLDER_OK, what) ||
            expect_chunk_lengths_at_most(ctx, &capture, chunk_size, what))
            return 1;

        // clang-format off
        if (chunk_size <= 31) {
            if (expect_chunked_output(ctx, &capture, what,
                RESET, "<0:5>", END_CHUNK,
                RESET, row_fmt, fg, FIRST_CELL(1), RESET, END_CHUNK,
                RESET, row_fmt, fg, CELL(1, 2), RESET, END_CHUNK,
                RESET, row_fmt, fg, CELL(1, 3), RESET, END_CHUNK,
                RESET, row_fmt, fg, CELL(1, 4), RESET, END_CHUNK,
                "[0:6]", END_CHUNK,
                RESET, "<1:1>", END_CHUNK,
                RESET, row_fmt, fg, FIRST_CELL(2), RESET, END_CHUNK,
                RESET, row_fmt, fg, CELL(2, 2), RESET, END_CHUNK,
                RESET, row_fmt, fg, CELL(2, 3), RESET, END_CHUNK,
                RESET, row_fmt, fg, CELL(2, 4), RESET, END_CHUNK,
                "[1:2]", END_CHUNK,
                RESET, "<2:9>", END_CHUNK,
                RESET, row_fmt, fg, FIRST_CELL(3), RESET, END_CHUNK,
                RESET, row_fmt, fg, CELL(3, 2), RESET, END_CHUNK,
                RESET, row_fmt, fg, CELL(3, 3), RESET, END_CHUNK,
                RESET, row_fmt, fg, CELL(3, 4), RESET, END_CHUNK,
                "[2:10]", END_CHUNK,
                NULL))
                return 1;
            continue;
        }

        if (EXPECT_FRAGMENTS_LEN(32,
                                 RESET, row_fmt, fg, CELL(1, 4), RESET,
                                 "[0:6]"))
            return 1;

        if (chunk_size == 32) {
            if (expect_chunked_output(ctx, &capture, what,
                RESET, "<0:5>", END_CHUNK,
                RESET, row_fmt, fg, FIRST_CELL(1), RESET, END_CHUNK,
                RESET, row_fmt, fg, CELL(1, 2), RESET, END_CHUNK,
                RESET, row_fmt, fg, CELL(1, 3), RESET, END_CHUNK,
                RESET, row_fmt, fg, CELL(1, 4), RESET, "[0:6]", END_CHUNK,
                RESET, "<1:1>", END_CHUNK,
                RESET, row_fmt, fg, FIRST_CELL(2), RESET, END_CHUNK,
                RESET, row_fmt, fg, CELL(2, 2), RESET, END_CHUNK,
                RESET, row_fmt, fg, CELL(2, 3), RESET, END_CHUNK,
                RESET, row_fmt, fg, CELL(2, 4), RESET, "[1:2]", END_CHUNK,
                RESET, "<2:9>", END_CHUNK,
                RESET, row_fmt, fg, FIRST_CELL(3), RESET, END_CHUNK,
                RESET, row_fmt, fg, CELL(3, 2), RESET, END_CHUNK,
                RESET, row_fmt, fg, CELL(3, 3), RESET, END_CHUNK,
                RESET, row_fmt, fg, CELL(3, 4), RESET, END_CHUNK,
                "[2:10]", END_CHUNK,
                NULL))
                return 1;
            continue;
        }

        if (EXPECT_FRAGMENTS_LEN(33,
                                 RESET, row_fmt, fg, CELL(3, 4), RESET,
                                 "[2:10]"))
            return 1;

        if (chunk_size == 33) {
            if (expect_chunked_output(ctx, &capture, what,
                RESET, "<0:5>", END_CHUNK,
                RESET, row_fmt, fg, FIRST_CELL(1), RESET, END_CHUNK,
                RESET, row_fmt, fg, CELL(1, 2), RESET, END_CHUNK,
                RESET, row_fmt, fg, CELL(1, 3), RESET, END_CHUNK,
                RESET, row_fmt, fg, CELL(1, 4), RESET, "[0:6]", END_CHUNK,
                RESET, "<1:1>", END_CHUNK,
                RESET, row_fmt, fg, FIRST_CELL(2), RESET, END_CHUNK,
                RESET, row_fmt, fg, CELL(2, 2), RESET, END_CHUNK,
                RESET, row_fmt, fg, CELL(2, 3), RESET, END_CHUNK,
                RESET, row_fmt, fg, CELL(2, 4), RESET, "[1:2]", END_CHUNK,
                RESET, "<2:9>", END_CHUNK,
                RESET, row_fmt, fg, FIRST_CELL(3), RESET, END_CHUNK,
                RESET, row_fmt, fg, CELL(3, 2), RESET, END_CHUNK,
                RESET, row_fmt, fg, CELL(3, 3), RESET, END_CHUNK,
                RESET, row_fmt, fg, CELL(3, 4), RESET, "[2:10]", END_CHUNK,
                NULL))
                return 1;
            continue;
        }

        if (EXPECT_FRAGMENTS_LEN(34,
                RESET, "<0:5>", row_fmt, fg, FIRST_CELL(1), RESET))
            return 1;

        if (chunk_size == 34) {
            if (expect_chunked_output(ctx, &capture, what,
                RESET, "<0:5>", row_fmt, fg, FIRST_CELL(1), RESET, END_CHUNK,
                RESET, row_fmt, fg, CELL(1, 2), RESET, END_CHUNK,
                RESET, row_fmt, fg, CELL(1, 3), RESET, END_CHUNK,
                RESET, row_fmt, fg, CELL(1, 4), RESET, "[0:6]", END_CHUNK,
                RESET, "<1:1>", row_fmt, fg, FIRST_CELL(2), RESET, END_CHUNK,
                RESET, row_fmt, fg, CELL(2, 2), RESET, END_CHUNK,
                RESET, row_fmt, fg, CELL(2, 3), RESET, END_CHUNK,
                RESET, row_fmt, fg, CELL(2, 4), RESET, "[1:2]", END_CHUNK,
                RESET, "<2:9>", row_fmt, fg, FIRST_CELL(3), RESET, END_CHUNK,
                RESET, row_fmt, fg, CELL(3, 2), RESET, END_CHUNK,
                RESET, row_fmt, fg, CELL(3, 3), RESET, END_CHUNK,
                RESET, row_fmt, fg, CELL(3, 4), RESET, "[2:10]", END_CHUNK,
                NULL))
                return 1;
            continue;
        }

        if (EXPECT_FRAGMENTS_LEN(35, RESET, row_fmt, fg, TWO_CELL(1, 2), RESET))
            return 1;

        if (chunk_size <= 41) {
            if (expect_chunked_output(ctx, &capture, what,
                RESET, "<0:5>", row_fmt, fg, FIRST_CELL(1), RESET, END_CHUNK,
                RESET, row_fmt, fg, TWO_CELL(1, 2), RESET, END_CHUNK,
                RESET, row_fmt, fg, CELL(1, 4), RESET, "[0:6]", END_CHUNK,
                RESET, "<1:1>", row_fmt, fg, FIRST_CELL(2), RESET, END_CHUNK,
                RESET, row_fmt, fg, TWO_CELL(2, 2), RESET, END_CHUNK,
                RESET, row_fmt, fg, CELL(2, 4), RESET, "[1:2]", END_CHUNK,
                RESET, "<2:9>", row_fmt, fg, FIRST_CELL(3), RESET, END_CHUNK,
                RESET, row_fmt, fg, TWO_CELL(3, 2), RESET, END_CHUNK,
                RESET, row_fmt, fg, CELL(3, 4), RESET, "[2:10]", END_CHUNK,
                NULL))
                return 1;
            continue;
        }

        if (EXPECT_FRAGMENTS_LEN(42,
                RESET, "<0:5>", row_fmt, fg, FIRST_CELL(1), CELL(1, 2), RESET))
            return 1;

        if (chunk_size <= 49) {
            if (expect_chunked_output(ctx, &capture, what,
                RESET, "<0:5>", row_fmt, fg, FIRST_CELL(1), CELL(1, 2), RESET,
                END_CHUNK,
                RESET, row_fmt, fg, TWO_CELL(1, 3), RESET, "[0:6]", END_CHUNK,
                RESET, "<1:1>", row_fmt, fg, FIRST_CELL(2), CELL(2, 2), RESET,
                END_CHUNK,
                RESET, row_fmt, fg, TWO_CELL(2, 3), RESET, "[1:2]", END_CHUNK,
                RESET, "<2:9>", row_fmt, fg, FIRST_CELL(3), CELL(3, 2), RESET,
                END_CHUNK,
                RESET, row_fmt, fg, TWO_CELL(3, 3), RESET, "[2:10]", END_CHUNK,
                NULL))
                return 1;
            continue;
        }

        if (EXPECT_FRAGMENTS_LEN(50,
                RESET, "<0:5>", row_fmt, fg, FIRST_CELL(1), TWO_CELL(1, 2), RESET))
            return 1;

        if (chunk_size <= 57) {
            if (expect_chunked_output(ctx, &capture, what,
                RESET, "<0:5>", row_fmt, fg, FIRST_CELL(1), TWO_CELL(1, 2), RESET,
                END_CHUNK,
                RESET, row_fmt, fg, CELL(1, 4), RESET, "[0:6]", END_CHUNK,
                RESET, "<1:1>", row_fmt, fg, FIRST_CELL(2), TWO_CELL(2, 2), RESET,
                END_CHUNK,
                RESET, row_fmt, fg, CELL(2, 4), RESET, "[1:2]", END_CHUNK,
                RESET, "<2:9>", row_fmt, fg, FIRST_CELL(3), TWO_CELL(3, 2), RESET,
                END_CHUNK,
                RESET, row_fmt, fg, CELL(3, 4), RESET, "[2:10]", END_CHUNK,
                NULL))
                return 1;
            continue;
        }

        if (EXPECT_FRAGMENTS_LEN(58,
                RESET, "<0:5>", row_fmt, fg, FIRST_CELL(1), THREE_CELL(1, 2), RESET))
            return 1;

        if (chunk_size <= 62) {
            if (expect_chunked_output(ctx, &capture, what,
                RESET, "<0:5>", row_fmt, fg, FIRST_CELL(1), THREE_CELL(1, 2), RESET,
                END_CHUNK,
                "[0:6]", END_CHUNK,
                RESET, "<1:1>", row_fmt, fg, FIRST_CELL(2), THREE_CELL(2, 2), RESET,
                END_CHUNK,
                "[1:2]", END_CHUNK,
                RESET, "<2:9>", row_fmt, fg, FIRST_CELL(3), THREE_CELL(3, 2), RESET,
                END_CHUNK,
                "[2:10]", END_CHUNK,
                NULL))
                return 1;
            continue;
        }

        if (EXPECT_FRAGMENTS_LEN(63, ROW("<0:5>", 1, "[0:6]")))
            return 1;

        if (chunk_size == 63) {
            if (expect_chunked_output(ctx, &capture, what,
                ROW("<0:5>", 1, "[0:6]"), END_CHUNK,
                ROW("<1:1>", 2, "[1:2]"), END_CHUNK,
                RESET, "<2:9>", row_fmt, fg, FIRST_CELL(3), THREE_CELL(3, 2),
                RESET, END_CHUNK,
                "[2:10]", END_CHUNK,
                NULL))
                return 1;
            continue;
        }

        if (chunk_size <= 125) {
            if (expect_chunked_output(ctx, &capture, what,
                ROW("<0:5>", 1, "[0:6]"), END_CHUNK,
                ROW("<1:1>", 2, "[1:2]"), END_CHUNK,
                ROW("<2:9>", 3, "[2:10]"), END_CHUNK,
                NULL))
                return 1;
            continue;
        }

        if (EXPECT_FRAGMENTS_LEN(126,
                                 ROW("<0:5>", 1, "[0:6]"),
                                 ROW("<1:1>", 2, "[1:2]")))
            return 1;

        if (chunk_size <= 189) {
            if (expect_chunked_output(ctx, &capture, what,
                ROW("<0:5>", 1, "[0:6]"),
                ROW("<1:1>", 2, "[1:2]"), END_CHUNK,
                ROW("<2:9>", 3, "[2:10]"), END_CHUNK,
                NULL))
                return 1;
            continue;
        }

        if (EXPECT_FRAGMENTS_LEN(190,
                                 ROW("<0:5>", 1, "[0:6]"),
                                 ROW("<1:1>", 2, "[1:2]"),
                                 ROW("<2:9>", 3, "[2:10]")))
            return 1;

        if (expect_chunked_output(ctx, &capture, what,
            ROW("<0:5>", 1, "[0:6]"),
            ROW("<1:1>", 2, "[1:2]"),
            ROW("<2:9>", 3, "[2:10]"), END_CHUNK,
            NULL))
            return 1;
        // clang-format on
    }

    return 0;

#undef ROW
#undef THREE_CELL
#undef TWO_CELL
#undef CELL
#undef FIRST_CELL
}

// Check exact chunking steps for ANSI output with positioning and no user
// formatting.
static int test_positioned_ansi_chunk_size_steps_exact(TestContext *ctx) {
    Placeholder placeholder = {
        .image_id = 7,
        .placement_id = 0,
        .rect = {.start_col = 0, .start_row = 0, .end_col = 2, .end_row = 2},
    };
    PlaceholderOptions options = placeholder_options_default();
    const char *fg = "\033[38;5;7m";
    Capture capture = {0};
    PlaceholderError error;
    const size_t max_chunk_size = 128;

#define CELL(row, col) PLACE, d(row), d(col)
#define TWO_CELL(row, col) CELL(row, col), CELL(row, (col) + 1)
#define ROW(start, row, end) RESET, start, fg, TWO_CELL(row, 1), RESET, end

    options.positioner = (PlaceholderPositioner){.func = custom_positioner};

    for (size_t chunk_size = 1; chunk_size <= max_chunk_size; ++chunk_size) {
        char what[64];

        snprintf(what, sizeof(what), "positioned chunk size %zu", chunk_size);
        options.chunk_size = chunk_size;
        capture = (Capture){0};
        error = placeholder_write(&placeholder, &options,
                                  capture_as_writer(&capture));

        if (chunk_size <= 24) {
            if (expect_error(ctx, error, PLACEHOLDER_CHUNK_TOO_SMALL, what))
                return 1;
            continue;
        }

        if (expect_error(ctx, error, PLACEHOLDER_OK, what) ||
            expect_chunk_lengths_at_most(ctx, &capture, chunk_size, what))
            return 1;

        if (EXPECT_FRAGMENTS_LEN(25, RESET, fg, CELL(1, 1), RESET))
            return 1;

        // clang-format off
        if (chunk_size <= 29) {
            if (expect_chunked_output(ctx, &capture, what,
                RESET, "<0:5>", END_CHUNK,
                RESET, fg, CELL(1, 1), RESET, END_CHUNK,
                RESET, fg, CELL(1, 2), RESET, END_CHUNK,
                "[0:6]", END_CHUNK,
                RESET, "<1:9>", END_CHUNK,
                RESET, fg, CELL(2, 1), RESET, END_CHUNK,
                RESET, fg, CELL(2, 2), RESET, END_CHUNK,
                "[1:10]", END_CHUNK,
                NULL))
                return 1;
            continue;
        }

        if (EXPECT_FRAGMENTS_LEN(30,
                                 RESET, "<0:5>", fg, CELL(1, 1), RESET))
            return 1;

        if (chunk_size == 30) {
            if (expect_chunked_output(ctx, &capture, what,
                RESET, "<0:5>", fg, CELL(1, 1), RESET, END_CHUNK,
                RESET, fg, CELL(1, 2), RESET, "[0:6]", END_CHUNK,
                RESET, "<1:9>", fg, CELL(2, 1), RESET, END_CHUNK,
                RESET, fg, CELL(2, 2), RESET, END_CHUNK,
                "[1:10]", END_CHUNK,
                NULL))
                return 1;
            continue;
        }

        if (EXPECT_FRAGMENTS_LEN(31,
                                 RESET, fg, CELL(2, 2), RESET, "[1:10]"))
            return 1;

        if (chunk_size <= 37) {
            if (expect_chunked_output(ctx, &capture, what,
                RESET, "<0:5>", fg, CELL(1, 1), RESET, END_CHUNK,
                RESET, fg, CELL(1, 2), RESET, "[0:6]", END_CHUNK,
                RESET, "<1:9>", fg, CELL(2, 1), RESET, END_CHUNK,
                RESET, fg, CELL(2, 2), RESET, "[1:10]", END_CHUNK,
                NULL))
                return 1;
            continue;
        }

        if (EXPECT_FRAGMENTS_LEN(38,
                                 RESET, "<0:5>", fg, TWO_CELL(1, 1), RESET))
            return 1;

        if (chunk_size <= 42) {
            if (expect_chunked_output(ctx, &capture, what,
                RESET, "<0:5>", fg, TWO_CELL(1, 1), RESET, END_CHUNK,
                "[0:6]", END_CHUNK,
                RESET, "<1:9>", fg, TWO_CELL(2, 1), RESET, END_CHUNK,
                "[1:10]", END_CHUNK,
                NULL))
                return 1;
            continue;
        }

        if (EXPECT_FRAGMENTS_LEN(43, ROW("<0:5>", 1, "[0:6]")))
            return 1;

        if (chunk_size == 43) {
            if (expect_chunked_output(ctx, &capture, what,
                ROW("<0:5>", 1, "[0:6]"), END_CHUNK,
                RESET, "<1:9>", fg, TWO_CELL(2, 1), RESET, END_CHUNK,
                "[1:10]", END_CHUNK,
                NULL))
                return 1;
            continue;
        }

        if (EXPECT_FRAGMENTS_LEN(44, ROW("<1:9>", 2, "[1:10]")))
            return 1;

        if (chunk_size <= 86) {
            if (expect_chunked_output(ctx, &capture, what,
                ROW("<0:5>", 1, "[0:6]"), END_CHUNK,
                ROW("<1:9>", 2, "[1:10]"), END_CHUNK,
                NULL))
                return 1;
            continue;
        }

        if (EXPECT_FRAGMENTS_LEN(87,
                                 ROW("<0:5>", 1, "[0:6]"),
                                 ROW("<1:9>", 2, "[1:10]")))
            return 1;

        if (expect_chunked_output(ctx, &capture, what,
            ROW("<0:5>", 1, "[0:6]"),
            ROW("<1:9>", 2, "[1:10]"), END_CHUNK,
            NULL))
            return 1;
        // clang-format on
    }

    return 0;

#undef ROW
#undef TWO_CELL
#undef CELL
}

// Check exact chunking steps for grapheme-only output with per-cell formatting.
// This keeps ANSI reset/color reservations out of the equation while still
// exercising user formatting that must be regenerated after chunk retries.
static int
test_grapheme_only_cell_format_chunk_size_steps_exact(TestContext *ctx) {
    Placeholder placeholder = {
        .image_id = 7,
        .placement_id = 0,
        .rect = {.start_col = 0, .start_row = 0, .end_col = 4, .end_row = 2},
    };
    FixedLengthFormatContext cell_format = {.len = 2};
    PlaceholderOptions options = placeholder_options_default();
    const char *cell_fmt = "xx";
    Capture capture = {0};
    PlaceholderError error;
    const size_t max_chunk_size = 128;

#define CELL(row, col) cell_fmt, PLACE, d(row), d(col)
#define TWO_CELL(row, col) CELL(row, col), CELL(row, (col) + 1)
#define THREE_CELL(row, col) TWO_CELL(row, col), CELL(row, (col) + 2)
#define FOUR_CELL(row, col) TWO_CELL(row, col), TWO_CELL(row, (col) + 2)
#define ROW(start, row, end) start, FOUR_CELL(row, 1), end

    options.grapheme_only = true;
    options.format =
        placeholder_format_dynamic_cell(fixed_length_format, &cell_format);
    options.positioner = (PlaceholderPositioner){.func = custom_positioner};

    for (size_t chunk_size = 1; chunk_size <= max_chunk_size; ++chunk_size) {
        char what[64];

        snprintf(what, sizeof(what), "grapheme chunk size %zu", chunk_size);
        options.chunk_size = chunk_size;
        capture = (Capture){0};
        error = placeholder_write(&placeholder, &options,
                                  capture_as_writer(&capture));

        if (chunk_size <= 9) {
            if (expect_error(ctx, error, PLACEHOLDER_CHUNK_TOO_SMALL, what))
                return 1;
            continue;
        }

        if (expect_error(ctx, error, PLACEHOLDER_OK, what) ||
            expect_chunk_lengths_at_most(ctx, &capture, chunk_size, what))
            return 1;

        if (EXPECT_FRAGMENTS_LEN(10, CELL(1, 1)))
            return 1;

        // clang-format off
        if (chunk_size <= 14) {
            if (expect_chunked_output(ctx, &capture, what,
                "<0:5>", END_CHUNK,
                CELL(1, 1), END_CHUNK,
                CELL(1, 2), END_CHUNK,
                CELL(1, 3), END_CHUNK,
                CELL(1, 4), END_CHUNK,
                "[0:6]", END_CHUNK,
                "<1:9>", END_CHUNK,
                CELL(2, 1), END_CHUNK,
                CELL(2, 2), END_CHUNK,
                CELL(2, 3), END_CHUNK,
                CELL(2, 4), END_CHUNK,
                "[1:10]", END_CHUNK,
                NULL))
                return 1;
            continue;
        }

        if (EXPECT_FRAGMENTS_LEN(15, "<0:5>", CELL(1, 1)))
            return 1;


        if (chunk_size == 15) {
            if (expect_chunked_output(ctx, &capture, what,
                "<0:5>", CELL(1, 1), END_CHUNK,
                CELL(1, 2), END_CHUNK,
                CELL(1, 3), END_CHUNK,
                CELL(1, 4), "[0:6]", END_CHUNK,
                "<1:9>", CELL(2, 1), END_CHUNK,
                CELL(2, 2), END_CHUNK,
                CELL(2, 3), END_CHUNK,
                CELL(2, 4), END_CHUNK,
                "[1:10]", END_CHUNK,
                NULL))
                return 1;
            continue;
        }

        if (EXPECT_FRAGMENTS_LEN(16, CELL(2, 4), "[1:10]"))
            return 1;

        if (chunk_size <= 19) {
            if (expect_chunked_output(ctx, &capture, what,
                "<0:5>", CELL(1, 1), END_CHUNK,
                CELL(1, 2), END_CHUNK,
                CELL(1, 3), END_CHUNK,
                CELL(1, 4), "[0:6]", END_CHUNK,
                "<1:9>", CELL(2, 1), END_CHUNK,
                CELL(2, 2), END_CHUNK,
                CELL(2, 3), END_CHUNK,
                CELL(2, 4), "[1:10]", END_CHUNK,
                NULL))
                return 1;
            continue;
        }

        if (EXPECT_FRAGMENTS_LEN(20, TWO_CELL(1, 2)))
            return 1;

        if (chunk_size <= 24) {
            if (expect_chunked_output(ctx, &capture, what,
                "<0:5>", CELL(1, 1), END_CHUNK,
                TWO_CELL(1, 2), END_CHUNK,
                CELL(1, 4), "[0:6]", END_CHUNK,
                "<1:9>", CELL(2, 1), END_CHUNK,
                TWO_CELL(2, 2), END_CHUNK,
                CELL(2, 4), "[1:10]", END_CHUNK,
                NULL))
                return 1;
            continue;
        }

        if (EXPECT_FRAGMENTS_LEN(25, "<0:5>", TWO_CELL(1, 1)))
            return 1;

        if (chunk_size == 25) {
            if (expect_chunked_output(ctx, &capture, what,
                "<0:5>", TWO_CELL(1, 1), END_CHUNK,
                TWO_CELL(1, 3), "[0:6]", END_CHUNK,
                "<1:9>", TWO_CELL(2, 1), END_CHUNK,
                TWO_CELL(2, 3), END_CHUNK,
                "[1:10]", END_CHUNK,
                NULL))
                return 1;
            continue;
        }

        if (EXPECT_FRAGMENTS_LEN(26, TWO_CELL(2, 3), "[1:10]"))
            return 1;

        if (chunk_size <= 34) {
            if (expect_chunked_output(ctx, &capture, what,
                "<0:5>", TWO_CELL(1, 1), END_CHUNK,
                TWO_CELL(1, 3), "[0:6]", END_CHUNK,
                "<1:9>", TWO_CELL(2, 1), END_CHUNK,
                TWO_CELL(2, 3), "[1:10]", END_CHUNK,
                NULL))
                return 1;
            continue;
        }

        if (EXPECT_FRAGMENTS_LEN(35, "<0:5>", THREE_CELL(1, 1)))
            return 1;

        if (chunk_size <= 44) {
            if (expect_chunked_output(ctx, &capture, what,
                "<0:5>", THREE_CELL(1, 1), END_CHUNK,
                CELL(1, 4), "[0:6]", END_CHUNK,
                "<1:9>", THREE_CELL(2, 1), END_CHUNK,
                CELL(2, 4), "[1:10]", END_CHUNK,
                NULL))
                return 1;
            continue;
        }

        if (EXPECT_FRAGMENTS_LEN(45, "<0:5>", FOUR_CELL(1, 1)))
            return 1;

        if (chunk_size <= 49) {
            if (expect_chunked_output(ctx, &capture, what,
                "<0:5>", FOUR_CELL(1, 1), END_CHUNK,
                "[0:6]", END_CHUNK,
                "<1:9>", FOUR_CELL(2, 1), END_CHUNK,
                "[1:10]", END_CHUNK,
                NULL))
                return 1;
            continue;
        }

        if (EXPECT_FRAGMENTS_LEN(50, ROW("<0:5>", 1, "[0:6]")))
            return 1;

        if (chunk_size == 50) {
            if (expect_chunked_output(ctx, &capture, what,
                ROW("<0:5>", 1, "[0:6]"), END_CHUNK,
                "<1:9>", FOUR_CELL(2, 1), END_CHUNK,
                "[1:10]", END_CHUNK,
                NULL))
                return 1;
            continue;
        }

        if (EXPECT_FRAGMENTS_LEN(51, ROW("<1:9>", 2, "[1:10]")))
            return 1;

        if (chunk_size <= 100) {
            if (expect_chunked_output(ctx, &capture, what,
                ROW("<0:5>", 1, "[0:6]"), END_CHUNK,
                ROW("<1:9>", 2, "[1:10]"), END_CHUNK,
                NULL))
                return 1;
            continue;
        }

        if (EXPECT_FRAGMENTS_LEN(101,
                                 ROW("<0:5>", 1, "[0:6]"),
                                 ROW("<1:9>", 2, "[1:10]")))
            return 1;

        if (expect_chunked_output(ctx, &capture, what,
            ROW("<0:5>", 1, "[0:6]"),
            ROW("<1:9>", 2, "[1:10]"), END_CHUNK,
            NULL))
            return 1;
        // clang-format on
    }

    return 0;

#undef ROW
#undef FOUR_CELL
#undef THREE_CELL
#undef TWO_CELL
#undef CELL
}

// Check large row, cell, and chunk-buffer fragments. These are not fixed
// staging-buffer failures: each too-small chunk must become renderable when the
// same options are retried with a larger chunk.
static int test_chunk_size_boundaries(TestContext *ctx) {
    Placeholder placeholder = base_placeholder();
    PlaceholderOptions options = placeholder_options_default();
    FixedLengthFormatContext format_len = {0};
    uint8_t row_diacritic_len = 0;
    uint8_t col_diacritic_len = 0;

    placeholder.image_id = 7;
    placeholder.placement_id = 0;
    placeholder.rect.end_col = 1;
    placeholder.rect.end_row = 1;
    rowcolumn_num_to_diacritic_utf8(1, &row_diacritic_len);
    rowcolumn_num_to_diacritic_utf8(1, &col_diacritic_len);

    size_t color_len = strlen("\033[38;5;7m");
    size_t row_start_reset_len = RESET_LEN;
    size_t cell_len =
        PLACEHOLDER_UTF8_LEN + row_diacritic_len + col_diacritic_len;

    // A large row format should be renderable with a chunk that can hold the
    // row start, the first cell, and the reserved reset suffix.
    char row_format[1300 + 1];
    fill_static_format(row_format, sizeof(row_format) - 1);
    options = placeholder_options_default();
    options.format = placeholder_format_static(row_format);
    size_t row_style_len = strlen(row_format) + color_len;
    size_t row_start_len = row_start_reset_len + row_style_len;
    size_t required_chunk_size = row_start_len + cell_len + RESET_LEN;
    if (expect_chunk_size_boundary(ctx, &placeholder, options,
                                   row_start_len + RESET_LEN - 1,
                                   required_chunk_size, "large row format"))
        return 1;

    // A large per-cell format should likewise be limited only by the configured
    // chunk size.
    format_len.len = 1200;
    options = placeholder_options_default();
    options.format =
        placeholder_format_dynamic_cell(fixed_length_format, &format_len);
    cell_len = format_len.len + PLACEHOLDER_UTF8_LEN + row_diacritic_len +
               col_diacritic_len;
    row_start_len = row_start_reset_len + color_len;
    required_chunk_size = RESET_LEN + color_len + cell_len + RESET_LEN;
    if (expect_chunk_size_boundary(ctx, &placeholder, options,
                                   required_chunk_size - 1, required_chunk_size,
                                   "large cell format"))
        return 1;

    // A direct static per-cell format exercises the static-format append retry
    // path. Checkerboard formatting wraps the static format in a dynamic
    // callback.
    char cell_format[300 + 1];
    fill_static_format(cell_format, sizeof(cell_format) - 1);
    PlaceholderFormat static_cell_format =
        placeholder_format_static(cell_format);
    static_cell_format.per_cell = true;
    options = placeholder_options_default();
    options.format = static_cell_format;
    cell_len = strlen(cell_format) + PLACEHOLDER_UTF8_LEN + row_diacritic_len +
               col_diacritic_len;
    required_chunk_size = RESET_LEN + color_len + cell_len + RESET_LEN;
    if (expect_chunk_size_boundary(
            ctx, &placeholder, options,
            RESET_LEN + color_len + strlen(cell_format) + RESET_LEN - 1,
            required_chunk_size, "static cell format larger than chunk"))
        return 1;
    if (expect_chunk_size_boundary(ctx, &placeholder, options,
                                   required_chunk_size - 1, required_chunk_size,
                                   "large static cell"))
        return 1;

    // Checkerboard formatting calls the nested static formatting callback
    // instead of using the direct static fast path above.
    PlaceholderAlternatingFormat static_cell_checkerboard = {
        .first = placeholder_format_static(cell_format),
        .second = placeholder_format_none(),
    };
    options = placeholder_options_default();
    options.format = placeholder_format_checkerboard(&static_cell_checkerboard);
    cell_len = strlen(cell_format) + PLACEHOLDER_UTF8_LEN + row_diacritic_len +
               col_diacritic_len;
    required_chunk_size = RESET_LEN + color_len + cell_len + RESET_LEN;
    if (expect_chunk_size_boundary(
            ctx, &placeholder, options,
            RESET_LEN + color_len + strlen(cell_format) + RESET_LEN - 1,
            required_chunk_size,
            "checkerboard static cell format larger than chunk"))
        return 1;
    if (expect_chunk_size_boundary(ctx, &placeholder, options,
                                   required_chunk_size - 1, required_chunk_size,
                                   "large checkerboard static cell"))
        return 1;

    // Multi-digit 256-color image IDs need more SGR bytes, so the minimum
    // renderable chunk grows with the decimal color length.
    placeholder.image_id = 123;
    color_len = strlen("\033[38;5;123m");
    options = placeholder_options_default();
    cell_len = PLACEHOLDER_UTF8_LEN + row_diacritic_len + col_diacritic_len;
    row_start_len = row_start_reset_len + color_len;
    required_chunk_size = row_start_len + cell_len + RESET_LEN;
    if (expect_chunk_size_boundary(
            ctx, &placeholder, options, row_start_len + RESET_LEN - 1,
            required_chunk_size, "multi-digit color format"))
        return 1;

    placeholder.image_id = 7;
    color_len = strlen("\033[38;5;7m");

    // Chunks larger than the stack buffer should use heap storage and obey the
    // same retry rules.
    format_len.len = 4200;
    options.format =
        placeholder_format_dynamic_cell(fixed_length_format, &format_len);
    cell_len = format_len.len + PLACEHOLDER_UTF8_LEN + row_diacritic_len +
               col_diacritic_len;
    required_chunk_size = RESET_LEN + color_len + cell_len + RESET_LEN;
    if (expect_chunk_size_boundary(ctx, &placeholder, options,
                                   required_chunk_size - 1, required_chunk_size,
                                   "heap chunk format"))
        return 1;

    // The row-start reset is not part of the active prefix after a mid-line
    // flush, but the initial row start still needs enough chunk space to fit.
    fill_static_format(row_format, 20);
    options = placeholder_options_default();
    options.format = placeholder_format_static(row_format);
    row_style_len = 20 + color_len;
    row_start_len = row_start_reset_len + row_style_len;
    cell_len = PLACEHOLDER_UTF8_LEN + row_diacritic_len + col_diacritic_len;
    required_chunk_size = row_start_len + cell_len + RESET_LEN;
    if (expect_chunk_size_boundary(
            ctx, &placeholder, options, row_start_len + RESET_LEN - 1,
            required_chunk_size, "row prefix larger than chunk"))
        return 1;

    // A valid cell can still be too large for the configured chunk because ANSI
    // chunks must have room for a reset, the active row style, the cell, and a
    // trailing reset.
    format_len.len = 64;
    options = placeholder_options_default();
    options.format =
        placeholder_format_dynamic_cell(fixed_length_format, &format_len);
    cell_len = format_len.len + PLACEHOLDER_UTF8_LEN + row_diacritic_len +
               col_diacritic_len;
    required_chunk_size = RESET_LEN + color_len + cell_len + RESET_LEN;
    if (expect_chunk_size_boundary(ctx, &placeholder, options,
                                   required_chunk_size - 1, required_chunk_size,
                                   "chunk-sized cell"))
        return 1;

    // If a row format fits its staging buffer but the chunk is too small to
    // hold the active row prefix plus the first cell, a larger chunk must fix
    // it.
    options = placeholder_options_default();
    options.grapheme_only = true;
    options.format = placeholder_format_static(row_format);
    cell_len = PLACEHOLDER_UTF8_LEN + row_diacritic_len + col_diacritic_len;
    required_chunk_size = 20 + cell_len;
    return expect_chunk_size_boundary(
        ctx, &placeholder, options, required_chunk_size - 1,
        required_chunk_size, "row style larger than chunk");
}

// Check chunk splitting, writer failures, and tiny chunk errors.
static int test_chunking_and_writers(TestContext *ctx) {
    Placeholder placeholder = base_placeholder();
    PlaceholderOptions options = placeholder_options_default();
    Capture capture = {0};

    uint8_t row_len = 0;
    uint8_t col_len = 0;
    rowcolumn_num_to_diacritic_utf8(1, &row_len);
    rowcolumn_num_to_diacritic_utf8(1, &col_len);

    size_t color_len = strlen("\033[38;5;7m");
    size_t cell_len = PLACEHOLDER_UTF8_LEN + row_len + col_len;

    // Small chunks should split a normal ANSI render into multiple writer calls
    // while resetting formatting before emitted linefeeds.
    options.chunk_size = 80;
    PlaceholderError error =
        placeholder_write(&placeholder, &options, capture_as_writer(&capture));
    if (expect_error(ctx, error, PLACEHOLDER_OK, "chunked write"))
        return 1;
    if (capture.call_count < 2) {
        fprintf(stderr, "%s: chunked write did not split output\n",
                ctx->test_name);
        return 1;
    }
    if (expect_newlines_preceded_by_reset(ctx, capture.data, capture.len))
        return 1;
    // The default final cursor position is bottom-right, so chunked default
    // output must not add a final linefeed after the last row.
    if (capture.len == 0 || capture.data[capture.len - 1] == '\n') {
        fprintf(stderr, "%s: chunked output ended with newline\n",
                ctx->test_name);
        return 1;
    }

    // Grapheme-only output may use a cell-sized chunk because no ANSI reset
    // padding or active row color prefix is needed.
    placeholder.image_id = 7;
    placeholder.placement_id = 0;
    placeholder.rect.end_col = 2;
    placeholder.rect.end_row = 1;
    options = placeholder_options_default();
    options.grapheme_only = true;
    options.chunk_size = cell_len;
    capture = (Capture){0};
    error =
        placeholder_write(&placeholder, &options, capture_as_writer(&capture));
    if (expect_error(ctx, error, PLACEHOLDER_OK, "grapheme-only cell chunking"))
        return 1;

    // When a grapheme-only chunk flushes after a complete line, a retained
    // position-only tail must stay in order and must not be discarded.
    placeholder = base_placeholder();
    options = placeholder_options_default();
    options.grapheme_only = true;
    options.positioner = (PlaceholderPositioner){.func = custom_positioner};
    options.chunk_size = 36;
    capture = (Capture){0};
    error =
        placeholder_write(&placeholder, &options, capture_as_writer(&capture));
    // clang-format off
    if (expect_error(ctx, error, PLACEHOLDER_OK,
                     "grapheme-only retained position tail") ||
        expect_output(ctx, capture.data, capture.len,
            "grapheme-only retained position tail",
            "<0:5>",
            PLACE, d(1), d(1), d(7), PLACE, d(1), d(2), d(7),
            "[0:6]",
            "<1:9>",
            PLACE, d(2), d(1), d(7), PLACE, d(2), d(2), d(7),
            "[1:10]",
            NULL))
        return 1;
    // clang-format on

    // A writer failure while flushing a mid-line ANSI chunk should be reported
    // as a write failure after the chunker has found a valid cell boundary.
    placeholder.image_id = 7;
    placeholder.placement_id = 0;
    placeholder.rect.end_col = 2;
    placeholder.rect.end_row = 1;
    options = placeholder_options_default();
    options.chunk_size = color_len + cell_len + 2 * RESET_LEN;

    capture = (Capture){.fail_after_calls = 1};
    error =
        placeholder_write(&placeholder, &options, capture_as_writer(&capture));
    if (expect_error(ctx, error, PLACEHOLDER_WRITE_FAILED,
                     "mid-line chunk write failure"))
        return 1;

    // A writer failure while flushing the reset before an inter-row linefeed
    // should also propagate as a write failure.
    placeholder.rect.end_col = 1;
    placeholder.rect.end_row = 2;
    options.chunk_size = RESET_LEN + color_len + cell_len + RESET_LEN;
    capture = (Capture){.fail_after_calls = 1};
    error =
        placeholder_write(&placeholder, &options, capture_as_writer(&capture));
    if (expect_error(ctx, error, PLACEHOLDER_WRITE_FAILED,
                     "row-end reset chunk write failure"))
        return 1;

    // If row-start position bytes are retained before the active prefix, a
    // retry flushes only that prefix first; writer failure there must
    // propagate.
    options = placeholder_options_default();
    options.positioner = (PlaceholderPositioner){.func = custom_positioner};
    options.chunk_size =
        RESET_LEN + strlen("<0:5>") + 1 + color_len + RESET_LEN;
    capture = (Capture){.fail_after_calls = 1};
    error =
        placeholder_write(&placeholder, &options, capture_as_writer(&capture));
    if (expect_error(ctx, error, PLACEHOLDER_WRITE_FAILED,
                     "position-prefix flush write failure"))
        return 1;

    // The public writer helper must pass full byte spans through successful
    // callbacks.
    const char raw_data[] = "abcdef";
    capture = (Capture){0};
    if (imgneko_writer_write(capture_as_writer(&capture), raw_data,
                             strlen(raw_data)) != 0) {
        fprintf(stderr, "%s: writer helper write failed\n", ctx->test_name);
        return 1;
    }
    if (capture.call_count != 1 || capture.len != strlen(raw_data) ||
        memcmp(capture.data, raw_data, strlen(raw_data)) != 0) {
        fprintf(stderr, "%s: writer helper did not write full span\n",
                ctx->test_name);
        return 1;
    }

    // A zero-length write with a NULL data pointer is valid and must not call
    // the underlying writer.
    capture = (Capture){0};
    if (imgneko_writer_write(capture_as_writer(&capture), NULL, 0) != 0 ||
        capture.call_count != 0) {
        fprintf(stderr, "%s: writer helper mishandled empty input\n",
                ctx->test_name);
        return 1;
    }

    if (imgneko_writer_write((ImgnekoWriter){0}, raw_data, strlen(raw_data)) !=
            -1 ||
        imgneko_writer_write(capture_as_writer(&capture), NULL, 1) != -1 ||
        imgneko_writer_write(imgneko_writer_fd(NULL), raw_data,
                             strlen(raw_data)) != -1) {
        fprintf(stderr, "%s: writer helper did not reject invalid inputs\n",
                ctx->test_name);
        return 1;
    }

    // A chunk that can fit either the color prefix or a cell, but not the
    // required combination, is still too small.
    if (MAX(cell_len, color_len) != color_len) {
        fprintf(stderr, "%s: bad reversed chunk size maximum\n",
                ctx->test_name);
        return 1;
    }
    options.chunk_size = MAX(color_len, cell_len) + RESET_LEN;
    error = placeholder_write(&placeholder, &options,
                              capture_as_writer(&(Capture){0}));
    if (expect_error(ctx, error, PLACEHOLDER_CHUNK_TOO_SMALL,
                     "chunk too small for row prefix and cell"))
        return 1;

    // The placeholder writer adapter must treat a callback failure as a write
    // failure.
    capture = (Capture){.fail_after_calls = 1};
    options = placeholder_options_default();
    placeholder = base_placeholder();
    error =
        placeholder_write(&placeholder, &options, capture_as_writer(&capture));
    if (expect_error(ctx, error, PLACEHOLDER_WRITE_FAILED, "failing writer"))
        return 1;

    // ANSI chunks smaller than the reset sequence cannot safely contain even an
    // empty formatting reset.
    options.chunk_size = 3;
    error = placeholder_write(&placeholder, &options,
                              capture_as_writer(&(Capture){0}));
    if (expect_error(ctx, error, PLACEHOLDER_CHUNK_TOO_SMALL, "tiny chunk"))
        return 1;

    // A reset-sized ANSI chunk is still too small because cell bytes need room
    // after the reserved reset padding.
    options.chunk_size = RESET_LEN;
    error = placeholder_write(&placeholder, &options,
                              capture_as_writer(&(Capture){0}));
    if (expect_error(ctx, error, PLACEHOLDER_CHUNK_TOO_SMALL,
                     "reset-only chunk"))
        return 1;

    // Grapheme-only output removes ANSI padding, but a one-byte chunk still
    // cannot hold a complete placeholder grapheme.
    options.grapheme_only = true;
    options.chunk_size = 1;
    error = placeholder_write(&placeholder, &options,
                              capture_as_writer(&(Capture){0}));
    return expect_error(ctx, error, PLACEHOLDER_CHUNK_TOO_SMALL,
                        "tiny grapheme chunk");
}

// Check public write error paths, defaulted positioners, custom positioner
// failures, and the file-descriptor writer wrapper.
static int test_write_edge_cases(TestContext *ctx) {
    Placeholder placeholder = base_placeholder();
    PlaceholderOptions options = placeholder_options_default();
    Capture capture = {0};

    if (expect_error(
            ctx, placeholder_write(NULL, &options, capture_as_writer(&capture)),
            PLACEHOLDER_INVALID_ARGUMENT, "null placeholder") ||
        expect_error(
            ctx, placeholder_write(&placeholder, &options, (ImgnekoWriter){0}),
            PLACEHOLDER_INVALID_ARGUMENT, "null writer"))
        return 1;

    placeholder.image_id = 0;
    if (expect_error(ctx,
                     placeholder_write(&placeholder, &options,
                                       capture_as_writer(&capture)),
                     PLACEHOLDER_INVALID_IMAGE_ID, "invalid write input"))
        return 1;

    placeholder = base_placeholder();
    options.positioner = (PlaceholderPositioner){0};
    if (expect_error(ctx,
                     placeholder_write(&placeholder, &options,
                                       capture_as_writer(&capture)),
                     PLACEHOLDER_OK, "defaulted positioner"))
        return 1;

    ConfiguredPositionerContext positioner = {
        .fail_flags = PLACEHOLDER_POSITION_LINE_START,
        .return_len = -1,
    };

    options = placeholder_options_default();
    options.positioner = (PlaceholderPositioner){
        .func = configured_positioner,
        .ctx = &positioner,
    };
    PlaceholderError error =
        placeholder_write(&placeholder, &options, capture_as_writer(&capture));
    if (expect_error(ctx, error, PLACEHOLDER_POSITION_FAILED,
                     "line-start positioner failure"))
        return 1;

    positioner.fail_flags = PLACEHOLDER_POSITION_LINE_END;
    capture = (Capture){0};
    error =
        placeholder_write(&placeholder, &options, capture_as_writer(&capture));
    if (expect_error(ctx, error, PLACEHOLDER_POSITION_FAILED,
                     "line-end positioner failure"))
        return 1;

    positioner.fail_flags = PLACEHOLDER_POSITION_LINE_START;
    positioner.return_len = 129;
    options.chunk_size = 64;
    if (expect_chunk_size_boundary(ctx, &placeholder, options, 64, 256,
                                   "large positioner output"))
        return 1;

    int fds[2];
    if (pipe(fds) != 0) {
        fprintf(stderr, "%s: pipe failed\n", ctx->test_name);
        return 1;
    }

    char fd_output[2048];
    error = placeholder_write_fd(&placeholder, NULL, fds[1]);
    close(fds[1]);
    ssize_t fd_len = read(fds[0], fd_output, sizeof(fd_output));
    close(fds[0]);
    if (expect_error(ctx, error, PLACEHOLDER_OK, "fd writer") || fd_len <= 0)
        return 1;

    if (pipe(fds) != 0) {
        fprintf(stderr, "%s: second pipe failed\n", ctx->test_name);
        return 1;
    }

    int fd = fds[1];
    error = placeholder_write(&placeholder, NULL, imgneko_writer_fd(&fd));
    close(fds[1]);
    fd_len = read(fds[0], fd_output, sizeof(fd_output));
    close(fds[0]);
    if (expect_error(ctx, error, PLACEHOLDER_OK, "fd writer factory") ||
        fd_len <= 0)
        return 1;

    return expect_error(ctx, placeholder_write_fd(&placeholder, NULL, -1),
                        PLACEHOLDER_WRITE_FAILED, "bad fd writer");
}

// Check exact buffer-length reporting across placeholder sizes and options.
static int test_write_to_buffer_lengths(TestContext *ctx) {
    Placeholder placeholder = base_placeholder();

    Placeholder wide_placeholder = base_placeholder();
    wide_placeholder.rect.end_col = 5;

    Placeholder tall_placeholder = base_placeholder();
    tall_placeholder.rect.end_row = 4;

    Placeholder offset_placeholder = base_placeholder();
    offset_placeholder.rect.start_col = 2;
    offset_placeholder.rect.end_col = 5;

    Placeholder max_columns_placeholder = base_placeholder();
    max_columns_placeholder.rect.end_col = ROWCOLUMN_DIACRITIC_MAX;
    max_columns_placeholder.rect.end_row = 1;

    Placeholder max_rows_placeholder = base_placeholder();
    max_rows_placeholder.rect.end_col = 1;
    max_rows_placeholder.rect.end_row = ROWCOLUMN_DIACRITIC_MAX;

    Placeholder max_rectangle_placeholder = base_placeholder();
    max_rectangle_placeholder.rect.end_col = ROWCOLUMN_DIACRITIC_MAX;
    max_rectangle_placeholder.rect.end_row = ROWCOLUMN_DIACRITIC_MAX;

    Placeholder max_offset_placeholder = base_placeholder();
    max_offset_placeholder.rect.start_col = ROWCOLUMN_DIACRITIC_MAX - 1;
    max_offset_placeholder.rect.start_row = ROWCOLUMN_DIACRITIC_MAX - 1;
    max_offset_placeholder.rect.end_col = ROWCOLUMN_DIACRITIC_MAX;
    max_offset_placeholder.rect.end_row = ROWCOLUMN_DIACRITIC_MAX;

    PlaceholderOptions grapheme_options = placeholder_options_default();
    grapheme_options.grapheme_only = true;

    PlaceholderOptions positioned_options = placeholder_options_default();
    positioned_options.positioner =
        (PlaceholderPositioner){.func = custom_positioner};

    PlaceholderOptions row_format_options = placeholder_options_default();
    row_format_options.format = placeholder_format_static("\033[48;5;12m");

    FixedLengthFormatContext cell_format = {.len = 3};
    PlaceholderOptions cell_format_options = placeholder_options_default();
    cell_format_options.format =
        placeholder_format_dynamic_cell(fixed_length_format, &cell_format);

    typedef struct BufferLengthPlaceholderCase {
        const char *what;
        Placeholder placeholder;
    } BufferLengthPlaceholderCase;

    typedef struct BufferLengthOptionsCase {
        const char *what;
        const PlaceholderOptions *options;
    } BufferLengthOptionsCase;

    const BufferLengthPlaceholderCase placeholders[] = {
        {
            .what = "base",
            .placeholder = placeholder,
        },
        {
            .what = "wide",
            .placeholder = wide_placeholder,
        },
        {
            .what = "tall",
            .placeholder = tall_placeholder,
        },
        {
            .what = "offset",
            .placeholder = offset_placeholder,
        },
        {
            .what = "max columns",
            .placeholder = max_columns_placeholder,
        },
        {
            .what = "max rows",
            .placeholder = max_rows_placeholder,
        },
        {
            .what = "max rectangle",
            .placeholder = max_rectangle_placeholder,
        },
        {
            .what = "max offset",
            .placeholder = max_offset_placeholder,
        },
    };

    const BufferLengthOptionsCase options[] = {
        {
            .what = "default",
            .options = NULL,
        },
        {
            .what = "grapheme-only",
            .options = &grapheme_options,
        },
        {
            .what = "positioned",
            .options = &positioned_options,
        },
        {
            .what = "row-format",
            .options = &row_format_options,
        },
        {
            .what = "cell-format",
            .options = &cell_format_options,
        },
    };

    // Exercise exact buffer sizing across different rectangle sizes and
    // formatting modes. Each case first obtains the required length from a
    // too-small render, then retries with exactly that many allocated bytes.
    for (size_t placeholder_i = 0; placeholder_i < ARRAY_SIZE(placeholders);
         ++placeholder_i) {
        for (size_t options_i = 0; options_i < ARRAY_SIZE(options);
             ++options_i) {
            char what[128];
            int len = snprintf(what, sizeof(what), "%s placeholder with %s",
                               placeholders[placeholder_i].what,
                               options[options_i].what);
            if (len < 0 || (size_t)len >= sizeof(what)) {
                fprintf(stderr, "%s: buffer length label did not fit\n",
                        ctx->test_name);
                return 1;
            }
            if (expect_buffer_len_round_trip(
                    ctx, &placeholders[placeholder_i].placeholder,
                    options[options_i].options, what))
                return 1;
        }
    }

    return 0;
}

// Check buffer-writing sizing and invalid arguments.
static int test_write_to_buffer(TestContext *ctx) {
    Placeholder placeholder = base_placeholder();
    char output[16];
    char large_output[512];
    size_t len = 0;

    PlaceholderError error = placeholder_write_to_buffer(
        &placeholder, NULL, output, sizeof(output), &len);
    if (expect_error(ctx, error, PLACEHOLDER_WRITE_FAILED, "small buffer"))
        return 1;
    if (len <= sizeof(output)) {
        fprintf(stderr, "%s: small buffer did not report required length\n",
                ctx->test_name);
        return 1;
    }

    if (expect_error(ctx,
                     placeholder_write_to_buffer(&placeholder, NULL,
                                                 large_output,
                                                 sizeof(large_output), NULL),
                     PLACEHOLDER_OK, "missing len_out") ||
        expect_error(ctx,
                     placeholder_write_to_buffer(&placeholder, NULL, output,
                                                 sizeof(output), NULL),
                     PLACEHOLDER_WRITE_FAILED,
                     "small buffer without len_out") ||
        expect_error(
            ctx, placeholder_write_to_buffer(&placeholder, NULL, NULL, 1, &len),
            PLACEHOLDER_INVALID_ARGUMENT, "missing output"))
        return 1;

    error = placeholder_write_to_buffer(&placeholder, NULL, NULL, 0, &len);
    if (expect_error(ctx, error, PLACEHOLDER_WRITE_FAILED,
                     "zero-capacity buffer"))
        return 1;

    return 0;
}

// Check public error strings, including the fallback for unknown values.
static int test_error_strings(TestContext *ctx) {
    const PlaceholderError errors[] = {
        PLACEHOLDER_OK,
        PLACEHOLDER_INVALID_ARGUMENT,
        PLACEHOLDER_INVALID_IMAGE_ID,
        PLACEHOLDER_INVALID_PLACEMENT_ID,
        PLACEHOLDER_INVALID_RECTANGLE,
        PLACEHOLDER_INVALID_MODE,
        PLACEHOLDER_INCOMPLETE_FIRST_COLUMN,
        PLACEHOLDER_UNREPRESENTABLE_CELL,
        PLACEHOLDER_UNREPRESENTABLE_COLUMN,
        PLACEHOLDER_CHUNK_TOO_SMALL,
        PLACEHOLDER_FORMAT_FAILED,
        PLACEHOLDER_POSITION_FAILED,
        PLACEHOLDER_WRITE_FAILED,
        (PlaceholderError)12345,
    };

    for (size_t i = 0; i < ARRAY_SIZE(errors); ++i) {
        const char *message = placeholder_error_string(errors[i]);
        if (message == NULL || message[0] == '\0') {
            fprintf(stderr, "%s: empty error string at %zu\n", ctx->test_name,
                    i);
            return 1;
        }
    }

    return 0;
}

// Register and run every placeholder subtest.
int main(int argc, char **argv) {
    const Subtest subtests[] = {
        PREFIXED_TEST(test_default_rendering),
        PREFIXED_TEST(test_modes_and_color_options),
        PREFIXED_TEST(test_user_formatting),
        PREFIXED_TEST(test_background_format_helpers),
        PREFIXED_TEST(test_positioners),
        PREFIXED_TEST(test_positioner_edge_cases),
        PREFIXED_TEST(test_grapheme_only),
        PREFIXED_TEST(test_unrepresentable_coordinates),
        PREFIXED_TEST(test_validation_errors),
        PREFIXED_TEST(test_chunk_size_steps_exact),
        PREFIXED_TEST(test_row_format_chunk_size_steps_exact),
        PREFIXED_TEST(test_positioned_ansi_chunk_size_steps_exact),
        PREFIXED_TEST(test_grapheme_only_cell_format_chunk_size_steps_exact),
        PREFIXED_TEST(test_chunk_size_boundaries),
        PREFIXED_TEST(test_chunking_and_writers),
        PREFIXED_TEST(test_write_edge_cases),
        PREFIXED_TEST(test_write_to_buffer_lengths),
        PREFIXED_TEST(test_write_to_buffer),
        PREFIXED_TEST(test_error_strings),
    };

    return run_subtests(argc, argv, subtests, ARRAY_SIZE(subtests));
}
