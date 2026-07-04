// SPDX-License-Identifier: MIT-0

// Command-line entry point for imgneko.

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include "build_info.h"
#include "cli/placeholder_bg.h"
#include "imgneko/placeholder.h"
#include "util/options.h"
#include "util/string.h"

OPT_DEFINE_WRAPPER_STRUCT(OptUint32, uint32_t);

// Terminal cursor movement method used between placeholder rows.
typedef enum PlaceholderCursorMovement {
    PLACEHOLDER_CURSOR_MOVEMENT_AUTO,
    PLACEHOLDER_CURSOR_MOVEMENT_TEXT,
    PLACEHOLDER_CURSOR_MOVEMENT_SAVE_RESTORE,
    PLACEHOLDER_CURSOR_MOVEMENT_MOVE_LEFT,
    PLACEHOLDER_CURSOR_MOVEMENT_ABSOLUTE,
} PlaceholderCursorMovement;

// Pair of unsigned decimal integers parsed from either A,B or AxB syntax. The
// first value is the horizontal component, and the second is the vertical
// component.
typedef struct Uint32Pair {
    uint32_t first;
    uint32_t second;
} Uint32Pair;

// Named enum value accepted by a CLI parser.
typedef struct NamedEnumOption {
    const char *name;
    int value;
} NamedEnumOption;

OPT_DEFINE_WRAPPER_STRUCT(OptUint32Pair, Uint32Pair);
OPT_DEFINE_WRAPPER_STRUCT(OptPlaceholderCursorMovement,
                          PlaceholderCursorMovement);
OPT_DEFINE_WRAPPER_STRUCT(OptPlaceholderFinalCursor, PlaceholderFinalCursor);
OPT_DEFINE_WRAPPER_STRUCT(OptPlaceholderMode, PlaceholderMode);
OPT_DEFINE_WRAPPER_STRUCT(OptPlaceholderBg, PlaceholderBg);

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

// Parse a pair from an A,B or AxB option value.
static bool parse_uint32_pair_option(void *value, const char *text,
                                     size_t text_len, String *error_out) {
    Uint32Pair pair = {0};
    const char *separator = NULL;

    for (size_t i = 0; i < text_len; ++i) {
        char ch = text[i];
        if (ch != ',' && ch != 'x' && ch != 'X')
            continue;
        if (separator != NULL)
            return opt_parse_error(
                error_out,
                "expected A,B or AxB with base-10 unsigned integers");
        separator = text + i;
    }
    if (separator == NULL)
        return opt_parse_error(
            error_out, "expected A,B or AxB with base-10 unsigned integers");

    size_t first_len = (size_t)(separator - text);
    size_t second_len = text_len - first_len - 1;
    int64_t first = 0;
    int64_t second = 0;
    if (!opt_parse_int64_span(text, first_len, &first) || first < 0 ||
        first > UINT32_MAX ||
        !opt_parse_int64_span(separator + 1, second_len, &second) ||
        second < 0 || second > UINT32_MAX) {
        return opt_parse_error(
            error_out, "expected A,B or AxB with base-10 unsigned integers");
    }

    pair.first = (uint32_t)first;
    pair.second = (uint32_t)second;
    *(Uint32Pair *)value = pair;
    return true;
}

// Parse a named enum from a fixed option table.
//
// `options`
//     Accepted names and their enum values.
// `num_options`
//     Number of entries in `options`.
// `text`
//     Option value text.
// `text_len`
//     Length of `text` in bytes.
// `out`
//     Receives the matching enum value on success.
// `error_out`
//     Optional. Receives a parse-error reason when parsing fails.
static bool parse_named_enum_option(const NamedEnumOption *options,
                                    size_t num_options, const char *text,
                                    size_t text_len, int *out,
                                    String *error_out) {
    for (size_t i = 0; i < num_options; ++i) {
        if (str_data_equals_cstr(text, text_len, options[i].name)) {
            *out = options[i].value;
            return true;
        }
    }

    String expected = str_from_cstr("expected one of ");
    for (size_t i = 0; i < num_options; ++i) {
        if (i != 0) {
            // IMGNEKO_UNCOVERED_OK[3 lines]: Current enum parsers have at
            // least three values.
            if (i + 1 == num_options && num_options == 2)
                str_append_cstr(expected, " or ");
            else if (i + 1 == num_options)
                str_append_cstr(expected, ", or ");
            else
                str_append_cstr(expected, ", ");
        }
        str_append_cstr(expected, options[i].name);
    }

    bool ok = opt_parse_error(error_out, expected.cstr);
    str_free(expected);
    return ok;
}

// Parse a placeholder diacritic mode name.
static bool parse_diacritics_option(void *value, const char *text,
                                    size_t text_len, String *error_out) {
    if (text_len == 0)
        return opt_parse_error(error_out,
                               "expected one of minimal, default, or complete");

    if (str_data_equals_cstr(text, text_len, "minimal")) {
        *(PlaceholderMode *)value = placeholder_mode_minimal();
        return true;
    }

    if (str_data_equals_cstr(text, text_len, "default")) {
        *(PlaceholderMode *)value = placeholder_mode_default();
        return true;
    }

    if (str_data_equals_cstr(text, text_len, "complete")) {
        *(PlaceholderMode *)value = placeholder_mode_complete();
        return true;
    }

    return opt_parse_error(error_out,
                           "expected one of minimal, default, or complete");
}

// Parse the terminal cursor movement method for drawing placeholder rows.
static bool parse_cursor_movement_option(void *value, const char *text,
                                         size_t text_len, String *error_out) {
    static const NamedEnumOption options[] = {
        {"auto", PLACEHOLDER_CURSOR_MOVEMENT_AUTO},
        {"text", PLACEHOLDER_CURSOR_MOVEMENT_TEXT},
        {"save-restore", PLACEHOLDER_CURSOR_MOVEMENT_SAVE_RESTORE},
        {"move-left", PLACEHOLDER_CURSOR_MOVEMENT_MOVE_LEFT},
        {"absolute", PLACEHOLDER_CURSOR_MOVEMENT_ABSOLUTE},
    };
    int parsed = 0;

    if (!parse_named_enum_option(options, ARRAY_SIZE(options), text, text_len,
                                 &parsed, error_out))
        return false;

    *(PlaceholderCursorMovement *)value = (PlaceholderCursorMovement)parsed;
    return true;
}

// Parse the final cursor position after placeholder output is complete.
static bool parse_final_cursor_option(void *value, const char *text,
                                      size_t text_len, String *error_out) {
    static const NamedEnumOption options[] = {
        {"next-line", PLACEHOLDER_FINAL_CURSOR_NEXT_LINE},
        {"bottom-left", PLACEHOLDER_FINAL_CURSOR_BOTTOM_LEFT},
        {"below-left", PLACEHOLDER_FINAL_CURSOR_BELOW_LEFT},
        {"bottom-right", PLACEHOLDER_FINAL_CURSOR_BOTTOM_RIGHT},
        {"top-left", PLACEHOLDER_FINAL_CURSOR_TOP_LEFT},
        {"top-right", PLACEHOLDER_FINAL_CURSOR_TOP_RIGHT},
    };
    int parsed = 0;

    if (!parse_named_enum_option(options, ARRAY_SIZE(options), text, text_len,
                                 &parsed, error_out))
        return false;

    *(PlaceholderFinalCursor *)value = (PlaceholderFinalCursor)parsed;
    return true;
}

// Resolve `auto` row movement from the start-placement mode.
static PlaceholderCursorMovement
resolve_cursor_movement(PlaceholderCursorMovement requested,
                        bool has_start_placement) {
    if (requested != PLACEHOLDER_CURSOR_MOVEMENT_AUTO)
        return requested;
    if (has_start_placement)
        return PLACEHOLDER_CURSOR_MOVEMENT_MOVE_LEFT;
    return PLACEHOLDER_CURSOR_MOVEMENT_TEXT;
}

// Format a zero-based absolute cursor-position prefix into `out`.
static bool format_cup_prefix(char *out, size_t out_cap, uint32_t col,
                              uint32_t row) {
    if (col == UINT32_MAX || row == UINT32_MAX)
        return false;

    int written = snprintf(out, out_cap, "\033[%u;%uH", row + 1, col + 1);
    // IMGNEKO_UNCOVERED_OK: The fixed prefix fits the CLI buffer.
    return written >= 0 && (size_t)written < out_cap;
}

// Format a zero-based absolute-column cursor prefix into `out`.
static bool format_cha_prefix(char *out, size_t out_cap, uint32_t col) {
    if (col == UINT32_MAX)
        return false;

    int written = snprintf(out, out_cap, "\033[%uG", col + 1);
    // IMGNEKO_UNCOVERED_OK: The fixed prefix fits the CLI buffer.
    return written >= 0 && (size_t)written < out_cap;
}

// Validate that an unsigned integer is positive.
static bool validate_positive_uint32(const void *value, String *error_out) {
    if (*(const uint32_t *)value == 0)
        return opt_parse_error(error_out, "expected a positive integer");
    return true;
}

// Validate that both values in a pair are positive.
static bool validate_positive_uint32_pair(const void *value,
                                          String *error_out) {
    const Uint32Pair *pair = value;

    if (pair->first == 0 || pair->second == 0)
        return opt_parse_error(
            error_out,
            "expected positive A,B or AxB with base-10 unsigned integers");
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
    X(S, cursor_movement, OptPlaceholderCursorMovement,                        \
      OPT_CUSTOM(.parse = parse_cursor_movement_option,                        \
                 .cli = "--cursor-movement MODE",                              \
                 .descr = "Cursor movement method.", .dflt = "auto"))          \
    X(S, final_cursor, OptPlaceholderFinalCursor,                              \
      OPT_CUSTOM(.parse = parse_final_cursor_option,                           \
                 .cli = "-C --final-cursor POS",                               \
                 .descr = "Final cursor position.", .dflt = "next-line"))      \
    X(S, at_cursor, OptBool,                                                   \
      OPT_BOOL_FLAG(.cli = "--at-cursor",                                      \
                    .descr = "Start at the current cursor position."))         \
    X(S, at, OptUint32Pair,                                                    \
      OPT_CUSTOM(.parse = parse_uint32_pair_option, .cli = "--at X,Y",         \
                 .descr = "Move to an absolute position before drawing."))     \
    X(S, at_column, OptUint32,                                                 \
      OPT_CUSTOM(.parse = parse_uint32_option, .cli = "--at-column X",         \
                 .descr = "Move to an absolute column before drawing."))       \
    X(S, bg, OptPlaceholderBg,                                                 \
      OPT_CUSTOM(.parse = placeholder_bg_parse_option,                         \
                 .clear = placeholder_bg_clear_option,                         \
                 .copy = placeholder_bg_copy_option, .cli = "--bg BG",         \
                 .descr = "Background color or pattern."))                     \
    X(S, bg_raw, OptPlaceholderBg,                                             \
      OPT_CUSTOM(.parse = placeholder_bg_parse_option_raw,                     \
                 .clear = placeholder_bg_clear_option,                         \
                 .copy = placeholder_bg_copy_option, .cli = "--bg-raw STR",    \
                 .descr = "Raw background formatting escape sequence."))       \
    X(S, bg_file, OptPlaceholderBg,                                            \
      OPT_CUSTOM(.parse = placeholder_bg_parse_option_file,                    \
                 .clear = placeholder_bg_clear_option,                         \
                 .copy = placeholder_bg_copy_option, .cli = "--bg-file PATH",  \
                 .descr = "Raw background formatting file path."))             \
    X(S, place, OptUint32Pair,                                                 \
      OPT_CUSTOM(.parse = parse_uint32_pair_option,                            \
                 .validate = validate_positive_uint32_pair,                    \
                 .cli = "-p --place CxR",                                      \
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
    // Validate required options and reject conflicting option groups.
    if (!require_option(options->id.is_set, "--id"))
        return 2;

    if (options->place.is_set &&
        (options->rows.is_set || options->cols.is_set)) {
        fprintf(stderr,
                "error: --place cannot be used with --rows or --cols\n");
        return 2;
    }

    int background_options_set = (options->bg.is_set ? 1 : 0) +
                                 (options->bg_raw.is_set ? 1 : 0) +
                                 (options->bg_file.is_set ? 1 : 0);
    if (background_options_set > 1) {
        fprintf(
            stderr,
            "error: --bg, --bg-raw, and --bg-file are mutually exclusive\n");
        return 2;
    }

    int at_options_set = (options->at_cursor.value ? 1 : 0) +
                         (options->at.is_set ? 1 : 0) +
                         (options->at_column.is_set ? 1 : 0);
    if (at_options_set > 1) {
        fprintf(stderr,
                "error: --at-cursor, --at, and --at-column are mutually "
                "exclusive\n");
        return 2;
    }

    PlaceholderCursorMovement requested_movement =
        options->cursor_movement.value;
    if (requested_movement == PLACEHOLDER_CURSOR_MOVEMENT_TEXT) {
        if (options->at_cursor.value) {
            fprintf(stderr, "error: --cursor-movement text cannot be used with "
                            "--at-cursor\n");
            return 2;
        }
        if (options->at.is_set && options->at.value.first != 0) {
            fprintf(stderr,
                    "error: --cursor-movement text requires --at X,Y with X "
                    "equal to 0\n");
            return 2;
        }
        if (options->at_column.is_set && options->at_column.value != 0) {
            fprintf(stderr,
                    "error: --cursor-movement text requires --at-column 0\n");
            return 2;
        }
    }

    if (requested_movement == PLACEHOLDER_CURSOR_MOVEMENT_ABSOLUTE) {
        if (options->at_cursor.value) {
            fprintf(stderr,
                    "error: --cursor-movement absolute cannot be used with "
                    "--at-cursor\n");
            return 2;
        }
        if (options->at_column.is_set) {
            fprintf(stderr,
                    "error: --cursor-movement absolute cannot be used with "
                    "--at-column\n");
            return 2;
        }
    }

    // Resolve the rectangle size from either --place or the --rows/--cols pair.
    uint32_t cols = 0;
    uint32_t rows = 0;
    if (options->place.is_set) {
        cols = options->place.value.first;
        rows = options->place.value.second;
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

    // Convert CLI rendering choices into the lower-level placeholder writer
    // options.
    PlaceholderOptions placeholder_options = placeholder_options_default();
    if (options->diacritics.is_set)
        placeholder_options.mode = options->diacritics.value;
    placeholder_options.grapheme_only = options->grapheme_only.value;

    // Build the optional prefix that moves the terminal cursor before the first
    // placeholder row is emitted.
    char first_line_start_prefix[64] = "";
    uint32_t start_col = 0;
    uint32_t start_row = 0;
    if (options->at.is_set) {
        start_col = options->at.value.first;
        start_row = options->at.value.second;
        if (!format_cup_prefix(first_line_start_prefix,
                               sizeof(first_line_start_prefix), start_col,
                               start_row)) {
            fprintf(stderr, "error: failed to format --at prefix\n");
            return 2;
        }
    } else if (options->at_column.is_set) {
        start_col = options->at_column.value;
        if (!format_cha_prefix(first_line_start_prefix,
                               sizeof(first_line_start_prefix), start_col)) {
            fprintf(stderr, "error: failed to format --at-column prefix\n");
            return 2;
        }
    }

    // Build the placeholder positioner.
    PlaceholderPositionConfig position_config = {
        .first_line_start_prefix =
            first_line_start_prefix[0] == '\0' ? NULL : first_line_start_prefix,
        .final_cursor = options->final_cursor.value,
    };
    PlaceholderAbsPos abs_pos = {
        .origin_col = start_col,
        .origin_row = options->at.is_set ? start_row : 0,
        .final_cursor = options->final_cursor.value,
    };
    PlaceholderCursorMovement movement =
        resolve_cursor_movement(requested_movement, at_options_set != 0);

    // IMGNEKO_UNCOVERED_OK: auto movement is resolved earlier.
    switch (movement) {
    case PLACEHOLDER_CURSOR_MOVEMENT_TEXT:
        placeholder_options.positioner =
            placeholder_position_linefeeds(&position_config);
        break;
    case PLACEHOLDER_CURSOR_MOVEMENT_SAVE_RESTORE:
        placeholder_options.positioner =
            placeholder_position_at_cursor_with_save(&position_config);
        break;
    case PLACEHOLDER_CURSOR_MOVEMENT_MOVE_LEFT:
        placeholder_options.positioner =
            placeholder_position_at_cursor_with_moves(&position_config);
        break;
    case PLACEHOLDER_CURSOR_MOVEMENT_ABSOLUTE:
        placeholder_options.positioner =
            placeholder_position_absolute(&abs_pos);
        break;
    // IMGNEKO_UNCOVERED_OK[3 lines]
    case PLACEHOLDER_CURSOR_MOVEMENT_AUTO:
        fprintf(stderr, "error: internal cursor movement resolution failed\n");
        return 2;
    }

    // Build the optional bg formatting.
    if (options->bg.is_set) {
        placeholder_options.format =
            placeholder_bg_to_format(&options->bg.value);
    } else if (options->bg_raw.is_set) {
        placeholder_options.format =
            placeholder_bg_to_format(&options->bg_raw.value);
    } else if (options->bg_file.is_set) {
        placeholder_options.format =
            placeholder_bg_to_format(&options->bg_file.value);
    }

    // Validate the final placeholder shape before handing it to the writer.
    PlaceholderError error =
        placeholder_validate(&placeholder, &placeholder_options.mode);
    // IMGNEKO_UNCOVERED_OK[5 lines]: CLI parsing and built-in modes produce
    // valid placeholders before this defensive validation check.
    if (error != PLACEHOLDER_OK) {
        fprintf(stderr, "error: invalid placeholder: %s\n",
                placeholder_error_string(error));
        return 2;
    }

    // Emit the placeholder to stdout.
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
