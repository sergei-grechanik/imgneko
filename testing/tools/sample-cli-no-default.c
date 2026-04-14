#include <stdio.h>

#include "util/options.h"

// Sample CLI with multiple commands, no default command, and no top-level
// option schema.

// Fail without writing an explicit parse-error string so diagnostics exercise
// the empty-reason formatting path through a real CLI entry point.
static bool parse_silent_error_option(void *value_ptr, const char *text,
                                      size_t text_len, String *error_out) {
    (void)value_ptr;
    (void)text;
    (void)text_len;
    (void)error_out;
    return false;
}

#define PLAN_OPTIONS(X, S)                                                     \
    X(S, verbose, OptBool,                                                     \
      OPT_BOOL_FLAG(.cli = "-v --verbose",                                     \
                    .descr = "Enable verbose planning output."))               \
    X(S, format, OptString,                                                    \
      OPT_STRING(.cli = "--format NAME",                                       \
                 .descr = "Choose the plan output format.",                    \
                 .dflt = "summary"))                                           \
    X(S, short_count, OptInt,                                                  \
      OPT_INT(.cli = "-n COUNT",                                               \
              .descr = "Short-only diagnostic sample option."))                \
    X(S, multi_short_count, OptInt,                                            \
      OPT_INT(.cli = "-m -M COUNT",                                            \
              .descr = "Multi-short diagnostic sample option."))               \
    X(S, silent_value, OptInt,                                                 \
      OPT_CUSTOM(.parse = parse_silent_error_option, .cli = "--silent VALUE",  \
                 .descr = "Synthetic option that always fails parsing."))      \
    X(S, disabled, OptBool,                                                    \
      OPT_ATTRS(.bool_mode = OPT_BOOL_MODE_NEGATABLE,                          \
                .parse = parse_silent_error_option,                            \
                .cli_negate = "--disable-feature",                             \
                .descr = "Synthetic negate-only option that always fails "     \
                         "parsing."))                                          \
    X(S, target, OptString,                                                    \
      OPT_STRING(.cli = "TARGET", .descr = "Thing to inspect.",                \
                 .positional = true))
OPT_DEFINE_STRUCT(PlanOptions, PLAN_OPTIONS)

#define APPLY_OPTIONS(X, S)                                                    \
    X(S, force, OptBool,                                                       \
      OPT_BOOL_FLAG(.cli = "-f --force",                                       \
                    .descr = "Apply without a confirmation prompt."))          \
    X(S, retries, OptInt,                                                      \
      OPT_INT(.cli = "-r --retries N",                                         \
              .descr = "Retry a failing apply step N times.", .dflt = "1"))    \
    X(S, target, OptString,                                                    \
      OPT_STRING(.cli = "TARGET", .descr = "Thing to modify.",                 \
                 .positional = true))                                          \
    X(S, count, OptInt,                                                        \
      OPT_INT(.cli = "COUNT",                                                  \
              .descr = "Number of synthetic changes to apply.",                \
              .positional = true))
OPT_DEFINE_STRUCT(ApplyOptions, APPLY_OPTIONS)

#define REPEAT_OPTIONS(X, S)                                                   \
    X(S, copies, OptInt,                                                       \
      OPT_ATTRS(.parse = opt_parse_int_option,                                 \
                .descr = "Number of synthetic repetitions.",                   \
                .positional = true))
OPT_DEFINE_STRUCT(RepeatOptions, REPEAT_OPTIONS)

#define LABELS_OPTIONS(X, S)                                                   \
    X(S, short_only, OptString,                                                \
      OPT_ATTRS(.parse = opt_parse_string_option,                              \
                .clear = opt_clear_string_option,                              \
                .copy = opt_copy_string_option, .cli = "-s SIZE",              \
                .nargs = "1"))                                                 \
    X(S, item_name, OptStringList,                                             \
      OPT_ATTRS(.parse = opt_parse_string_list_option,                         \
                .clear = opt_clear_string_list_option,                         \
                .copy = opt_copy_string_list_option, .cli = "--item NAME",     \
                .nargs = "+"))                                                 \
    X(S, file2_count, OptInt,                                                  \
      OPT_ATTRS(.parse = opt_parse_int_option,                                 \
                .descr = "Fallback positional.  ", .positional = true))
OPT_DEFINE_STRUCT(LabelsOptions, LABELS_OPTIONS)

// This command intentionally omits command-level `.descr` so the sample CLI
// covers the command-help path where a command has no description text.
#define UNEXPLAINED_OPTIONS(X, S)                                              \
    X(S, image, OptString,                                                     \
      OPT_STRING(.cli = "IMAGE",                                               \
                 .descr = "Optional unexplained sample positional.",           \
                 .positional = true, .nargs = "?"))
OPT_DEFINE_STRUCT(UnexplainedOptions, UNEXPLAINED_OPTIONS)

#define SAMPLE_CLI_NO_DEFAULT_COMMANDS(Name, X)                                \
    X(Name, plan, PlanOptions,                                                 \
      OPT_COMMAND(.descr = "Exercise explicit command selection with "         \
                           "command-local options."))                          \
    X(Name, apply, ApplyOptions,                                               \
      OPT_COMMAND(.descr = "Exercise no-default parsing with two positional "  \
                           "arguments."))                                      \
    X(Name, repeat, RepeatOptions,                                             \
      OPT_COMMAND(.descr = "Exercise integer positional parsing without a "    \
                           "default command."))                                \
    X(Name, unexplained, UnexplainedOptions, OPT_COMMAND(.descr = NULL))       \
    X(Name, labels, LabelsOptions,                                             \
      OPT_COMMAND(.descr = "Exercise help and diagnostic label fallbacks."))

OPT_DEFINE_PROGRAM_PARSER(
    SampleCliNoDefault,
    OPT_PROGRAM(.program_name = "sample-cli-no-default",
                .descr = "Sample CLI with multiple commands and no default "
                         "command."),
    SAMPLE_CLI_NO_DEFAULT_COMMANDS);

// Print a boolean option in a stable, test-friendly format.
static void print_bool_option(const char *name, OptBool option) {
    printf("%s: %s (%s)\n", name, option.value ? "true" : "false",
           opt_provenance_name(option.provenance));
}

// Print an integer option in a stable, test-friendly format.
static void print_int_option(const char *name, OptInt option) {
    printf("%s: %d (%s)\n", name, option.value,
           opt_provenance_name(option.provenance));
}

// Print a string option in a stable, test-friendly format.
static void print_string_option(const char *name, OptString option) {
    printf("%s: %s (%s)\n", name, option.value.cstr,
           opt_provenance_name(option.provenance));
}

// Print an optional string option in a stable, test-friendly format.
static void print_optional_string_option(const char *name, OptString option) {
    printf("%s: ", name);
    if (!option.is_set) {
        printf("<unset>\n");
        return;
    }

    printf("%s (%s)\n", option.value.cstr,
           opt_provenance_name(option.provenance));
}

// Print a string-list option in a stable, test-friendly format.
static void print_string_list_option(const char *name, OptStringList option) {
    printf("%s: ", name);
    if (!option.is_set) {
        printf("<unset>\n");
        return;
    }

    printf("[");
    for (size_t i = 0; i < option.value.size; ++i) {
        if (i != 0)
            printf(", ");
        printf("%s", option.value.data[i].cstr);
    }
    printf("] (%s)\n", opt_provenance_name(option.provenance));
}

// Print the parsed plan command.
static int process_plan_command(const PlanOptions *options) {
    printf("command: plan\n");
    print_bool_option("verbose", options->verbose);
    print_string_option("format", options->format);
    print_string_option("target", options->target);
    return 0;
}

// Print the parsed apply command.
static int process_apply_command(const ApplyOptions *options) {
    printf("command: apply\n");
    print_bool_option("force", options->force);
    print_int_option("retries", options->retries);
    print_string_option("target", options->target);
    print_int_option("count", options->count);
    return 0;
}

// Print the parsed repeat command.
static int process_repeat_command(const RepeatOptions *options) {
    printf("command: repeat\n");
    print_int_option("copies", options->copies);
    return 0;
}

// Print the parsed unexplained command.
static int process_unexplained_command(const UnexplainedOptions *options) {
    printf("command: unexplained\n");
    print_optional_string_option("image", options->image);
    return 0;
}

// Print the parsed labels command.
static int process_labels_command(const LabelsOptions *options) {
    printf("command: labels\n");
    print_optional_string_option("short_only", options->short_only);
    print_string_list_option("item_name", options->item_name);
    print_int_option("file2_count", options->file2_count);
    return 0;
}

int main(int argc, char **argv) {
    ParsedSampleCliNoDefault parsed = {0};
    int rc =
        opt_run_program_parser(&SampleCliNoDefault_parser, argc, argv, &parsed);

    if (rc != 0 || parsed.must_exit)
        return rc;

    switch (parsed.command_id) {
    case OPT_CMD_NONE_SampleCliNoDefault:
        fprintf(stderr, "error: missing command\n");
        rc = 2;
        break;
    case OPT_CMD_SampleCliNoDefault_plan:
        rc = process_plan_command(&parsed.command.plan);
        break;
    case OPT_CMD_SampleCliNoDefault_apply:
        rc = process_apply_command(&parsed.command.apply);
        break;
    case OPT_CMD_SampleCliNoDefault_repeat:
        rc = process_repeat_command(&parsed.command.repeat);
        break;
    case OPT_CMD_SampleCliNoDefault_unexplained:
        rc = process_unexplained_command(&parsed.command.unexplained);
        break;
    case OPT_CMD_SampleCliNoDefault_labels:
        rc = process_labels_command(&parsed.command.labels);
        break;
    // IMGNEKO_UNCOVERED_OK[3 lines]: Defensive.
    default:
        rc = 0;
        break;
    }

    opt_program_result_deinit(&SampleCliNoDefault_parser, &parsed);
    return rc;
}
