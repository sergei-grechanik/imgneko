#include "util/options.h"

// Sample CLI with commands and a top-level positional. This intentionally
// violates the parser contract so tests can verify that command-based parsers
// now reject top-level positional arguments up front.

#define TOP_LEVEL_OPTIONS(X, S)                                                \
    X(S, prefix, OptString,                                                    \
      OPT_STRING(.cli = "PREFIX", .descr = "Synthetic top-level positional.",  \
                 .positional = true))
OPT_DEFINE_STRUCT(TopLevelPositionalTopLevelOptions, TOP_LEVEL_OPTIONS)

#define SHOW_OPTIONS(X, S)                                                     \
    X(S, count, OptInt,                                                        \
      OPT_INT(.cli = "-n --count N", .descr = "Synthetic count.",              \
              .dflt = "3"))                                                    \
    X(S, image, OptString,                                                     \
      OPT_STRING(.cli = "IMAGE", .descr = "Synthetic image.",                  \
                 .positional = true, .nargs = "?"))
OPT_DEFINE_STRUCT(TopLevelPositionalShowOptions, SHOW_OPTIONS)

#define SAMPLE_CLI_TOP_LEVEL_POSITIONAL_COMMANDS(Name, X)                      \
    X(Name, show, TopLevelPositionalShowOptions,                               \
      OPT_COMMAND(.descr = "Exercise default-command selection after a "       \
                           "finite top-level positional."))

OPT_DEFINE_PROGRAM_PARSER_WITH_TOP_LEVEL_OPTIONS(
    SampleCliTopLevelPositional,
    OPT_PROGRAM(.program_name = "sample-cli-top-level-positional",
                .descr = "Sample CLI with a top-level positional before "
                         "command selection.",
                .default_command = "show"),
    TopLevelPositionalTopLevelOptions,
    SAMPLE_CLI_TOP_LEVEL_POSITIONAL_COMMANDS);

int main(int argc, char **argv) {
    ParsedSampleCliTopLevelPositional parsed = {0};

    return opt_run_program_parser(&SampleCliTopLevelPositional_parser, argc,
                                  argv, &parsed);
}
