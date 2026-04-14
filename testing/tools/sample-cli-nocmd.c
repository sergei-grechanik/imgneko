#include <stdio.h>
#include <string.h>

#include "build_info.h"
#include "util/error.h"
#include "util/options.h"

// Standalone sample CLI without subcommands. This exercises the no-command
// program parser path with built-in option types and three fixed positional
// arguments.

#define SAMPLE_CLI_NOCMD_OPTIONS(X, S)                                         \
    X(S, version, OptBool,                                                     \
      OPT_BOOL_FLAG(.cli = "--version",                                        \
                    .descr = "Show program version and exit."))                \
    X(S, verbose, OptBool,                                                     \
      OPT_BOOL_FLAG(.cli = "-v --verbose",                                     \
                    .descr = "Enable verbose logging."))                       \
    X(S, retries, OptInt,                                                      \
      OPT_INT(.cli = "-r --retries N",                                         \
              .descr = "Retry the synthetic run N times.", .dflt = "3"))       \
    X(S, color, OptBool,                                                       \
      OPT_BOOL_VALUE(.cli = "--color BOOL",                                    \
                     .descr = "Force colored status output.", .dflt = "true")) \
    X(S, config, OptString,                                                    \
      OPT_STRING(.cli = "-c --config FILE",                                    \
                 .descr =                                                      \
                     "Read settings from FILE before processing inputs."))     \
    X(S, source, OptString,                                                    \
      OPT_STRING(.cli = "SOURCE", .descr = "Input artifact to process.",       \
                 .positional = true))                                          \
    X(S, destination, OptString,                                               \
      OPT_STRING(.cli = "DEST", .descr = "Output artifact path.",              \
                 .positional = true))                                          \
    X(S, profile, OptString,                                                   \
      OPT_STRING(.cli = "PROFILE", .descr = "Synthetic execution profile.",    \
                 .positional = true, .nargs = "?"))

OPT_DEFINE_STRUCT(SampleCliNoCmdOptions, SAMPLE_CLI_NOCMD_OPTIONS)

OPT_DEFINE_PROGRAM_PARSER_NO_COMMANDS(
    SampleCliNoCmd,
    OPT_PROGRAM(.program_name = "sample-cli-nocmd",
                .descr =
                    "Sample CLI without commands that exercises help, version, "
                    "options, and fixed positional arguments."),
    SampleCliNoCmdOptions);

// Print a boolean value in a stable, test-friendly format.
static void print_bool_value(bool value) {
    fputs(value ? "true" : "false", stdout);
}

// Print an integer value in a stable, test-friendly format.
static void print_int_value(int value) { printf("%d", value); }

// Print a string value in a stable, test-friendly format.
static void print_string_value(const String *value) {
    fputs(value->cstr, stdout);
}

// Print a field wrapper using its name, value, and provenance.
static void print_option_field(const OptFieldSpec *field, const void *options) {
    const void *value_ptr = opt_const_field_value_ptr(field, options);

    printf("%s: ", field->name);
    if (!opt_field_is_set(field, options)) {
        printf("<unset>\n");
        return;
    }

    if (strcmp(field->type_name, "OptBool") == 0)
        print_bool_value(*(const bool *)value_ptr);
    else if (strcmp(field->type_name, "OptInt") == 0)
        print_int_value(*(const int *)value_ptr);
    else if (strcmp(field->type_name, "OptString") == 0) // IMGNEKO_UNCOVERED_OK
        print_string_value((const String *)value_ptr);
    else // IMGNEKO_UNCOVERED_OK[2 lines]
        die("unsupported option wrapper type in sample-cli-nocmd renderer");

    printf(" (%s)\n",
           opt_provenance_name(opt_field_provenance(field, options)));
}

// Print every field from the sample schema using the type-based renderer.
static void print_options(const SampleCliNoCmdOptions *options) {
    for (size_t i = 0; i < SampleCliNoCmdOptions_schema.field_count; ++i)
        print_option_field(&SampleCliNoCmdOptions_schema.fields[i], options);
}

// Validate the positional arguments that this sample treats as required.
static int validate_required_arguments(const SampleCliNoCmdOptions *options) {
    if (!options->source.is_set) {
        fprintf(stderr, "error: missing required argument: SOURCE\n");
        return 2;
    }
    if (!options->destination.is_set) {
        fprintf(stderr, "error: missing required argument: DEST\n");
        return 2;
    }

    return 0;
}

int main(int argc, char **argv) {
    ParsedSampleCliNoCmd parsed = {0};
    int rc = 0;

    rc = opt_run_program_parser(&SampleCliNoCmd_parser, argc, argv, &parsed);

    if (rc != 0 || parsed.must_exit)
        return rc;

    if (parsed.top_level.version.value) {
        printf("sample-cli-nocmd %s\n", BUILD_IMGNEKO_VERSION);
        SampleCliNoCmdOptions_deinit(&parsed.top_level);
        return 0;
    }

    rc = validate_required_arguments(&parsed.top_level);
    if (rc != 0) {
        SampleCliNoCmdOptions_deinit(&parsed.top_level);
        return rc;
    }

    print_options(&parsed.top_level);
    SampleCliNoCmdOptions_deinit(&parsed.top_level);
    return 0;
}
