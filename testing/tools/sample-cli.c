// SPDX-License-Identifier: MIT-0

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "build_info.h"
#include "util/error.h"
#include "util/options.h"

// Standalone sample program that exercises the option parser with synthetic
// commands and both owned and POD custom value types.

//===----------------------------------------------------------------------===//
// Custom option type definitions
//===----------------------------------------------------------------------===//

// One parsed NxM grid size stored on the heap so the sample covers owned custom
// values, not just POD fields.
typedef struct GridSize {
    int width;
    int height;
} GridSize;

// One parsed START:END pass window stored inline to exercise POD custom values.
typedef struct PassWindow {
    int start;
    int end;
} PassWindow;

// One owning array of pass windows used by the sample custom list field.
DEFINE_ARRAY_TYPE(PassWindowArray, PassWindow)

//===----------------------------------------------------------------------===//
// Option wrapper struct definitions
//===----------------------------------------------------------------------===//

// Each wrapper struct looks like this:
//
//   typedef struct OptFoo {
//       Foo value;
//       bool is_set;
//       OptProvenance provenance;
//   } OptFoo;
//

// Optional grid size plus metadata about whether it was supplied.
OPT_DEFINE_WRAPPER_STRUCT(OptGridSize, GridSize *);

// Optional pass window plus metadata about whether it was supplied.
OPT_DEFINE_WRAPPER_STRUCT(OptPassWindow, PassWindow);

// Optional pass-window list plus metadata about whether it was supplied.
OPT_DEFINE_WRAPPER_STRUCT(OptPassWindowList, PassWindowArray);

//===----------------------------------------------------------------------===//
// Custom option parsing and management functions
//===----------------------------------------------------------------------===//

// For each custom option type we can plug three value-oriented callbacks into
// the generic layer:
// - bool parse(void *value, const char *text, size_t text_len, String
// *error_out)
// - void clear(void *value)
// - void copy(void *dst_value, const void *src_value)
// Only `parse` is required. List parsers append one element per call instead of
// replacing the whole list.

// Parse a START:END pass-window value.
static bool parse_pass_window_value(const char *text, size_t text_len,
                                    PassWindow *out) {
    const char *separator = NULL;
    int start = 0;
    int end = 0;
    size_t start_len = 0;
    size_t end_len = 0;

    // IMGNEKO_UNCOVERED_OK[2 lines]
    if (text == NULL || text_len == 0)
        return false;

    separator = memchr(text, ':', text_len);
    if (separator == NULL)
        return false;
    start_len = (size_t)(separator - text);
    end_len = text_len - start_len - 1;
    if (memchr(separator + 1, ':', end_len) != NULL)
        return false;
    if (!opt_parse_int_option(&start, text, start_len, NULL))
        return false;
    if (!opt_validate_positive_int(&start, NULL))
        return false;
    if (!opt_parse_int_option(&end, separator + 1, end_len, NULL))
        return false;
    if (!opt_validate_positive_int(&end, NULL))
        return false;
    if (start > end)
        return false;

    *out = (PassWindow){.start = start, .end = end};
    return true;
}

// Parse a START:END pass-window string into a POD wrapper.
static bool parse_pass_window_option(void *value_ptr, const char *text,
                                     size_t text_len, String *error_out) {
    if (!parse_pass_window_value(text, text_len, value_ptr))
        return opt_parse_error(
            error_out,
            "expected START:END with positive integers and START <= END");

    return true;
}

// Clear a pass-window list.
static void clear_pass_window_list_option(void *value_ptr) {
    PassWindowArray *value = value_ptr;
    arr_free(*value);
}

// Deep-copy a pass-window list.
static void copy_pass_window_list_option(void *dst_value,
                                         const void *src_value) {
    const PassWindowArray *src = src_value;
    PassWindowArray *dst = dst_value;
    *dst = copy_PassWindowArray(*src);
}

// Parse a START:END value and append it to a pass-window list.
static bool parse_pass_window_list_option(void *value_ptr, const char *text,
                                          size_t text_len, String *error_out) {
    PassWindowArray *value = value_ptr;
    PassWindow parsed = {0};

    if (!parse_pass_window_value(text, text_len, &parsed))
        return opt_parse_error(
            error_out,
            "expected START:END with positive integers and START <= END");

    arr_push(*value, parsed);
    return true;
}

// Release the owned grid-size option and reset it to the empty state.
static void clear_grid_size_option(void *value_ptr) {
    GridSize **value = value_ptr;
    free(*value);
    *value = NULL;
}

// Deep-copy the owned grid-size option.
static void copy_grid_size_option(void *dst_value, const void *src_value) {
    const GridSize *const *src = src_value;
    GridSize **dst = dst_value;
    GridSize *copy = NULL;

    // IMGNEKO_UNCOVERED_OK[4 lines]
    if (*src == NULL) {
        *dst = NULL;
        return;
    }

    copy = calloc(1, sizeof(*copy));
    require(copy != NULL, "failed to allocate grid size copy: %errno");
    *copy = **src;
    *dst = copy;
}

// Parse an NxM string into an owned grid-size wrapper.
static bool parse_grid_size_option(void *value_ptr, const char *text,
                                   size_t text_len, String *error_out) {
    GridSize **value = value_ptr;
    const char *separator = NULL;
    GridSize *parsed = NULL;
    int width = 0;
    int height = 0;
    size_t width_len = 0;
    size_t height_len = 0;

    // IMGNEKO_UNCOVERED_OK[3 lines]
    if (text == NULL || text_len == 0)
        return opt_parse_error(error_out,
                               "expected NxM with positive integers");

    for (size_t i = 0; i < text_len; ++i) {
        if (text[i] != 'x' && text[i] != 'X')
            continue;
        if (separator != NULL)
            return opt_parse_error(error_out,
                                   "expected exactly one x separator");
        separator = text + i;
    }
    if (separator == NULL)
        return opt_parse_error(error_out,
                               "expected NxM with positive integers");
    width_len = (size_t)(separator - text);
    height_len = text_len - width_len - 1;
    if (!opt_parse_int_option(&width, text, width_len, NULL))
        return opt_parse_error(error_out, "width must be a positive integer");
    if (!opt_validate_positive_int(&width, NULL))
        return opt_parse_error(error_out, "width must be a positive integer");
    if (!opt_parse_int_option(&height, separator + 1, height_len, NULL))
        return opt_parse_error(error_out, "height must be a positive integer");
    if (!opt_validate_positive_int(&height, NULL))
        return opt_parse_error(error_out, "height must be a positive integer");

    parsed = calloc(1, sizeof(*parsed));
    require(parsed != NULL, "failed to allocate grid size: %errno");
    parsed->width = width;
    parsed->height = height;

    clear_grid_size_option(value);
    *value = parsed;
    return true;
}

//===----------------------------------------------------------------------===//
// Custom option validation functions
//===----------------------------------------------------------------------===//

// Accept either boolean value for options that use validation only to exercise
// the generic parser's bool-idempotence path.
static bool validate_any_bool(const void *value_ptr, String *error_out) {
    (void)value_ptr;
    (void)error_out;
    return true;
}

// Reject empty profile names after parsing so String validation covers the
// ownership-preserving assign-and-rollback path.
static bool validate_non_empty_profile_name(const void *value_ptr,
                                            String *error_out) {
    const String *value = value_ptr;

    if (value->len == 0)
        return opt_parse_error(error_out, "profile name must not be empty");
    return true;
}

// Keep ordinary parsing permissive, but decline the duplicate-idempotence
// shortcut for the false case so the CLI parser falls back to its duplicate
// diagnostic path.
static bool validate_gate_bool(const void *value_ptr, String *error_out) {
    const bool *value = value_ptr;

    if (error_out == NULL && !*value)
        return false;
    return true;
}

// Reject empty validated-owned strings after parsing.
static bool validate_non_empty_validation_owned(const void *value_ptr,
                                                String *error_out) {
    const String *value = value_ptr;

    if (value->len == 0)
        return opt_parse_error(error_out, "text must not be empty");
    return true;
}

//===----------------------------------------------------------------------===//
// Define CLI options using the X macro pattern
//===----------------------------------------------------------------------===//

// Top-level options that are parsed no matter which command is selected, and
// are stored in a separate struct from the per-command options.
#define PROGRAM_OPTIONS(X, S)                                                  \
    X(S, version, OptBool,                                                     \
      OPT_BOOL_FLAG(.cli = "--version",                                        \
                    .descr = "Show program version and exit."))

// Options common for multiple commands.
#define COMMON_OPTIONS(X, S)                                                   \
    X(S, config, OptString,                                                    \
      OPT_STRING(.cli = "-c --config FILE",                                    \
                 .descr = "Read settings from FILE before applying "           \
                          "command-line overrides."))                          \
    X(S, verbose, OptBool,                                                     \
      OPT_BOOL_FLAG(.cli = "-v --verbose",                                     \
                    .descr = "Enable verbose logging."))

#define MORPH_ONLY_OPTIONS(X, S)                                               \
    X(S, passes, OptInt,                                                       \
      OPT_INT(.cli = "-p --passes N",                                          \
              .descr = "Run N synthetic transformation passes.",               \
              .dflt = "24"))                                                   \
    X(S, quota, OptInt,                                                        \
      OPT_INT(.cli = "-q --quota N", .validate = opt_validate_positive_int,    \
              .descr = "Use a positive quota as the synthetic work budget."))  \
    X(S, window, OptPassWindow,                                                \
      OPT_CUSTOM(.parse = parse_pass_window_option,                            \
                 .cli = "--window START:END",                                  \
                 .descr = "Limit the synthetic pass window to the inclusive "  \
                          "START:END range.",                                  \
                 .dflt = "1:3"))                                               \
    X(S, grid, OptGridSize,                                                    \
      OPT_CUSTOM(.parse = parse_grid_size_option,                              \
                 .copy = copy_grid_size_option,                                \
                 .clear = clear_grid_size_option, .cli = "--grid NxM",         \
                 .descr = "Use an NxM work grid for the synthetic job.",       \
                 .dflt = "80x24"))                                             \
    X(S, profile, OptString,                                                   \
      OPT_STRING(.cli = "--profile NAME",                                      \
                 .validate = validate_non_empty_profile_name,                  \
                 .descr = "Select the synthetic profile.", .dflt = "auto"))    \
    X(S, cache_results, OptBool,                                               \
      OPT_BOOL_NEGATABLE(.cli = "-k --cache-results",                          \
                         .cli_negate = "-K --no-cache-results",                \
                         .validate = validate_any_bool,                        \
                         .descr = "Cache or skip cached morph results."))      \
    X(S, keep_workspace, OptBool,                                              \
      OPT_BOOL_NEGATABLE(.cli = "--keep-workspace",                            \
                         .descr = "Retain the synthetic workspace after the "  \
                                  "morph run.",                                \
                         .dflt = "false"))                                     \
    X(S, cleanup, OptBool,                                                     \
      OPT_BOOL_NEGATABLE(                                                      \
              .cli_negate = "--no-cleanup",                                    \
              .descr =                                                         \
                  "Clean up the synthetic workspace after the morph run. "     \
                  "This an awkward negate-only negatable flag, just for "      \
                  "testing, prefer just normal flags in real life.",           \
              .dflt = "true"))                                                 \
    X(S, subjects, OptStringList,                                              \
      OPT_STRING_LIST(.cli = "SUBJECT", .descr = "Subjects to morph.",         \
                      .positional = true))

#define AUDIT_ONLY_OPTIONS(X, S)                                               \
    X(S, format, OptString,                                                    \
      OPT_STRING(.cli = "-f --format NAME",                                    \
                 .descr = "Select the report format.", .dflt = "summary"))     \
    X(S, strict, OptBool,                                                      \
      OPT_BOOL_VALUE(.cli = "--strict BOOL",                                   \
                     .descr = "Require an explicit boolean value."))           \
    X(S, slices, OptPassWindowList,                                            \
      OPT_CUSTOM_LIST(.parse = parse_pass_window_list_option,                  \
                      .copy = copy_pass_window_list_option,                    \
                      .clear = clear_pass_window_list_option,                  \
                      .cli = "--slice START:END",                              \
                      .descr = "Append an audit slice range."))                \
    X(S, targets, OptStringList,                                               \
      OPT_STRING_LIST(.cli = "TARGET",                                         \
                      .descr = "Optional targets to inspect.",                 \
                      .positional = true))

#define PURGE_ONLY_OPTIONS(X, S)                                               \
    X(S, all, OptBool,                                                         \
      OPT_BOOL_FLAG(.cli = "-a --all",                                         \
                    .descr = "Remove all cached artifacts."))                  \
    X(S, dry_run, OptBool,                                                     \
      OPT_BOOL_FLAG(.cli = "-n --dry-run",                                     \
                    .descr = "Describe the purge without deleting anything.")) \
    X(S, cache_root, OptString,                                                \
      OPT_STRING(.cli = "-o --cache-root DIR",                                 \
                 .descr = "Purge DIR instead of the default cache root."))

#define VALIDATE_ONLY_OPTIONS(X, S)                                            \
    X(S, toggle, OptBool,                                                      \
      OPT_BOOL_VALUE(.cli = "--toggle BOOL",                                   \
                     .descr = "Accept repeated equivalent boolean values."))   \
    X(S, gate, OptBool,                                                        \
      OPT_BOOL_VALUE(.cli = "--gate BOOL", .validate = validate_gate_bool,     \
                     .descr = "Reject one duplicate-idempotence preflight."))  \
    X(S, owned, OptString,                                                     \
      OPT_CUSTOM(.parse = opt_parse_string_option,                             \
                 .validate = validate_non_empty_validation_owned,              \
                 .clear = opt_clear_string_option, .cli = "--owned TEXT",      \
                 .descr = "Exercise validated owned values without a copy "    \
                          "hook."))                                            \
    X(S, probe_copy_required, OptBool,                                         \
      OPT_BOOL_FLAG(.cli = "--probe-copy-required",                            \
                    .descr = "Trigger the validated owned-value copy "         \
                             "requirement probe."))

//===----------------------------------------------------------------------===//
// Define options structs for commands
//===----------------------------------------------------------------------===//

#define MORPH_OPTIONS(X, S)                                                    \
    COMMON_OPTIONS(X, S)                                                       \
    MORPH_ONLY_OPTIONS(X, S)
OPT_DEFINE_STRUCT(MorphOptions, MORPH_OPTIONS)

#define AUDIT_OPTIONS(X, S)                                                    \
    COMMON_OPTIONS(X, S)                                                       \
    AUDIT_ONLY_OPTIONS(X, S)
OPT_DEFINE_STRUCT(AuditOptions, AUDIT_OPTIONS)

#define PURGE_OPTIONS(X, S)                                                    \
    COMMON_OPTIONS(X, S)                                                       \
    PURGE_ONLY_OPTIONS(X, S)
OPT_DEFINE_STRUCT(PurgeOptions, PURGE_OPTIONS)

#define VALIDATE_OPTIONS(X, S) VALIDATE_ONLY_OPTIONS(X, S)
OPT_DEFINE_STRUCT(ValidateOptions, VALIDATE_OPTIONS)

// All options, from all commands.
#define GLOBAL_OPTIONS(X, S)                                                   \
    COMMON_OPTIONS(X, S)                                                       \
    MORPH_ONLY_OPTIONS(X, S)                                                   \
    AUDIT_ONLY_OPTIONS(X, S)                                                   \
    PURGE_ONLY_OPTIONS(X, S)
OPT_DEFINE_STRUCT(GlobalOptions, GLOBAL_OPTIONS)

// Top-level options.
OPT_DEFINE_STRUCT(ProgramOptions, PROGRAM_OPTIONS)

//===----------------------------------------------------------------------===//
// Define the program parser
//===----------------------------------------------------------------------===//

#define PROGRAM_COMMANDS(Name, X)                                              \
    X(Name, morph, MorphOptions,                                               \
      OPT_COMMAND(.descr = "Exercise the default-command path with shared "    \
                           "and custom options."))                             \
    X(Name, audit, AuditOptions,                                               \
      OPT_COMMAND(.descr = "Exercise an explicit command with list and "       \
                           "bool-value parsing."))                             \
    X(Name, purge, PurgeOptions,                                               \
      OPT_COMMAND(.descr = "Exercise a command without positionals and with "  \
                           "short flags."))                                    \
    X(Name, validate, ValidateOptions,                                         \
      OPT_COMMAND(.descr = "Exercise validation-only parser edge cases."))

OPT_DEFINE_PROGRAM_PARSER_WITH_TOP_LEVEL_OPTIONS(
    SampleCLI,
    OPT_PROGRAM(.program_name = "sample-cli",
                .descr = "Sample CLI that exercises shared options, commands, "
                         "version handling, boolean modes, defaults, custom "
                         "scalar and list values, and positional parsing.",
                .default_command = "morph"),
    ProgramOptions, PROGRAM_COMMANDS);

//===----------------------------------------------------------------------===//
// Command handlers and output functions
//===----------------------------------------------------------------------===//

// Print a boolean value in a stable, test-friendly format.
static void print_bool_value(bool value) {
    fputs(value ? "true" : "false", stdout);
}

// Print an integer value in a stable, test-friendly format.
static void print_int_value(int value) { printf("%d", value); }

// Print an owned NxM value in a stable, test-friendly format.
static void print_grid_size_value(const GridSize *value) {
    require(value != NULL, "set grid size option is missing its value");
    printf("%dx%d", value->width, value->height);
}

// Print a pass-window value in a stable, test-friendly format.
static void print_pass_window_value(PassWindow value) {
    printf("%d:%d", value.start, value.end);
}

// Print a pass-window list value in a stable, test-friendly format.
static void print_pass_window_list_value(const PassWindowArray *value) {
    printf("[");
    for (size_t i = 0; i < value->size; ++i) {
        if (i != 0)
            printf(", ");
        printf("%d:%d", value->data[i].start, value->data[i].end);
    }
    printf("]");
}

// Print a string value in a stable, test-friendly format.
static void print_string_value(const String *value) {
    fputs(value->cstr, stdout);
}

// Print a C string with minimal escaping so list output stays unambiguous.
static void print_quoted_text(const char *text) {
    String quoted = str_empty;
    str_append_c_quoted_cstr(&quoted, text);
    fputs(quoted.cstr, stdout);
    str_free(quoted);
}

// Print a string-list value in a stable, test-friendly format.
static void print_string_list_value(const StringArray *value) {
    printf("[");
    for (size_t i = 0; i < value->size; ++i) {
        if (i != 0)
            printf(", ");
        print_quoted_text(value->data[i].cstr);
    }
    printf("]");
}

// Print a field wrapper using its name, value, and provenance.
static void print_option_field(const OptFieldSpec *field, const void *options) {
    const void *value_ptr = opt_const_field_value_ptr(field, options);

    printf("%s: ", field->name);

    if (!opt_field_is_set(field, options)) {
        printf("<unset>\n");
        return;
    }

    if (strcmp(field->type_name, "OptBool") == 0) {
        print_bool_value(*(const bool *)value_ptr);
    } else if (strcmp(field->type_name, "OptInt") == 0) {
        print_int_value(*(const int *)value_ptr);
    } else if (strcmp(field->type_name, "OptString") == 0) {
        print_string_value((const String *)value_ptr);
    } else if (strcmp(field->type_name, "OptStringList") == 0) {
        print_string_list_value((const StringArray *)value_ptr);
    } else if (strcmp(field->type_name, "OptPassWindow") == 0) {
        print_pass_window_value(*(const PassWindow *)value_ptr);
    } else if (strcmp(field->type_name, "OptPassWindowList") == 0) {
        print_pass_window_list_value((const PassWindowArray *)value_ptr);
        // IMGNEKO_UNCOVERED_OK
    } else if (strcmp(field->type_name, "OptGridSize") == 0) {
        print_grid_size_value(*(GridSize *const *)value_ptr);
        // IMGNEKO_UNCOVERED_OK[3 lines]
    } else {
        die("unsupported option wrapper type in sample renderer");
    }

    printf(" (%s)\n",
           opt_provenance_name(opt_field_provenance(field, options)));
}

// Print every field from one option schema using the sample's type-based
// renderer.
static void print_options_by_schema(const OptSchema *schema,
                                    const void *options) {
    for (size_t i = 0; i < schema->field_count; ++i)
        print_option_field(&schema->fields[i], options);
}

// Print the sample program version when the top-level `--version` flag was set.
static bool maybe_print_version(OptBool version) {
    if (!version.value)
        return false;

    printf("sample-cli %s\n", BUILD_IMGNEKO_VERSION);
    return true;
}

// Command processors.

// Merge a parsed command into the global option view and print every global
// field in schema order.
static int process_command(const char *command_name,
                           const OptSchema *command_schema,
                           const void *command_options) {
    GlobalOptions global;

    GlobalOptions_init(&global);
    require(opt_merge_matching(&GlobalOptions_schema, &global, command_schema,
                               command_options),
            "command/global schemas are incompatible");

    printf("command: %s\n", command_name);
    print_options_by_schema(&GlobalOptions_schema, &global);

    GlobalOptions_deinit(&global);
    return 0;
}

// Merge a parsed morph command into the global option view and print it.
static int process_morph_command(const MorphOptions *command_options) {
    return process_command("morph", &MorphOptions_schema, command_options);
}

// Merge a parsed audit command into the global option view and print it.
static int process_audit_command(const AuditOptions *command_options) {
    return process_command("audit", &AuditOptions_schema, command_options);
}

// Merge a parsed purge command into the global option view and print it.
static int process_purge_command(const PurgeOptions *command_options) {
    return process_command("purge", &PurgeOptions_schema, command_options);
}

// Trigger the validated-owned-value path that requires a copy callback when the
// destination already holds one committed owned value.
static int run_validate_copy_required_probe(void) {
    ValidateOptions options;
    const OptFieldSpec *owned_field =
        opt_find_field_by_name(&ValidateOptions_schema, "owned");

    ValidateOptions_init(&options);
    require(owned_field != NULL, "missing validation owned field");
    require(opt_assign_field_value(owned_field, &options, "alpha",
                                   strlen("alpha"), OPT_PROVENANCE_DEFAULT,
                                   NULL),
            "failed to assign initial validation owned value");

    // This second validated assignment should die before it returns because the
    // schema intentionally omits the required deep-copy callback.
    (void)opt_assign_field_value(owned_field, &options, "beta", strlen("beta"),
                                 OPT_PROVENANCE_DEFAULT, NULL);

    ValidateOptions_deinit(&options);
    die("expected validation copy-requirement probe to fail");
}

// Print the validation command fields unless the caller requested the
// copy-requirement probe path.
static int process_validate_command(const ValidateOptions *command_options) {
    if (command_options->probe_copy_required.value)
        return run_validate_copy_required_probe();

    printf("command: validate\n");
    print_options_by_schema(&ValidateOptions_schema, command_options);
    return 0;
}

// Entrypoint.

int main(int argc, char **argv) {
    ParsedSampleCLI parsed = {0};
    int rc = 0;

    rc = opt_run_program_parser(&SampleCLI_parser, argc, argv, &parsed);
    if (rc != 0)
        return rc;

    if (maybe_print_version(parsed.top_level.version)) {
        opt_program_result_deinit(&SampleCLI_parser, &parsed);
        return 0;
    }

    switch (parsed.command_id) { // IMGNEKO_UNCOVERED_OK
    // IMGNEKO_UNCOVERED_OK[4 lines]: The parser either exited early for
    // `--version` or selected a concrete command before we reach this switch.
    case OPT_CMD_NONE:
        rc = 0;
        break;
    case OPT_CMD_SampleCLI_morph:
        rc = process_morph_command(&parsed.command.morph);
        break;
    case OPT_CMD_SampleCLI_audit:
        rc = process_audit_command(&parsed.command.audit);
        break;
    case OPT_CMD_SampleCLI_purge:
        rc = process_purge_command(&parsed.command.purge);
        break;
    case OPT_CMD_SampleCLI_validate:
        rc = process_validate_command(&parsed.command.validate);
        break;
    }

    opt_program_result_deinit(&SampleCLI_parser, &parsed);
    return rc;
}
