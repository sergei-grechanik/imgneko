// SPDX-License-Identifier: MIT-0

// Command-line entry point for imgneko.

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include "build_info.h"
#include "imgneko/placeholder.h"
#include "util/options.h"

OPT_DEFINE_WRAPPER_STRUCT(OptUint32, uint32_t);

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
      OPT_CUSTOM(.parse = parse_uint32_option,                                 \
                 .validate = validate_positive_uint32, .cli = "--id ID",       \
                 .descr = "Image ID to encode in the placeholder."))           \
    X(S, placement_id, OptUint32,                                              \
      OPT_CUSTOM(.parse = parse_uint32_option,                                 \
                 .validate = validate_placement_id,                            \
                 .cli = "--placement-id ID",                                   \
                 .descr = "Placement ID to encode in the placeholder.",        \
                 .dflt = "0"))                                                 \
    X(S, rows, OptUint32,                                                      \
      OPT_CUSTOM(.parse = parse_uint32_option,                                 \
                 .validate = validate_positive_uint32, .cli = "--rows ROWS",   \
                 .descr = "Placeholder height in terminal cells."))            \
    X(S, cols, OptUint32,                                                      \
      OPT_CUSTOM(.parse = parse_uint32_option,                                 \
                 .validate = validate_positive_uint32, .cli = "--cols COLS",   \
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
    if (!require_option(options->id.is_set, "--id") ||
        !require_option(options->rows.is_set, "--rows") ||
        !require_option(options->cols.is_set, "--cols")) {
        return 2;
    }

    Placeholder placeholder = {
        .image_id = options->id.value,
        .placement_id = options->placement_id.value,
        .rect = {.start_col = 0,
                 .start_row = 0,
                 .end_col = options->cols.value,
                 .end_row = options->rows.value},
    };
    PlaceholderOptions placeholder_options = placeholder_options_default();

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
