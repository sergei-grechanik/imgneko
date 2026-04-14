#include <stdio.h>

#include "util/error.h"
#include "util/options.h"

// Sample CLI with top-level options and no default command. This exists to
// exercise the commandless-success path where parsing ends without selecting a
// command, leaving the caller to handle `OPT_CMD_NONE`.

#define TOP_LEVEL_OPTIONS(X, S)                                                \
    X(S, config, OptString,                                                    \
      OPT_STRING(.cli = "-c --config FILE",                                    \
                 .descr = "Read a synthetic config file."))                    \
    X(S, profile, OptString,                                                   \
      OPT_STRING(.cli = "--profile NAME",                                      \
                 .descr = "Select a synthetic top-level profile."))
OPT_DEFINE_STRUCT(TopLevelNoDefaultTopLevelOptions, TOP_LEVEL_OPTIONS)

#define SHOW_OPTIONS(X, S)                                                     \
    X(S, image, OptString,                                                     \
      OPT_STRING(.cli = "IMAGE", .descr = "Synthetic optional image.",         \
                 .positional = true, .nargs = "?"))
OPT_DEFINE_STRUCT(TopLevelNoDefaultShowOptions, SHOW_OPTIONS)

#define SAMPLE_CLI_TOP_LEVEL_NO_DEFAULT_COMMANDS(Name, X)                      \
    X(Name, show, TopLevelNoDefaultShowOptions,                                \
      OPT_COMMAND(.descr = "Exercise explicit command selection with "         \
                           "top-level options."))

OPT_DEFINE_PROGRAM_PARSER_WITH_TOP_LEVEL_OPTIONS(
    SampleCliTopLevelNoDefault,
    OPT_PROGRAM(.program_name = "sample-cli-top-level-no-default",
                .descr = "Sample CLI with top-level options and no default "
                         "command."),
    TopLevelNoDefaultTopLevelOptions, SAMPLE_CLI_TOP_LEVEL_NO_DEFAULT_COMMANDS);

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

// Print the parsed top-level values when no command was selected.
static int
process_top_level_options(const TopLevelNoDefaultTopLevelOptions *top_level) {
    printf("command: <none>\n");
    print_optional_string_option("config", top_level->config);
    print_optional_string_option("profile", top_level->profile);
    return 0;
}

// Print the parsed explicit command.
static int
process_show_command(const TopLevelNoDefaultTopLevelOptions *top_level,
                     const TopLevelNoDefaultShowOptions *options) {
    printf("command: show\n");
    print_optional_string_option("config", top_level->config);
    print_optional_string_option("profile", top_level->profile);
    print_optional_string_option("image", options->image);
    return 0;
}

int main(int argc, char **argv) {
    ParsedSampleCliTopLevelNoDefault parsed = {0};
    int rc = opt_run_program_parser(&SampleCliTopLevelNoDefault_parser, argc,
                                    argv, &parsed);

    if (rc != 0 || parsed.must_exit)
        return rc;

    switch (parsed.command_id) {
    case OPT_CMD_NONE:
        rc = process_top_level_options(&parsed.top_level);
        break;
    case OPT_CMD_SampleCliTopLevelNoDefault_show:
        rc = process_show_command(&parsed.top_level, &parsed.command.show);
        break;
    // IMGNEKO_UNCOVERED_OK[3 lines]: Should never happen.
    default:
        die("Unexpected command id");
        break;
    }

    opt_program_result_deinit(&SampleCliTopLevelNoDefault_parser, &parsed);
    return rc;
}
