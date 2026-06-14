// SPDX-License-Identifier: MIT-0

// Command-line entry point for imgneko.

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "build_info.h"
#include "imgneko/placeholder.h"
#include "util/options.h"

OPT_DEFINE_WRAPPER_STRUCT(OptUint32, uint32_t);

// Parsed value for the `--place CxR` size option. The CLI spelling uses
// columns first and rows second, matching the width-by-height convention.
typedef struct PlaceSize {
    uint32_t cols;
    uint32_t rows;
} PlaceSize;

OPT_DEFINE_WRAPPER_STRUCT(OptPlaceSize, PlaceSize);
OPT_DEFINE_WRAPPER_STRUCT(OptPlaceholderMode, PlaceholderMode);

// Parse an unsigned 32-bit decimal integer from an option value.
static bool parse_uint32_option(void *value, const char *text, size_t text_len,
                                String *error_out) {
    int64_t parsed = 0;

    if (!opt_parse_int64_span(text, text_len, &parsed) || parsed < 0)
        return opt_parse_error(error_out,
                               "expected a base-10 unsigned integer");
    if (parsed > UINT32_MAX)
        return opt_parse_error(error_out, "expected a 32-bit unsigned integer");

    *(uint32_t *)value = (uint32_t)parsed;
    return true;
}

// Parse an image or placement ID from an option value.
static bool parse_id_option(void *value, const char *text, size_t text_len,
                            String *error_out) {
    uint64_t parsed = 0;

    if (!opt_parse_uint64_hex_or_decimal_span(text, text_len, &parsed))
        return opt_parse_error(
            error_out, "expected an unsigned decimal or hexadecimal integer");
    if (parsed > UINT32_MAX)
        return opt_parse_error(error_out, "expected a 32-bit unsigned integer");

    *(uint32_t *)value = (uint32_t)parsed;
    return true;
}

// Parse a positive unsigned 32-bit decimal integer from raw text.
static bool parse_positive_uint32_span(const char *text, size_t text_len,
                                       uint32_t *out) {
    int64_t parsed = 0;

    if (!opt_parse_int64_span(text, text_len, &parsed) || parsed <= 0 ||
        parsed > UINT32_MAX) {
        return false;
    }

    *out = (uint32_t)parsed;
    return true;
}

// Parse a placeholder size from a COLSxROWS option value.
static bool parse_place_option(void *value, const char *text, size_t text_len,
                               String *error_out) {
    const char *separator = NULL;
    size_t cols_len = 0;
    size_t rows_len = 0;
    PlaceSize parsed = {0};

    if (text_len == 0)
        return opt_parse_error(
            error_out, "expected CxR with positive base-10 unsigned integers");

    for (size_t i = 0; i < text_len; ++i) {
        if (text[i] != 'x' && text[i] != 'X')
            continue;
        if (separator != NULL)
            return opt_parse_error(
                error_out, "expected exactly one x separator in CxR value");
        separator = text + i;
    }
    if (separator == NULL)
        return opt_parse_error(
            error_out, "expected CxR with positive base-10 unsigned integers");

    cols_len = (size_t)(separator - text);
    rows_len = text_len - cols_len - 1;
    if (!parse_positive_uint32_span(text, cols_len, &parsed.cols) ||
        !parse_positive_uint32_span(separator + 1, rows_len, &parsed.rows)) {
        return opt_parse_error(
            error_out, "expected CxR with positive base-10 unsigned integers");
    }

    *(PlaceSize *)value = parsed;
    return true;
}

// Parse a placeholder diacritic mode name.
static bool parse_diacritics_option(void *value, const char *text,
                                    size_t text_len, String *error_out) {
    if (text_len == 0)
        return opt_parse_error(error_out,
                               "expected one of minimal, default, or complete");

    // REVIEW: Create a helper for this kind of string comparison: char and len
    // against cstring
    if (text_len == sizeof("minimal") - 1 &&
        memcmp(text, "minimal", sizeof("minimal") - 1) == 0) {
        *(PlaceholderMode *)value = placeholder_mode_minimal();
        return true;
    }

    if (text_len == sizeof("default") - 1 &&
        memcmp(text, "default", sizeof("default") - 1) == 0) {
        *(PlaceholderMode *)value = placeholder_mode_default();
        return true;
    }

    if (text_len == sizeof("complete") - 1 &&
        memcmp(text, "complete", sizeof("complete") - 1) == 0) {
        *(PlaceholderMode *)value = placeholder_mode_complete();
        return true;
    }

    return opt_parse_error(error_out,
                           "expected one of minimal, default, or complete");
}

// Validate that an unsigned integer is positive.
static bool validate_positive_uint32(const void *value, String *error_out) {
    if (*(const uint32_t *)value == 0)
        return opt_parse_error(error_out, "expected a positive integer");
    return true;
}

// Validate that a placement ID fits the placeholder protocol.
static bool validate_placement_id(const void *value, String *error_out) {
    if (*(const uint32_t *)value > 0xFFFFFFu)
        return opt_parse_error(error_out, "expected a value up to 16777215");
    return true;
}

// Top-level options parsed for every imgneko command.
#define PROGRAM_OPTIONS(X, S)                                                  \
    X(S, version, OptBool,                                                     \
      OPT_BOOL_FLAG(.cli = "-v --version",                                     \
                    .descr = "Show program version and exit."))

// Options for the `placeholder` command.
#define PLACEHOLDER_OPTIONS(X, S)                                              \
    X(S, id, OptUint32,                                                        \
      OPT_CUSTOM(.parse = parse_id_option,                                     \
                 .validate = validate_positive_uint32, .cli = "--id ID",       \
                 .descr = "Image ID to encode in the placeholder, as decimal " \
                          "or 0x-prefixed hex."))                              \
    X(S, placement_id, OptUint32,                                              \
      OPT_CUSTOM(.parse = parse_id_option, .validate = validate_placement_id,  \
                 .cli = "--placement-id ID",                                   \
                 .descr = "Placement ID to encode in the placeholder, as "     \
                          "decimal or 0x-prefixed hex.",                       \
                 .dflt = "0"))                                                 \
    X(S, diacritics, OptPlaceholderMode,                                       \
      OPT_CUSTOM(.parse = parse_diacritics_option,                             \
                 .cli = "-D --diacritics MODE",                                \
                 .descr = "Diacritic mode: minimal, default, or complete."))   \
    X(S, grapheme_only, OptBool,                                               \
      OPT_BOOL_FLAG(.cli = "--grapheme-only",                                  \
                    .descr = "Emit grapheme-only output without SGR colors.")) \
    X(S, place, OptPlaceSize,                                                  \
      OPT_CUSTOM(.parse = parse_place_option, .cli = "-p --place CxR",         \
                 .descr = "Placeholder size as COLSxROWS terminal cells."))    \
    X(S, rows, OptUint32,                                                      \
      OPT_CUSTOM(.parse = parse_uint32_option,                                 \
                 .validate = validate_positive_uint32,                         \
                 .cli = "-r --rows ROWS",                                      \
                 .descr = "Placeholder height in terminal cells."))            \
    X(S, cols, OptUint32,                                                      \
      OPT_CUSTOM(.parse = parse_uint32_option,                                 \
                 .validate = validate_positive_uint32,                         \
                 .cli = "-c --cols COLS",                                      \
                 .descr = "Placeholder width in terminal cells."))

OPT_DEFINE_STRUCT(ProgramOptions, PROGRAM_OPTIONS)
OPT_DEFINE_STRUCT(PlaceholderCliOptions, PLACEHOLDER_OPTIONS)

#define IMGNEKO_COMMANDS(Name, X)                                              \
    X(Name, placeholder, PlaceholderCliOptions,                                \
      OPT_COMMAND(.descr = "Print a terminal image placeholder."))

OPT_DEFINE_PROGRAM_PARSER_WITH_TOP_LEVEL_OPTIONS(
    ImgnekoCLI,
    OPT_PROGRAM(.program_name = "imgneko",
                .descr = "Terminal image placeholder utilities."),
    ProgramOptions, IMGNEKO_COMMANDS);

// Print the build information reported by the historical --version path.
static void print_version(void) {
    printf("version: %s\n", BUILD_IMGNEKO_VERSION);
    printf("compiled: %s\n", BUILD_COMPILED_AT);
    printf("profile: %s\n", BUILD_CONFIG_PROFILE);
    printf("prefix: %s\n", BUILD_CONFIG_PREFIX);
    printf("cc: %s\n", BUILD_CONFIG_CC);
    printf("cppflags: %s\n", BUILD_CONFIG_CPPFLAGS);
    printf("cflags: %s\n", BUILD_CONFIG_CFLAGS);
    printf("ldflags: %s\n", BUILD_CONFIG_LDFLAGS);
    printf("ldlibs: %s\n", BUILD_CONFIG_LDLIBS);
    printf("feature_x: %s\n", BUILD_CONFIG_FEATURE_X);
    printf("coverage_report: %s\n", BUILD_CONFIG_COVERAGE_REPORT);
}

// Report a missing required command option.
static bool require_option(bool is_set, const char *cli_name) {
    if (is_set)
        return true;

    fprintf(stderr, "error: missing required option: %s\n", cli_name);
    return false;
}

// Run the placeholder command.
static int run_placeholder_command(const PlaceholderCliOptions *options) {
    if (!require_option(options->id.is_set, "--id"))
        return 2;

    if (options->place.is_set &&
        (options->rows.is_set || options->cols.is_set)) {
        fprintf(stderr,
                "error: --place cannot be used with --rows or --cols\n");
        return 2;
    }

    uint32_t cols = 0;
    uint32_t rows = 0;
    if (options->place.is_set) {
        cols = options->place.value.cols;
        rows = options->place.value.rows;
    } else {
        if (!require_option(options->rows.is_set, "--rows") ||
            !require_option(options->cols.is_set, "--cols")) {
            return 2;
        }
        cols = options->cols.value;
        rows = options->rows.value;
    }

    Placeholder placeholder = {
        .image_id = options->id.value,
        .placement_id = options->placement_id.value,
        .rect = {.start_col = 0,
                 .start_row = 0,
                 .end_col = cols,
                 .end_row = rows},
    };
    PlaceholderOptions placeholder_options = placeholder_options_default();
    if (options->diacritics.is_set)
        placeholder_options.mode = options->diacritics.value;
    placeholder_options.grapheme_only = options->grapheme_only.value;

    PlaceholderError error =
        placeholder_validate(&placeholder, &placeholder_options.mode);
    if (error != PLACEHOLDER_OK) {
        fprintf(stderr, "error: invalid placeholder: %s\n",
                placeholder_error_string(error));
        return 2;
    }

    error =
        placeholder_write_fd(&placeholder, &placeholder_options, STDOUT_FILENO);
    if (error != PLACEHOLDER_OK) {
        fprintf(stderr, "error: failed to write placeholder: %s\n",
                placeholder_error_string(error));
        return 1;
    }

    return 0;
}

int main(int argc, char **argv) {
    ParsedImgnekoCLI parsed = {0};
    int rc = opt_run_program_parser(&ImgnekoCLI_parser, argc, argv, &parsed);
    if (rc != 0 || parsed.must_exit)
        return rc;

    if (parsed.top_level.version.value) {
        print_version();
        opt_program_result_deinit(&ImgnekoCLI_parser, &parsed);
        return 0;
    }

    switch (parsed.command_id) { // IMGNEKO_UNCOVERED_OK
    case OPT_CMD_NONE:
        fprintf(stderr, "error: missing command\n");
        rc = 2;
        break;
    case OPT_CMD_ImgnekoCLI_placeholder:
        rc = run_placeholder_command(&parsed.command.placeholder);
        break;
    }

    opt_program_result_deinit(&ImgnekoCLI_parser, &parsed);
    return rc;
}
