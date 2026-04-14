#include <stdio.h>

#include "util/error.h"
#include "util/options.h"

// Sample CLI with a default command and no top-level schema. This exists to
// exercise the default-command help path where there are no global options.

#define SHOW_OPTIONS(X, S)                                                     \
    X(S, count, OptInt,                                                        \
      OPT_INT(.cli = "-n --count N", .descr = "Synthetic count.",              \
              .dflt = "3"))                                                    \
    X(S, image, OptString,                                                     \
      OPT_STRING(.cli = "IMAGE", .descr = "Synthetic optional image.",         \
                 .positional = true, .nargs = "?"))
OPT_DEFINE_STRUCT(DefaultNoTopLevelShowOptions, SHOW_OPTIONS)

#define SAMPLE_CLI_DEFAULT_NO_TOP_LEVEL_COMMANDS(Name, X)                      \
    X(Name, show, DefaultNoTopLevelShowOptions,                                \
      OPT_COMMAND(.descr = "Exercise default-command help without top-level "  \
                           "options."))

OPT_DEFINE_PROGRAM_PARSER(
    SampleCliDefaultNoTopLevel,
    OPT_PROGRAM(.program_name = "sample-cli-default-no-top-level",
                .descr = "Sample CLI with a default command and no top-level "
                         "schema.",
                .default_command = "show"),
    SAMPLE_CLI_DEFAULT_NO_TOP_LEVEL_COMMANDS);

// Print an integer option in a stable, test-friendly format.
static void print_int_option(const char *name, OptInt option) {
    printf("%s: %d (%s)\n", name, option.value,
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

// Print the parsed default command.
static int process_show_command(const DefaultNoTopLevelShowOptions *options) {
    printf("command: show\n");
    print_int_option("count", options->count);
    print_optional_string_option("image", options->image);
    return 0;
}

int main(int argc, char **argv) {
    ParsedSampleCliDefaultNoTopLevel parsed = {0};
    int rc = opt_run_program_parser(&SampleCliDefaultNoTopLevel_parser, argc,
                                    argv, &parsed);

    if (rc != 0 || parsed.must_exit)
        return rc;

    switch (parsed.command_id) { // IMGNEKO_UNCOVERED_OK[2 lines]
    case OPT_CMD_SampleCliDefaultNoTopLevel_show:
        rc = process_show_command(&parsed.command.show);
        break;
    // IMGNEKO_UNCOVERED_OK[3 lines]: Should never happen.
    default:
        die("Unexpected command id");
        break;
    }

    opt_program_result_deinit(&SampleCliDefaultNoTopLevel_parser, &parsed);
    return rc;
}
