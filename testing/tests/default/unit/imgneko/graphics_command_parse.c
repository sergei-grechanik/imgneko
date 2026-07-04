// SPDX-License-Identifier: MIT-0

// Unit tests for parser behavior that is independent of command field mapping.

#include <stdio.h>
#include <string.h>

#include "imgneko/graphics_command.h"
#include "test_main.h"
#include "test_reader.h"
#include "util/common.h"
#include "util/string.h"

#define STR(text) (text), (sizeof(text) - 1)

// Compare a borrowed span against exact bytes, including embedded NUL bytes.
//
// `ctx`
//     Test context used for diagnostics.
// `actual`
//     Borrowed span produced by the parser.
// `expected_data`, `expected_len`
//     Exact expected byte span.
// `description`
//     Human-readable description of the span.
static int expect_span(TestContext *ctx, StrSpan actual,
                       const char *expected_data, size_t expected_len,
                       const char *description) {
    StrSpan expected = str_span(expected_data, expected_len);
    if (str_span_equal(actual, expected))
        return 0;

    if (actual.len != expected.len) {
        fprintf(stderr, "%s: %s: expected length %zu, got %zu\n",
                ctx->test_name, description, expected.len, actual.len);
        return 1;
    }

    fprintf(stderr, "%s: %s: span contents differ\n", ctx->test_name,
            description);
    return 1;
}

// Parse a default-framed command expected to succeed without a diagnostic.
//
// `ctx`
//     Test context used for diagnostics.
// `data`, `len`
//     Complete serialized command bytes.
// `parsed_out`
//     Output parameter receiving the parsed command and borrowed spans.
// `description`
//     Human-readable description of the parse operation.
static int parse_default_or_fail(TestContext *ctx, const char *data, size_t len,
                                 ImgnekoParsedCommand *parsed_out,
                                 const char *description) {
    ImgnekoCommandParseOptions options = {0};
    char message[256];
    ImgnekoErrorDetail detail = {
        .message = message,
        .message_cap = sizeof(message),
        .message_len = SIZE_MAX,
    };

    memset(message, 'x', sizeof(message));
    ImgnekoCommandParseError status =
        imgneko_command_parse(data, len, &options, parsed_out, &detail);
    if (status != IMGNEKO_COMMAND_PARSE_OK) {
        fprintf(stderr, "%s: %s: parse returned %d (%.*s)\n", ctx->test_name,
                description, status, (int)sizeof(message), message);
        return 1;
    }
    if (detail.message_len != 0 || message[0] != '\0')
        return test_fail_message(ctx, "successful parse left a diagnostic");

    return 0;
}

// Verify framing and payload separators produce exact borrowed views into the
// caller's input, including opaque payload bytes and embedded semicolons.
static int test_borrowed_spans(TestContext *ctx) {
    static const char no_payload[] = "\x1b_Ga=p,i=7\x1b\\";
    ImgnekoParsedCommand parsed = {0};

    if (parse_default_or_fail(ctx, STR(no_payload), &parsed,
                              "command without payload separator"))
        return 1;
    if (parsed.header != no_payload + 3 || parsed.payload != NULL ||
        parsed.payload_len != 0)
        return test_fail_message(ctx, "no-payload borrowed pointers are wrong");
    if (expect_span(ctx, str_span(parsed.header, parsed.header_len),
                    STR("a=p,i=7"), "no-payload header"))
        return 1;

    static const char empty_payload[] = "\x1b_Ga=t,t=d;\x1b\\";
    if (parse_default_or_fail(ctx, STR(empty_payload), &parsed,
                              "explicit empty payload"))
        return 1;
    const char *empty_payload_start =
        empty_payload + sizeof("\x1b_Ga=t,t=d;") - 1;
    if (parsed.payload != empty_payload_start || parsed.payload_len != 0)
        return test_fail_message(ctx, "empty-payload pointer is wrong");

    static const char opaque_payload[] = "\x1b_Ga=t,t=d;AA;B\0C\x1b\\";
    if (parse_default_or_fail(ctx, opaque_payload, sizeof(opaque_payload) - 1,
                              &parsed, "opaque payload"))
        return 1;
    const char *opaque_payload_start =
        opaque_payload + sizeof("\x1b_Ga=t,t=d;") - 1;
    if (parsed.payload != opaque_payload_start)
        return test_fail_message(ctx, "opaque-payload pointer is wrong");
    if (expect_span(ctx, str_span(parsed.payload, parsed.payload_len),
                    "AA;B\0C", 6, "opaque payload bytes"))
        return 1;

    return 0;
}

// Verify custom and explicitly empty framing remain independent for the start
// and end sequences.
static int test_custom_framing(TestContext *ctx) {
    static const char custom_input[] = "BEGINa=t,t=d;XYZEND";
    ImgnekoCommandParseOptions options = {
        .start_sequence = "BEGIN",
        .start_sequence_len = 5,
        .end_sequence = "END",
        .end_sequence_len = 3,
    };
    ImgnekoParsedCommand parsed = {0};
    char message[128];
    ImgnekoErrorDetail detail = {
        .message = message,
        .message_cap = sizeof(message),
    };

    ImgnekoCommandParseError status =
        imgneko_command_parse(STR(custom_input), &options, &parsed, &detail);
    if (test_expect_status(ctx, status, IMGNEKO_COMMAND_PARSE_OK,
                           "custom framing") ||
        parsed.command.kind != IMGNEKO_COMMAND_TRANSMIT ||
        parsed.command.data.transmit.transmission.medium !=
            IMGNEKO_TRANSMISSION_MEDIUM_DIRECT ||
        expect_span(ctx, str_span(parsed.header, parsed.header_len),
                    STR("a=t,t=d"), "custom-framed header") ||
        expect_span(ctx, str_span(parsed.payload, parsed.payload_len),
                    STR("XYZ"), "custom-framed payload"))
        return 1;

    static const char unframed_input[] = "a=p,i=8";
    options = (ImgnekoCommandParseOptions){
        .start_sequence = "",
        .end_sequence = "",
    };
    status =
        imgneko_command_parse(STR(unframed_input), &options, &parsed, NULL);
    if (test_expect_status(ctx, status, IMGNEKO_COMMAND_PARSE_OK,
                           "explicit empty framing") ||
        parsed.command.kind != IMGNEKO_COMMAND_PUT ||
        parsed.command.image_id != 8)
        return 1;

    return 0;
}

// Verify parsing never sets omission flags when the corresponding key cannot
// be omitted for the parsed command.
static int test_irrelevant_omission_flags(TestContext *ctx) {
    ImgnekoParsedCommand parsed = {0};
    static const char put_input[] = "\x1b_Ga=p,i=1\x1b\\";

    if (parse_default_or_fail(ctx, STR(put_input), &parsed,
                              "non-transmit action"))
        return 1;
    if (parsed.command.kind_implicit)
        return test_fail_message(ctx, "non-transmit action marked implicit");

    static const char file_input[] = "\x1b_Ga=t,t=f\x1b\\";
    if (parse_default_or_fail(ctx, STR(file_input), &parsed,
                              "non-direct medium"))
        return 1;
    ImgnekoTransmission *transmission =
        imgneko_command_get_transmission(&parsed.command);
    if (transmission == NULL || transmission->medium_implicit)
        return test_fail_message(ctx, "non-direct medium marked implicit");

    static const char delete_input[] = "\x1b_Ga=d,d=A\x1b\\";
    if (parse_default_or_fail(ctx, STR(delete_input), &parsed,
                              "uppercase delete target"))
        return 1;
    if (parsed.command.data.delete_cmd.target_implicit)
        return test_fail_message(ctx,
                                 "uppercase delete target marked implicit");

    return 0;
}

// Verify a character field containing an embedded NUL is unrepresentable, can
// be discarded with the same policy used for malformed numeric fields, and
// reports the strict diagnostic as an optional warning.
static int test_unrepresentable_character_value(TestContext *ctx) {
    static const char input[] = {'a', '=', 't', ',', 't', '=',
                                 'd', ',', 'o', '=', '\0'};
    ImgnekoCommandParseOptions options = {
        .start_sequence = "",
        .end_sequence = "",
    };
    ImgnekoParsedCommand parsed;
    char message[128];
    char strict_message[128];
    ImgnekoErrorDetail detail = {
        .message = message,
        .message_cap = sizeof(message),
    };

    ImgnekoCommandParseError status =
        imgneko_command_parse(input, sizeof(input), &options, &parsed, &detail);
    if (test_expect_status(ctx, status, IMGNEKO_COMMAND_PARSE_FAILED,
                           "embedded NUL character") ||
        strstr(message, "payload compression is not representable (o)") == NULL)
        return test_fail_message(
            ctx, "unrepresentable character diagnostic is wrong");
    memcpy(strict_message, message, detail.message_len + 1);

    options.flags = IMGNEKO_COMMAND_PARSE_DROP_UNREPRESENTABLE_VALUES;
    status =
        imgneko_command_parse(input, sizeof(input), &options, &parsed, &detail);
    if (test_expect_status(ctx, status, IMGNEKO_COMMAND_PARSE_OK,
                           "dropped embedded NUL character") ||
        detail.message_len != strlen(strict_message) ||
        strcmp(message, strict_message) != 0 ||
        parsed.command.data.transmit.transmission.compression !=
            IMGNEKO_PAYLOAD_COMPRESSION_NONE)
        return 1;

    status = imgneko_command_parse(input, sizeof(input), &options, &parsed,
                                   /*error_out=*/NULL);
    if (test_expect_status(ctx, status, IMGNEKO_COMMAND_PARSE_OK,
                           "dropped warning without an error sink"))
        return 1;

    return 0;
}

// Verify multiple relaxed diagnostics are newline-separated and retain their
// complete required length when caller-owned storage truncates the text.
static int test_appended_warning_truncation(TestContext *ctx) {
    static const char input[] = "a=p,first=1,second=2";
    static const char expected[] = "unknown graphics command key 'first'\n"
                                   "unknown graphics command key 'second'";
    char message[24];
    ImgnekoErrorDetail detail = {
        .message = message,
        .message_cap = sizeof(message),
        .message_len = SIZE_MAX,
    };
    ImgnekoCommandParseOptions options = {
        .start_sequence = "",
        .end_sequence = "",
        .flags = IMGNEKO_COMMAND_PARSE_DROP_UNKNOWN_KEYS,
    };
    ImgnekoParsedCommand parsed;

    memset(message, 'x', sizeof(message));
    ImgnekoCommandParseError status = imgneko_command_parse(
        input, sizeof(input) - 1, &options, &parsed, &detail);
    if (test_expect_status(ctx, status, IMGNEKO_COMMAND_PARSE_OK,
                           "multiple relaxed warnings"))
        return 1;
    if (detail.message_len != sizeof(expected) - 1)
        return test_fail_message(ctx, "combined warning length is wrong");
    if (memcmp(message, expected, sizeof(message) - 1) != 0 ||
        message[sizeof(message) - 1] != '\0')
        return test_fail_message(ctx, "truncated combined warning is wrong");

    // Size-only detail output must account for every warning and separator
    // without requiring message storage.
    ImgnekoErrorDetail size_detail = {
        .message = NULL,
        .message_cap = 0,
        .message_len = SIZE_MAX,
    };
    status = imgneko_command_parse(input, sizeof(input) - 1, &options, &parsed,
                                   &size_detail);
    if (test_expect_status(ctx, status, IMGNEKO_COMMAND_PARSE_OK,
                           "size-only relaxed warnings") ||
        size_detail.message_len != sizeof(expected) - 1)
        return test_fail_message(ctx, "size-only warning length is wrong");

    return 0;
}

// Verify invalid parser arguments and every independent framing mismatch.
static int test_parser_argument_and_framing_errors(TestContext *ctx) {
    char message[128];
    ImgnekoErrorDetail detail = {
        .message = message,
        .message_cap = sizeof(message),
    };
    ImgnekoErrorDetail invalid_detail = {
        .message = NULL,
        .message_cap = 1,
    };
    ImgnekoCommandParseOptions options = {
        .start_sequence = "",
        .end_sequence = "",
    };
    ImgnekoParsedCommand parsed;

    if (test_expect_status(
            ctx,
            imgneko_command_parse("", 0, &options, &parsed, &invalid_detail),
            IMGNEKO_COMMAND_PARSE_FAILED, "invalid parse detail") ||
        test_expect_status(
            ctx, imgneko_command_parse(NULL, 1, &options, &parsed, &detail),
            IMGNEKO_COMMAND_PARSE_FAILED, "NULL parse data") ||
        test_expect_status(
            ctx, imgneko_command_parse("", 0, NULL, &parsed, &detail),
            IMGNEKO_COMMAND_PARSE_FAILED, "NULL parse options") ||
        test_expect_status(
            ctx, imgneko_command_parse("", 0, &options, NULL, &detail),
            IMGNEKO_COMMAND_PARSE_FAILED, "NULL parsed output"))
        return 1;

    options = (ImgnekoCommandParseOptions){
        .start_sequence_len = 1,
        .end_sequence = "",
    };
    if (test_expect_status(
            ctx, imgneko_command_parse("", 0, &options, &parsed, &detail),
            IMGNEKO_COMMAND_PARSE_FAILED, "NULL start framing"))
        return 1;
    options = (ImgnekoCommandParseOptions){
        .start_sequence = "",
        .end_sequence_len = 1,
    };
    if (test_expect_status(
            ctx, imgneko_command_parse("", 0, &options, &parsed, &detail),
            IMGNEKO_COMMAND_PARSE_FAILED, "NULL end framing"))
        return 1;

    options = (ImgnekoCommandParseOptions){
        .start_sequence = "ab",
        .start_sequence_len = 2,
        .end_sequence = "",
    };
    if (test_expect_status(
            ctx, imgneko_command_parse("a", 1, &options, &parsed, &detail),
            IMGNEKO_COMMAND_PARSE_FAILED, "oversized start framing"))
        return 1;
    options = (ImgnekoCommandParseOptions){
        .start_sequence = "",
        .end_sequence = "ab",
        .end_sequence_len = 2,
    };
    if (test_expect_status(
            ctx, imgneko_command_parse("a", 1, &options, &parsed, &detail),
            IMGNEKO_COMMAND_PARSE_FAILED, "oversized end framing"))
        return 1;
    options = (ImgnekoCommandParseOptions){
        .start_sequence = "b",
        .start_sequence_len = 1,
        .end_sequence = "",
    };
    if (test_expect_status(
            ctx, imgneko_command_parse("a", 1, &options, &parsed, &detail),
            IMGNEKO_COMMAND_PARSE_FAILED, "mismatched start framing"))
        return 1;
    options = (ImgnekoCommandParseOptions){
        .start_sequence = "",
        .end_sequence = "b",
        .end_sequence_len = 1,
    };
    if (test_expect_status(
            ctx, imgneko_command_parse("a", 1, &options, &parsed, &detail),
            IMGNEKO_COMMAND_PARSE_FAILED, "mismatched end framing"))
        return 1;

    options = (ImgnekoCommandParseOptions){
        .start_sequence = "",
        .end_sequence = "",
    };
    if (test_expect_status(
            ctx, imgneko_command_parse(NULL, 0, &options, &parsed, &detail),
            IMGNEKO_COMMAND_PARSE_OK, "NULL empty parse input") ||
        parsed.header != NULL || parsed.header_len != 0)
        return test_fail_message(ctx, "NULL empty parse produced a header");

    return 0;
}

int main(int argc, char **argv) {
    const Subtest subtests[] = {
        PREFIXED_TEST(test_appended_warning_truncation),
        PREFIXED_TEST(test_borrowed_spans),
        PREFIXED_TEST(test_custom_framing),
        PREFIXED_TEST(test_irrelevant_omission_flags),
        PREFIXED_TEST(test_parser_argument_and_framing_errors),
        PREFIXED_TEST(test_unrepresentable_character_value),
    };

    return run_subtests(argc, argv, subtests, ARRAY_SIZE(subtests));
}
