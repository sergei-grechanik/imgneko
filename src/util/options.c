// SPDX-License-Identifier: MIT-0

#include "util/options.h"

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "util/error.h"

//------------------------------------------------------------------------------
// Internal Types
//------------------------------------------------------------------------------

// Internal parse result used while probing active schemas. `NO_MATCH` means
// "this token does not belong to this schema", `RETRY` means "reprocess this
// same raw argv entry after the caller updates parser mode", and HELP/ERROR are
// terminal outcomes for the whole parse.
typedef enum OptParseStatus {
    OPT_PARSE_STATUS_OK = 0,
    OPT_PARSE_STATUS_NO_MATCH,
    OPT_PARSE_STATUS_RETRY,
    OPT_PARSE_STATUS_HELP,
    OPT_PARSE_STATUS_ERROR,
} OptParseStatus;

// One whitespace-delimited token inside a field's combined CLI spec.
typedef struct OptCliToken {
    const char *text;
    size_t len;
} OptCliToken;

// Parsed cardinality for one field's value count.
typedef enum OptNargsKind {
    OPT_NARGS_ONE = 0,
    OPT_NARGS_OPTIONAL,
    OPT_NARGS_ZERO_OR_MORE,
    OPT_NARGS_ONE_OR_MORE,
} OptNargsKind;

// One caller-provided option lookup key used for either long or short names.
typedef struct OptOptionQuery {
    const char *long_name;
    size_t long_name_len;
    char short_name;
    bool is_long;
} OptOptionQuery;

// Shared signature for the raw long/short option token parsers.
// `arg` is the current raw argv token, while `argc`/`argv` plus the mutable
// `arg_index` let one parser consume an additional argv entry when the option
// takes a separate value. `schema` and `options` are passed in explicitly so
// the same parser logic can be reused for the active schema and the top-level
// fallback schema.
typedef OptParseStatus (*OptOptionParseFn)(const OptSchema *schema,
                                           const char *arg, int argc,
                                           char **argv, int *arg_index,
                                           void *options);

// Mutable state for one opt_run_program_parser() invocation.
// The run context keeps the current argv cursor, the selected command (if any),
// and the two positional cursors that advance independently for top-level and
// command-local schemas. `help_requested` remembers a deferred `--help` so we
// can keep parsing far enough to choose the right help message, and
// `selected_command_explicitly` distinguishes an explicit command name from an
// implicit default-command activation.
typedef struct OptRunCtx {
    const OptProgramParser *parser;            // Static parser definition.
    int argc;                                  // Full argv length.
    char **argv;                               // Raw argv pointer.
    void *result;                              // Generated Parsed<Name>.
    const OptProgramCommand *default_command;  // Cached default command.
    const OptProgramCommand *selected_command; // Active command, if any.
    void *top_level_options;                   // Parsed top-level storage.
    size_t top_level_positional_index;         // Next top-level positional.
    size_t command_positional_index;           // Next command positional.
    int arg_index;                             // Current raw argv index.
    bool stop_options;                         // True after bare `--`.
    bool help_requested;                       // Any `-h`/`--help` seen.
    bool selected_command_explicitly;          // False for default command.
} OptRunCtx;

//------------------------------------------------------------------------------
// Internal Forward Declarations
//------------------------------------------------------------------------------

static OptParseStatus opt_assign_field_or_fail(const OptFieldSpec *field,
                                               void *options,
                                               const char *value_text,
                                               size_t value_text_len,
                                               const char *diagnostic_value);
static OptParseStatus opt_assign_cli_option(const OptFieldSpec *field,
                                            void *options,
                                            const char *value_text,
                                            bool is_negated,
                                            const char *diagnostic_value);
static bool opt_append_option_aliases(String *out, const char *cli_spec,
                                      bool *first_out);
static bool opt_find_first_option_alias(const char *cli_spec,
                                        OptCliToken *alias_out);
static bool opt_append_first_option_alias(String *out, const char *cli_spec);
static void opt_require_option_aliases(const OptFieldSpec *field);
static const OptProgramCommand *
opt_find_program_command(const OptProgramParser *parser, const char *name);
static const OptProgramCommand *
opt_find_program_command_by_id(const OptProgramParser *parser, int command_id);
static bool opt_schema_has_any_value(const OptSchema *schema,
                                     const void *options);
static bool *opt_result_must_exit_ptr(const OptProgramParser *parser,
                                      void *result);
static int opt_return_parse_status(const OptProgramParser *parser, void *result,
                                   OptParseStatus status);
static int opt_finish_program_parse(const OptProgramParser *parser,
                                    void *result, OptParseStatus status);
static void opt_run_ctx_init(OptRunCtx *ctx, const OptProgramParser *parser,
                             int argc, char **argv, void *result);
static bool opt_run_ctx_awaiting_command(const OptRunCtx *ctx);
static bool opt_run_ctx_has_active_command(const OptRunCtx *ctx);
static void
opt_run_ctx_activate_command(OptRunCtx *ctx,
                             const OptProgramCommand *program_command);
static const OptSchema *opt_run_ctx_primary_schema(const OptRunCtx *ctx);
static void *opt_run_ctx_primary_options(OptRunCtx *ctx);
static size_t *opt_run_ctx_primary_positional_index(OptRunCtx *ctx);
static bool opt_run_ctx_has_top_level_option_fallback(const OptRunCtx *ctx);
static bool
opt_run_ctx_next_positional_requires_double_dash(const OptRunCtx *ctx);
static OptParseStatus opt_parse_long_option_text(const OptSchema *schema,
                                                 const char *arg, int argc,
                                                 char **argv, int *arg_index,
                                                 void *options);
static OptParseStatus opt_parse_short_option_text(const OptSchema *schema,
                                                  const char *arg, int argc,
                                                  char **argv, int *arg_index,
                                                  void *options);
static const OptProgramCommand *
opt_require_default_program_command(const OptProgramParser *parser);
static void opt_print_requested_help(const OptRunCtx *ctx);
static OptParseStatus opt_finish_requested_help(const OptRunCtx *ctx);
static void opt_note_later_help_request(OptRunCtx *ctx);
static OptParseStatus
opt_try_parse_active_option(OptRunCtx *ctx, const char *arg,
                            OptOptionParseFn parse_option_fn);
static OptParseStatus opt_try_assign_active_positional(OptRunCtx *ctx,
                                                       const char *arg);
static bool opt_try_activate_matching_command(OptRunCtx *ctx, const char *arg);
static bool opt_try_activate_default_command(OptRunCtx *ctx);
static OptParseStatus opt_handle_double_dash(OptRunCtx *ctx);
static OptParseStatus opt_process_arg_item(OptRunCtx *ctx);
static OptParseStatus opt_finalize_run_ctx(OptRunCtx *ctx);

//------------------------------------------------------------------------------
// Small Public Helpers
//------------------------------------------------------------------------------

// Return a human-readable provenance name.
const char *opt_provenance_name(OptProvenance provenance) {
    switch (provenance) {
    case OPT_PROVENANCE_NONE:
        return "none";
    case OPT_PROVENANCE_DEFAULT:
        return "default";
    case OPT_PROVENANCE_CLI:
        return "cli";
    }

    return "unknown";
}

// Store a short parse-error reason when the caller requested it, then return
// false so callers can use this in one-line error paths.
bool opt_parse_error(String *error_out, const char *message) {
    if (error_out == NULL)
        return false;

    str_free(*error_out);
    *error_out = str_from_cstr(message);
    return false;
}

//------------------------------------------------------------------------------
// CLI Spec and Field Metadata Helpers
//------------------------------------------------------------------------------

// Parse a whitespace-delimited CLI spec token.
static bool opt_next_cli_token(const char *spec, size_t *cursor,
                               OptCliToken *token_out) {
    size_t i = *cursor;

    if (spec == NULL)
        return false;

    while (spec[i] != '\0' && str_char_is_ascii_space(spec[i]))
        ++i;
    if (spec[i] == '\0') {
        *cursor = i;
        return false;
    }

    size_t start = i;

    while (spec[i] != '\0' && !str_char_is_ascii_space(spec[i]))
        ++i;

    token_out->text = spec + start;
    token_out->len = i - start;
    *cursor = i;
    return true;
}

// Return true when token is any option name, not a metavar.
static bool opt_cli_token_is_option(OptCliToken token) {
    // IMGNEKO_UNCOVERED_OK: len check is defensive
    return token.len != 0 && token.text[0] == '-';
}

// Append an upper-case field name fallback suitable for metavars.
static void opt_append_upper_field_name(String *out, const char *field_name) {
    for (size_t i = 0; field_name[i] != '\0'; ++i) {
        char ch = field_name[i];

        if (ch == '_') {
            str_push(*out, '_');
            continue;
        }

        if (str_char_is_ascii_lower(ch))
            str_push(*out, (char)(ch - 'a' + 'A'));
        else
            str_push(*out, ch);
    }
}

// Return true when a field consumes an argument value.
static bool opt_field_takes_value(const OptFieldSpec *field) {
    return field->attrs.bool_mode == OPT_BOOL_MODE_NONE ||
           field->attrs.bool_mode == OPT_BOOL_MODE_EXPLICIT;
}

// Return true when a field is a negatable boolean long option.
static bool opt_field_is_negatable_bool(const OptFieldSpec *field) {
    return field->attrs.bool_mode == OPT_BOOL_MODE_NEGATABLE;
}

// Return the parsed cardinality for one field's value count.
static OptNargsKind opt_field_nargs_kind(const OptFieldSpec *field) {
    const char *nargs = field->attrs.nargs;

    if (nargs == NULL || strcmp(nargs, "1") == 0)
        return OPT_NARGS_ONE;
    if (strcmp(nargs, "?") == 0)
        return OPT_NARGS_OPTIONAL;
    if (strcmp(nargs, "*") == 0)
        return OPT_NARGS_ZERO_OR_MORE;
    // IMGNEKO_UNCOVERED_OK: Always true at this point.
    if (strcmp(nargs, "+") == 0)
        return OPT_NARGS_ONE_OR_MORE;

    // IMGNEKO_UNCOVERED_OK[2 lines]
    require(false, "option field has an unsupported nargs value");
    return OPT_NARGS_ONE;
}

// Return true when a field can accumulate multiple values.
static bool opt_field_is_multi_valued(const OptFieldSpec *field) {
    OptNargsKind nargs_kind = opt_field_nargs_kind(field);
    return nargs_kind == OPT_NARGS_ZERO_OR_MORE ||
           nargs_kind == OPT_NARGS_ONE_OR_MORE;
}

// Return true when a positional field is only valid after a bare `--`.
static bool opt_field_requires_double_dash(const OptFieldSpec *field) {
    // IMGNEKO_UNCOVERED_OK[2 lines]: Defensive, always called for positionals.
    if (!field->attrs.positional)
        return false;
    return field->attrs.double_dash_only;
}

// Return true when a field's cardinality is optional in usage text.
static bool opt_field_usage_is_optional(const OptFieldSpec *field) {
    OptNargsKind nargs_kind = opt_field_nargs_kind(field);
    return nargs_kind == OPT_NARGS_OPTIONAL ||
           nargs_kind == OPT_NARGS_ZERO_OR_MORE;
}

// Return the first non-option token from a CLI spec, if any.
static bool opt_field_metavar_token(const OptFieldSpec *field,
                                    OptCliToken *token_out) {
    size_t cursor = 0;
    OptCliToken token;

    while (opt_next_cli_token(field->attrs.cli, &cursor, &token)) {
        if (!opt_cli_token_is_option(token)) {
            *token_out = token;
            return true;
        }
    }

    return false;
}

// Build a long-option query key.
static OptOptionQuery opt_long_option_query(const char *option_name,
                                            size_t option_name_len) {
    return (OptOptionQuery){
        .long_name = option_name,
        .long_name_len = option_name_len,
        .is_long = true,
    };
}

// Build a short-option query key.
static OptOptionQuery opt_short_option_query(char option_name) {
    return (OptOptionQuery){
        .short_name = option_name,
        .is_long = false,
    };
}

// Return true when one CLI alias token matches one caller-provided option key.
static bool opt_cli_token_matches_option(OptCliToken token,
                                         const OptOptionQuery *query) {
    if (query->is_long) {
        // IMGNEKO_UNCOVERED_OK[4 lines]: Callers only pass option alias tokens
        // here.
        return token.len > 2 && token.text[0] == '-' && token.text[1] == '-' &&
               token.len - 2 == query->long_name_len &&
               memcmp(token.text + 2, query->long_name, query->long_name_len) ==
                   0;
    }

    // IMGNEKO_UNCOVERED_OK[2 lines]: Callers only pass option alias tokens
    // here.
    return token.len == 2 && token.text[0] == '-' && token.text[1] != '-' &&
           token.text[1] == query->short_name;
}

// Return true when one option query matches any option alias from one CLI
// spec.
static bool opt_spec_matches_option(const char *cli_spec,
                                    const OptOptionQuery *query) {
    size_t cursor = 0;
    OptCliToken token;

    while (opt_next_cli_token(cli_spec, &cursor, &token)) {
        if (!opt_cli_token_is_option(token))
            break;
        if (opt_cli_token_matches_option(token, query))
            return true;
    }

    return false;
}

// Return true when one option query matches any alias of one field. When the
// match came from a negated alias, store that in `is_negated_out`.
static bool opt_field_matches_option(const OptFieldSpec *field,
                                     const OptOptionQuery *query,
                                     bool *is_negated_out) {
    // Negate-only options are allowed, so the primary alias set may be absent.
    if (field->attrs.cli != NULL &&
        opt_spec_matches_option(field->attrs.cli, query)) {
        if (is_negated_out != NULL) // IMGNEKO_UNCOVERED_OK
            *is_negated_out = false;
        return true;
    }

    if (opt_field_is_negatable_bool(field) && field->attrs.cli_negate != NULL &&
        opt_spec_matches_option(field->attrs.cli_negate, query)) {
        if (is_negated_out != NULL) // IMGNEKO_UNCOVERED_OK
            *is_negated_out = true;
        return true;
    }

    return false;
}

//------------------------------------------------------------------------------
// Help Label and Display Helpers
//------------------------------------------------------------------------------

// Append all option aliases from one CLI spec to one help label exactly as they
// appear in the declaration order.
static bool opt_append_option_aliases(String *out, const char *cli_spec,
                                      bool *first_out) {
    size_t cursor = 0;
    OptCliToken token;
    bool appended = false;

    while (opt_next_cli_token(cli_spec, &cursor, &token)) {
        if (!opt_cli_token_is_option(token))
            break;
        if (!*first_out)
            str_append_cstr(*out, ", ");
        str_append_data(*out, token.text, token.len);
        *first_out = false;
        appended = true;
    }

    return appended;
}

// Return the first option alias from one CLI spec, preferring a long alias
// when present and otherwise falling back to the first short alias.
static bool opt_find_first_option_alias(const char *cli_spec,
                                        OptCliToken *alias_out) {
    size_t cursor = 0;
    OptCliToken token;
    OptCliToken fallback = {0};

    while (opt_next_cli_token(cli_spec, &cursor, &token)) {
        if (!opt_cli_token_is_option(token))
            break;
        // CLI specs only support single-character short aliases, so any longer
        // option token here is necessarily a long alias.
        if (token.len > 2) {
            *alias_out = token;
            return true;
        }
        if (fallback.len == 0)
            fallback = token;
    }

    if (fallback.len == 0)
        return false;

    *alias_out = fallback;
    return true;
}

// Append the first option alias from one CLI spec, preferring a long alias
// when present and otherwise falling back to the first short alias.
static bool opt_append_first_option_alias(String *out, const char *cli_spec) {
    OptCliToken alias;

    if (!opt_find_first_option_alias(cli_spec, &alias))
        return false;

    str_append_data(*out, alias.text, alias.len);
    return true;
}

// Write a raw CLI token to a stream without requiring NUL-termination.
static void opt_fprint_cli_token(FILE *out, OptCliToken token) {
    fwrite(token.text, 1, token.len, out);
}

// Require that one non-positional field declare at least one CLI alias.
static void opt_require_option_aliases(const OptFieldSpec *field) {
    // IMGNEKO_UNCOVERED_OK[3 lines]
    if (!field->attrs.positional) {
        require(field->attrs.cli != NULL || field->attrs.cli_negate != NULL,
                "option field is missing CLI aliases");
    }
}

// Append the field's preferred metavar or a generated fallback.
static void opt_append_metavar(String *out, const OptFieldSpec *field) {
    OptCliToken metavar;

    if (opt_field_metavar_token(field, &metavar)) {
        str_append_data(*out, metavar.text, metavar.len);
        return;
    }

    opt_append_upper_field_name(out, field->name);
}

// Append the field's display name for diagnostics.
static void opt_append_field_display_name(String *out,
                                          const OptFieldSpec *field) {
    if (field->attrs.positional) {
        opt_append_metavar(out, field);
        return;
    }

    opt_require_option_aliases(field);

    // Diagnostics prefer a primary long alias, then any primary alias, and
    // finally the corresponding negated aliases when this is a negate-only
    // option.
    // IMGNEKO_UNCOVERED_OK[2 lines]
    if (opt_append_first_option_alias(out, field->attrs.cli) ||
        opt_append_first_option_alias(out, field->attrs.cli_negate)) {
        return;
    }

    // IMGNEKO_UNCOVERED_OK
    die("option field is missing CLI aliases");
}

// Write the field's display name for diagnostics.
static void opt_fprint_field_display_name(FILE *out,
                                          const OptFieldSpec *field) {
    OptCliToken alias;

    opt_require_option_aliases(field);

    // IMGNEKO_UNCOVERED_OK[2 lines]
    if (opt_find_first_option_alias(field->attrs.cli, &alias) ||
        opt_find_first_option_alias(field->attrs.cli_negate, &alias)) {
        opt_fprint_cli_token(out, alias);
        return;
    }

    // IMGNEKO_UNCOVERED_OK
    die("option field is missing CLI aliases");
}

// Append the option label used in the generated help output.
static void opt_append_option_label(String *out, const OptFieldSpec *field) {
    bool first = true;

    opt_require_option_aliases(field);

    // Print aliases exactly as declared so help text reflects the real parser
    // surface. Positive aliases are listed first. For negatable booleans, the
    // negated aliases are listed after a '/'.
    bool appended_primary =
        opt_append_option_aliases(out, field->attrs.cli, &first);

    if (opt_field_is_negatable_bool(field)) {
        if (field->attrs.cli_negate != NULL) {
            if (appended_primary) {
                String negated_label = str_empty;
                bool first_negated = true;

                if (opt_append_option_aliases(&negated_label,
                                              field->attrs.cli_negate,
                                              &first_negated)) {
                    str_append_cstr(*out, " / ");
                    str_append_cstr(*out, negated_label.cstr);
                }

                str_free(negated_label);
            } else {
                opt_append_option_aliases(out, field->attrs.cli_negate, &first);
            }
        }
    }

    require(!first, "option field is missing CLI aliases");

    if (opt_field_takes_value(field)) {
        str_push(*out, ' ');
        opt_append_metavar(out, field);
        if (opt_field_is_multi_valued(field))
            str_append_cstr(*out, "...");
    }
}

// Append the positional label used in the generated help output.
static void opt_append_positional_label(String *out,
                                        const OptFieldSpec *field) {
    opt_append_metavar(out, field);
    if (opt_field_is_multi_valued(field))
        str_append_cstr(*out, "...");
}

// Return true when help text should omit one field's default annotation because
// the default is implicit in the option syntax.
static bool opt_field_hides_default_text(const OptFieldSpec *field) {
    return !opt_field_takes_value(field) &&
           field->attrs.bool_mode == OPT_BOOL_MODE_FLAG;
}

// Append a default-value annotation to help text.
static void opt_append_default_text(String *out, const OptFieldSpec *field) {
    if (field->attrs.dflt == NULL)
        return;
    if (opt_field_hides_default_text(field))
        return;

    str_append_cstr(*out, " (default: ");
    str_append_cstr(*out, field->attrs.dflt);
    str_push(*out, ')');
}

//------------------------------------------------------------------------------
// Help Rendering Helpers
//------------------------------------------------------------------------------

// Clamp an externally reported terminal width to a sane help-rendering width.
static size_t opt_clamp_terminal_width(unsigned long reported_width) {
    if (reported_width == 0)
        return 80;
    if (reported_width < 40)
        return 40;
    return (size_t)reported_width;
}

// Return a reasonable terminal width for wrapped help output.
static size_t opt_terminal_width(void) {
    const char *columns_text = getenv("COLUMNS");
    char *end = NULL;
    unsigned long columns = 0;
    struct winsize size;

    if (columns_text != NULL && columns_text[0] != '\0') {
        errno = 0;
        columns = strtoul(columns_text, &end, 10);
        if (errno == 0 && *end == '\0')
            return opt_clamp_terminal_width(columns);
    }

    // IMGNEKO_UNCOVERED_OK[2 lines]: TIOCGWINSZ failure is hard to inject.
    if (isatty(STDOUT_FILENO) && ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0) {
        return opt_clamp_terminal_width(size.ws_col);
    }

    return 80;
}

// Return the description-column indent for one wrapped help block.
static size_t opt_help_description_indent(size_t width) {
    size_t description_indent = 28;

    // Keep roughly twenty columns for description text on mid-width terminals.
    // Truly narrow terminals instead fall back to printing the label on its own
    // line and using a small continuation indent for the description.
    if (description_indent + 20 > width)
        return width / 3;

    return description_indent;
}

// Print a wrapped description paragraph aligned under label.
static void opt_print_wrapped_kv(FILE *out, const char *label,
                                 const char *description, size_t width) {
    size_t label_width = strlen(label);
    size_t description_indent = opt_help_description_indent(width);
    size_t column = 0;
    const char *cursor = description;

    // When the description is empty, print the label without trailing padding
    // to avoid trailing whitespace in the output.
    if (description[0] == '\0') {
        fprintf(out, "  %s\n", label);
        return;
    }

    if (label_width + 2 >= description_indent) {
        fprintf(out, "  %s\n", label);
        for (size_t i = 0; i < description_indent; ++i)
            fputc(' ', out);
        column = description_indent;
    } else {
        // `%-*s` left-aligns the label inside the computed label column width.
        fprintf(out, "  %-*s", (int)(description_indent - 2), label);
        column = description_indent;
    }

    // Reflow the description one word at a time. Once the current line would
    // exceed `width`, start a continuation line aligned under the description
    // column rather than under the option label.
    while (*cursor != '\0') {
        while (*cursor == ' ')
            ++cursor;
        if (*cursor == '\0')
            break;

        const char *word = cursor;
        size_t word_len = 0;

        while (cursor[word_len] != '\0' && cursor[word_len] != ' ')
            ++word_len;

        if (column > description_indent && column + 1 + word_len > width) {
            fputc('\n', out);
            for (size_t i = 0; i < description_indent; ++i)
                fputc(' ', out);
            fwrite(word, 1, word_len, out);
            column = description_indent + word_len;
        } else {
            if (column > description_indent) {
                fputc(' ', out);
                ++column;
            }
            fwrite(word, 1, word_len, out);
            column += word_len;
        }

        cursor += word_len;
    }

    fputc('\n', out);
}

// Print an option/help section entry.
static void opt_print_field_help(FILE *out, const OptFieldSpec *field,
                                 bool positional_section, size_t width) {
    String label = str_empty;
    String description = str_empty;

    // Build the label (option aliases and metavar).
    if (positional_section)
        opt_append_positional_label(&label, field);
    else
        opt_append_option_label(&label, field);

    // Build the description.
    if (field->attrs.descr != NULL)
        str_append_cstr(description, field->attrs.descr);
    opt_append_default_text(&description, field);

    // Print the entry with the label and description aligned.
    opt_print_wrapped_kv(out, label.cstr, description.cstr, width);

    str_free(description);
    str_free(label);
}

// Print all non-positional fields from one schema in declaration order.
static void opt_print_schema_options_help(FILE *out, const OptSchema *schema,
                                          size_t width) {
    for (size_t i = 0; i < schema->field_count; ++i) {
        const OptFieldSpec *field = &schema->fields[i];

        if (field->attrs.positional)
            continue;
        opt_print_field_help(out, field, false, width);
    }
}

// Return true when schema declares at least one positional field.
static bool opt_schema_has_positionals(const OptSchema *schema) {
    if (schema == NULL)
        return false;

    for (size_t i = 0; i < schema->field_count; ++i) {
        if (schema->fields[i].attrs.positional)
            return true;
    }

    return false;
}

// Print the positional-argument help section for a schema when it has any
// positional fields. Returns true when anything was printed.
static bool opt_print_schema_positionals_help(FILE *out,
                                              const OptSchema *schema,
                                              size_t width) {
    assert(schema != NULL);
    bool printed_positionals = false;

    for (size_t i = 0; i < schema->field_count; ++i) {
        const OptFieldSpec *field = &schema->fields[i];

        if (!field->attrs.positional)
            continue;
        if (!printed_positionals) {
            fputs("Positional arguments:\n", out);
            printed_positionals = true;
        }
        opt_print_field_help(out, field, true, width);
    }

    return printed_positionals;
}

// Append a positional usage fragment such as ` FILE` or ` [TARGET...]`.
static void opt_append_usage_positional(String *out,
                                        const OptFieldSpec *field) {
    str_push(*out, ' ');
    if (opt_field_usage_is_optional(field))
        str_push(*out, '[');
    opt_append_positional_label(out, field);
    if (opt_field_usage_is_optional(field))
        str_push(*out, ']');
}

// Append the positional-only usage fragments from a schema.
static void opt_append_schema_positional_usage(String *out,
                                               const OptSchema *schema) {
    assert(schema != NULL);
    bool printed_double_dash_boundary = false;

    for (size_t i = 0; i < schema->field_count; ++i) {
        const OptFieldSpec *field = &schema->fields[i];

        if (!field->attrs.positional)
            continue;
        if (opt_field_requires_double_dash(field) &&
            !printed_double_dash_boundary) {
            str_append_cstr(*out, " --");
            printed_double_dash_boundary = true;
        }

        opt_append_usage_positional(out, field);
    }
}

// Print the shared tail of one usage line after the program/command prefix.
static void opt_print_schema_usage_suffix(FILE *out, const OptSchema *schema,
                                          bool has_leading_options,
                                          const char *options_label) {
    bool has_positional = false;
    bool has_double_dash_only_positional = false;
    bool has_options = has_leading_options;

    for (size_t i = 0; i < schema->field_count; ++i) {
        const OptFieldSpec *field = &schema->fields[i];

        if (field->attrs.positional) {
            has_positional = true;
            if (opt_field_requires_double_dash(field))
                has_double_dash_only_positional = true;
        } else {
            has_options = true;
        }
    }

    if (has_options)
        fprintf(out, " [%s]", options_label);
    if (has_positional && has_options && !has_double_dash_only_positional)
        fputs(" [--]", out);

    String positional_usage = str_empty;
    opt_append_schema_positional_usage(&positional_usage, schema);
    fputs(positional_usage.cstr, out);
    str_free(positional_usage);

    fputc('\n', out);
}

//------------------------------------------------------------------------------
// Schema Lookup and Parser State Helpers
//------------------------------------------------------------------------------

// Find a field by one caller-provided option key.
static const OptFieldSpec *opt_find_option(const OptSchema *schema,
                                           const OptOptionQuery *query,
                                           bool *is_negated_out) {
    for (size_t i = 0; i < schema->field_count; ++i) {
        const OptFieldSpec *field = &schema->fields[i];
        bool is_negated = false;

        if (field->attrs.positional)
            continue;
        if (opt_field_matches_option(field, query, &is_negated)) {
            // IMGNEKO_UNCOVERED_OK[2 lines]
            if (is_negated_out != NULL)
                *is_negated_out = is_negated;
            return field;
        }
    }

    return NULL;
}

// Return the next positional field in schema order. If (*field_index)-th field
// is positional, return it, otherwise advance `field_index` until we find a
// positional field.
static const OptFieldSpec *opt_next_positional_field(const OptSchema *schema,
                                                     size_t *field_index) {
    while (*field_index < schema->field_count) {
        const OptFieldSpec *field = &schema->fields[*field_index];

        if (field->attrs.positional)
            return field;
        ++(*field_index);
    }

    return NULL;
}

// Return a pointer to the .must_exit field in the parsing result object.
static bool *opt_result_must_exit_ptr(const OptProgramParser *parser,
                                      void *result) {
    return (bool *)((char *)result + parser->must_exit_offset);
}

// Translate an internal parse status into the public exit code and must_exit
// flag.
static int opt_return_parse_status(const OptProgramParser *parser, void *result,
                                   OptParseStatus status) {
    // IMGNEKO_UNCOVERED_OK[3 lines]
    require(status != OPT_PARSE_STATUS_NO_MATCH &&
                status != OPT_PARSE_STATUS_RETRY,
            "internal parser-only status escaped to public result");
    *opt_result_must_exit_ptr(parser, result) =
        status == OPT_PARSE_STATUS_HELP || status == OPT_PARSE_STATUS_ERROR;
    return status == OPT_PARSE_STATUS_ERROR ? 2 : 0;
}

// Tear down any initialized parsed data for non-successful exits, then return
// the matching public exit code.
static int opt_finish_program_parse(const OptProgramParser *parser,
                                    void *result, OptParseStatus status) {
    if (status != OPT_PARSE_STATUS_OK)
        opt_program_result_deinit(parser, result);
    return opt_return_parse_status(parser, result, status);
}

// Initialize a program-parser run context and the destination result object.
static void opt_run_ctx_init(OptRunCtx *ctx, const OptProgramParser *parser,
                             int argc, char **argv, void *result) {
    memset(result, 0, parser->result_size);

    *ctx = (OptRunCtx){
        .parser = parser,
        .argc = argc,
        .argv = argv,
        .result = result,
        .default_command = opt_require_default_program_command(parser),
        .top_level_options = parser->top_level_schema != NULL
                                 ? (char *)result + parser->top_level_offset
                                 : NULL,
    };

    // IMGNEKO_UNCOVERED_OK[2 lines]
    require(parser->command_count != 0 || parser->top_level_schema != NULL,
            "no-command parser requires a top-level schema");
    require(parser->command_count == 0 ||
                !opt_schema_has_positionals(parser->top_level_schema),
            "program parsers with commands do not support top-level positional "
            "arguments");
    if (parser->top_level_schema != NULL)
        opt_schema_init(parser->top_level_schema, ctx->top_level_options);
}

// Return true when the parser is still deciding which command, if any, should
// handle subsequent tokens.
static bool opt_run_ctx_awaiting_command(const OptRunCtx *ctx) {
    return ctx->parser->command_count != 0 && ctx->selected_command == NULL;
}

// Return true when one command has already been selected.
static bool opt_run_ctx_has_active_command(const OptRunCtx *ctx) {
    return ctx->selected_command != NULL;
}

// Activate a command and switch the run context to that command schema.
static void
opt_run_ctx_activate_command(OptRunCtx *ctx,
                             const OptProgramCommand *program_command) {
    int *command_id_ptr =
        (int *)((char *)ctx->result + ctx->parser->command_id_offset);

    require(program_command != NULL, "missing selected program command");
    require(*command_id_ptr == OPT_CMD_NONE,
            "program parser selected multiple commands");

    opt_schema_init(program_command->command->schema,
                    (char *)ctx->result + ctx->parser->command_offset);
    *command_id_ptr = program_command->command_id;
    ctx->selected_command = program_command;
    ctx->command_positional_index = 0;
}

// Return the primary schema that is active right now: if a command is active,
// it's the command schema, and otherwise it's the top-level schema.
static const OptSchema *opt_run_ctx_primary_schema(const OptRunCtx *ctx) {
    if (ctx->selected_command != NULL)
        return ctx->selected_command->command->schema;
    return ctx->parser->top_level_schema;
}

// Return the option storage that matches the current primary schema.
static void *opt_run_ctx_primary_options(OptRunCtx *ctx) {
    if (ctx->selected_command != NULL)
        return (char *)ctx->result + ctx->parser->command_offset;
    return ctx->top_level_options;
}

// Return the positional-field cursor for the current primary schema.
static size_t *opt_run_ctx_primary_positional_index(OptRunCtx *ctx) {
    if (ctx->selected_command != NULL)
        return &ctx->command_positional_index;
    if (ctx->parser->top_level_schema == NULL)
        return NULL;
    return &ctx->top_level_positional_index;
}

// Return true when selected-command parsing should still fall back to
// top-level option aliases. True when there is a selected command (so the
// top-level schema is not primary) and there is a top-level schema.
static bool opt_run_ctx_has_top_level_option_fallback(const OptRunCtx *ctx) {
    return ctx->selected_command != NULL &&
           ctx->parser->top_level_schema != NULL; // IMGNEKO_UNCOVERED_OK
}

// Return true when the next positional slot exists but is blocked until a bare
// `--` switches the parser into positional-only mode.
static bool
opt_run_ctx_next_positional_requires_double_dash(const OptRunCtx *ctx) {
    const OptSchema *schema = opt_run_ctx_primary_schema(ctx);
    size_t positional_field_index = 0;
    const OptFieldSpec *field = NULL;

    if (ctx->stop_options)
        return false;
    // IMGNEKO_UNCOVERED_OK[2 lines]: Defensive
    if (schema == NULL)
        return false;

    if (ctx->selected_command != NULL)
        positional_field_index = ctx->command_positional_index;
    else
        positional_field_index = ctx->top_level_positional_index;

    field = opt_next_positional_field(schema, &positional_field_index);
    if (field == NULL)
        return false;
    return opt_field_requires_double_dash(field);
}

// Return the next positional field that is currently legal to consume. Before
// a bare `--`, the next `double_dash_only` positional blocks all later
// positionals in the same schema so declaration order stays meaningful.
static const OptFieldSpec *
opt_next_assignable_positional_field(const OptSchema *schema,
                                     size_t *field_index, bool stop_options) {
    const OptFieldSpec *field = opt_next_positional_field(schema, field_index);

    if (field == NULL)
        return NULL;
    if (!stop_options && opt_field_requires_double_dash(field))
        return NULL;
    return field;
}

//------------------------------------------------------------------------------
// Built-in Value Parsing and Storage Helpers
//------------------------------------------------------------------------------

// Return true when the raw text matches the literal string exactly.
static bool opt_text_equals(const char *text, size_t text_len,
                            const char *literal) {
    size_t literal_len = strlen(literal);
    return text_len == literal_len && memcmp(text, literal, text_len) == 0;
}

// Return true when the raw text matches the ASCII literal ignoring case.
static bool opt_text_equals_ignore_case(const char *text, size_t text_len,
                                        const char *literal) {
    size_t literal_len = strlen(literal);

    if (text_len != literal_len)
        return false;

    for (size_t i = 0; i < text_len; ++i) {
        char lhs = text[i];
        char rhs = literal[i];

        if (str_char_is_ascii_upper(lhs))
            lhs = (char)(lhs - 'A' + 'a');
        // IMGNEKO_UNCOVERED_OK[2 lines]: Callers only pass ASCII literals here.
        if (str_char_is_ascii_upper(rhs))
            rhs = (char)(rhs - 'A' + 'a');
        if (lhs != rhs)
            return false;
    }

    return true;
}

// Parse a strict decimal integer from raw text into out.
bool opt_parse_int_span(const char *text, size_t text_len, int *out) {
    char parsed_text[64];
    char *end = NULL;
    long parsed = 0;

    if (text == NULL || text_len == 0)
        return false;
    if (text_len >= sizeof(parsed_text))
        return false;

    memcpy(parsed_text, text, text_len);
    parsed_text[text_len] = '\0';

    errno = 0;
    parsed = strtol(parsed_text, &end, 10);
    if (errno != 0 || *end != '\0')
        return false;
    if (parsed < INT_MIN || parsed > INT_MAX)
        return false;

    *out = (int)parsed;
    return true;
}

// Parse a strict floating-point value from raw text into out.
bool opt_parse_double_span(const char *text, size_t text_len, double *out) {
    char parsed_text[64];
    char *end = NULL;
    double parsed = 0.0;

    if (text == NULL || text_len == 0)
        return false;
    if (text_len >= sizeof(parsed_text))
        return false;

    memcpy(parsed_text, text, text_len);
    parsed_text[text_len] = '\0';

    errno = 0;
    parsed = strtod(parsed_text, &end);
    if (errno != 0 || *end != '\0')
        return false;

    *out = parsed;
    return true;
}

// Parse a bool value from an explicit or synthesized textual value.
bool opt_parse_bool_option(void *value_ptr, const char *text, size_t text_len,
                           String *error_out) {
    bool *value = value_ptr;
    bool parsed = true;

    if (text == NULL)
        return opt_parse_error(error_out, "value is required");

    if (opt_text_equals_ignore_case(text, text_len, "true") ||
        opt_text_equals(text, text_len, "1") ||
        opt_text_equals_ignore_case(text, text_len, "yes") ||
        opt_text_equals_ignore_case(text, text_len, "on")) {
        parsed = true;
    } else if (opt_text_equals_ignore_case(text, text_len, "false") ||
               opt_text_equals(text, text_len, "0") ||
               opt_text_equals_ignore_case(text, text_len, "no") ||
               opt_text_equals_ignore_case(text, text_len, "off")) {
        parsed = false;
    } else {
        return opt_parse_error(
            error_out,
            "expected one of true, false, yes, no, on, off, 1, or 0");
    }

    *value = parsed;
    return true;
}

// Parse a floating-point value from a textual number.
bool opt_parse_double_option(void *value, const char *text, size_t text_len,
                             String *error_out) {
    if (!opt_parse_double_span(text, text_len, value))
        return opt_parse_error(error_out, "expected a number");
    return true;
}

// Parse an int value from a textual decimal integer.
bool opt_parse_int_option(void *value, const char *text, size_t text_len,
                          String *error_out) {
    if (!opt_parse_int_span(text, text_len, value))
        return opt_parse_error(error_out, "expected a base-10 integer");
    return true;
}

// Validate that an already parsed int is positive.
bool opt_validate_positive_int(const void *value_ptr, String *error_out) {
    const int *int_value = value_ptr;

    if (*int_value <= 0)
        return opt_parse_error(error_out, "must be positive");
    return true;
}

// Validate that an already parsed double is finite and non-negative.
bool opt_validate_non_negative_double(const void *value_ptr,
                                      String *error_out) {
    const double *double_value = value_ptr;

    if (!isfinite(*double_value))
        return opt_parse_error(error_out, "must be finite");
    if (*double_value < 0.0)
        return opt_parse_error(error_out, "must be non-negative");
    return true;
}

// Validate that an already parsed double is a finite probability.
bool opt_validate_probability(const void *value_ptr, String *error_out) {
    const double *double_value = value_ptr;

    if (!isfinite(*double_value))
        return opt_parse_error(error_out, "must be finite");
    if (*double_value < 0.0 || *double_value > 1.0) {
        return opt_parse_error(error_out, "expected a number in the range 0-1");
    }
    return true;
}

// Parse a string value from raw text.
bool opt_parse_string_option(void *value_ptr, const char *text, size_t text_len,
                             String *error_out) {
    String *value = value_ptr;

    if (text == NULL)
        return opt_parse_error(error_out, "value is required");

    str_free(*value);
    *value = str_from_data(text, text_len);
    return true;
}

// Parse a string-list value by appending one raw textual value.
bool opt_parse_string_list_option(void *value_ptr, const char *text,
                                  size_t text_len, String *error_out) {
    StringArray *value = value_ptr;

    if (text == NULL)
        return opt_parse_error(error_out, "value is required");

    arr_push(*value, str_from_data(text, text_len));
    return true;
}

// Clear a string value.
void opt_clear_string_option(void *value_ptr) {
    String *value = value_ptr;

    str_free(*value);
}

// Clear a string-list value.
void opt_clear_string_list_option(void *value_ptr) {
    StringArray *value = value_ptr;
    str_array_free(value);
}

// Deep-copy a string value.
void opt_copy_string_option(void *dst_value, const void *src_value) {
    const String *src = src_value;
    String *dst = dst_value;

    str_free(*dst);
    *dst = copy_str(*src);
}

// Deep-copy a string-list value.
void opt_copy_string_list_option(void *dst_value, const void *src_value) {
    const StringArray *src = src_value;
    StringArray *dst = dst_value;

    str_array_free(dst);
    for (size_t i = 0; i < src->size; ++i)
        arr_push(*dst, copy_str(src->data[i]));
}

//------------------------------------------------------------------------------
// Program Lookup and Help Printers
//------------------------------------------------------------------------------

// Return the registered default command or fail if the parser config is
// inconsistent.
static const OptProgramCommand *
opt_require_default_program_command(const OptProgramParser *parser) {
    const OptProgramCommand *program_command = NULL;

    if (parser->attrs.default_command == NULL)
        return NULL;

    program_command =
        opt_find_program_command(parser, parser->attrs.default_command);
    require(program_command != NULL, "default command is not registered");
    return program_command;
}

// Find a command by its declared name.
static const OptProgramCommand *
opt_find_program_command(const OptProgramParser *parser, const char *name) {
    for (size_t i = 0; i < parser->command_count; ++i) {
        if (strcmp(parser->commands[i].command->name, name) == 0)
            return &parser->commands[i];
    }

    return NULL;
}

// Find a command by the stored parsed command id.
static const OptProgramCommand *
opt_find_program_command_by_id(const OptProgramParser *parser, int command_id) {
    for (size_t i = 0; i < parser->command_count; ++i) {
        if (parser->commands[i].command_id == command_id)
            return &parser->commands[i];
    }

    return NULL;
}

// Print top-level program help.
static void opt_print_program_help(FILE *out, const OptProgramParser *parser) {
    size_t width = opt_terminal_width();
    const OptProgramCommand *default_program_command = NULL;
    const OptCommandDesc *default_command = NULL;

    // IMGNEKO_UNCOVERED_OK
    if (parser->attrs.descr != NULL && parser->attrs.descr[0] != '\0')
        fprintf(out, "%s\n\n", parser->attrs.descr);

    if (parser->command_count == 0) {
        fprintf(out, "Usage: %s", parser->attrs.program_name);
        opt_print_schema_usage_suffix(out, parser->top_level_schema, false,
                                      "options");
        fputc('\n', out);

        // IMGNEKO_UNCOVERED_OK
        if (opt_print_schema_positionals_help(out, parser->top_level_schema,
                                              width))
            fputc('\n', out);

        fputs("Options:\n", out);
        opt_print_wrapped_kv(out, "-h, --help",
                             "Show this help message and exit.", width);
        opt_print_schema_options_help(out, parser->top_level_schema, width);
        fputc('\n', out);
        return;
    }

    default_program_command = opt_require_default_program_command(parser);

    fprintf(out, "Usage: %s", parser->attrs.program_name);
    if (parser->top_level_schema != NULL)
        fputs(" [options] <command> [command options]\n", out);
    else
        fputs(" <command> [options]\n", out);
    if (parser->attrs.default_command != NULL) {
        String options_label = str_empty;

        default_command = default_program_command->command;
        fprintf(out, "       %s", parser->attrs.program_name);
        str_append_cstr(options_label, default_command->name);
        str_append_cstr(options_label, " options");
        opt_print_schema_usage_suffix(out, default_command->schema,
                                      parser->top_level_schema != NULL,
                                      options_label.cstr);
        str_free(options_label);
    }
    fputc('\n', out);

    fputs("Commands:\n", out);
    for (size_t i = 0; i < parser->command_count; ++i) {
        String label = str_from_cstr(parser->commands[i].command->name);

        if (parser->attrs.default_command != NULL &&
            strcmp(parser->attrs.default_command,
                   parser->commands[i].command->name) == 0) {
            str_append_cstr(label, " (default)");
        }

        opt_print_wrapped_kv(out, label.cstr,
                             parser->commands[i].command->attrs.descr == NULL
                                 ? ""
                                 : parser->commands[i].command->attrs.descr,
                             width);
        str_free(label);
    }
    fputs("\nOptions:\n", out);
    if (parser->top_level_schema != NULL)
        opt_print_schema_options_help(out, parser->top_level_schema, width);
    opt_print_wrapped_kv(out, "-h, --help", "Show overall help and exit.",
                         width);
    fputc('\n', out);
}

// Print a command-specific usage line.
static void opt_print_command_usage(FILE *out, const OptProgramParser *parser,
                                    const OptCommandDesc *command) {
    fprintf(out, "Usage: %s %s", parser->attrs.program_name, command->name);
    opt_print_schema_usage_suffix(out, command->schema, false, "options");
}

// Print an alternate default-command usage line aligned under the main usage
// line.
static void opt_print_default_command_usage(FILE *out,
                                            const OptProgramParser *parser,
                                            const OptCommandDesc *command) {
    fprintf(out, "       %s", parser->attrs.program_name);
    opt_print_schema_usage_suffix(out, command->schema,
                                  parser->top_level_schema != NULL, "options");
}

// Print command-specific help, including positionals and options.
static void opt_print_command_help(FILE *out, const OptProgramParser *parser,
                                   const OptCommandDesc *command) {
    size_t width = opt_terminal_width();

    // IMGNEKO_UNCOVERED_OK
    if (command->attrs.descr != NULL && command->attrs.descr[0] != '\0')
        fprintf(out, "%s\n\n", command->attrs.descr);

    opt_print_command_usage(out, parser, command);
    if (parser->attrs.default_command != NULL &&
        strcmp(parser->attrs.default_command, command->name) == 0) {
        opt_print_default_command_usage(out, parser, command);
    }
    fputc('\n', out);

    if (opt_print_schema_positionals_help(out, command->schema, width))
        fputc('\n', out);

    fputs("Options:\n", out);
    if (parser->top_level_schema != NULL)
        opt_print_schema_options_help(out, parser->top_level_schema, width);
    opt_print_schema_options_help(out, command->schema, width);
    opt_print_wrapped_kv(out, "-h, --help", "Show this help message and exit.",
                         width);
    fputc('\n', out);
}

//------------------------------------------------------------------------------
// Diagnostics and Assignment Helpers
//------------------------------------------------------------------------------

// Print an unknown-option diagnostic.
static OptParseStatus opt_fail_unknown_option(const char *arg) {
    fprintf(stderr, "error: unknown option: %s\n", arg);
    return OPT_PARSE_STATUS_ERROR;
}

// Print an unknown-command diagnostic.
static OptParseStatus opt_fail_unknown_command(const char *arg) {
    fprintf(stderr, "error: unknown command: %s\n", arg);
    return OPT_PARSE_STATUS_ERROR;
}

// Print an unexpected-positional diagnostic.
static OptParseStatus opt_fail_unexpected_positional(const char *arg) {
    fprintf(stderr, "error: unexpected positional argument: %s\n", arg);
    return OPT_PARSE_STATUS_ERROR;
}

// Print a diagnostic for a positional argument that is only legal after `--`.
static OptParseStatus opt_fail_missing_double_dash(const char *arg) {
    fprintf(stderr,
            "error: positional argument requires the -- delimiter here: %s\n",
            arg);
    return OPT_PARSE_STATUS_ERROR;
}

// Print a "requires a value" diagnostic for a field.
static OptParseStatus opt_fail_missing_value(const OptFieldSpec *field) {
    fputs("error: ", stderr);
    opt_fprint_field_display_name(stderr, field);
    fputs(" requires a value\n", stderr);
    return OPT_PARSE_STATUS_ERROR;
}

// Print a duplicate-option diagnostic for a field that already has a CLI
// value.
static OptParseStatus opt_fail_duplicate_option(const OptFieldSpec *field) {
    fputs("error: option specified multiple times: ", stderr);
    opt_fprint_field_display_name(stderr, field);
    fputc('\n', stderr);
    return OPT_PARSE_STATUS_ERROR;
}

// Print an invalid-value diagnostic, optionally including a parser-supplied
// reason.
static OptParseStatus opt_fail_invalid_value(const char *field_name,
                                             const char *value_text,
                                             const String *error_text) {
    fprintf(stderr, "error: invalid value for %s: %s", field_name, value_text);
    if (error_text->len != 0)
        fprintf(stderr, " (%s)", error_text->cstr);
    fputc('\n', stderr);
    return OPT_PARSE_STATUS_ERROR;
}

// Assign a field and emit a consistent invalid-value diagnostic on failure.
static OptParseStatus opt_assign_field_or_fail(const OptFieldSpec *field,
                                               void *options,
                                               const char *value_text,
                                               size_t value_text_len,
                                               const char *diagnostic_value) {
    String parse_error = str_empty;
    String field_name = str_empty;

    // Repeated CLI writes to the same scalar option are normally an error.
    // The only exception is boolean options when the repeated spelling resolves
    // to the same stored value, for example `--flag --flag` or
    // `--no-flag --no-flag`; those are treated as harmless idempotent repeats.
    // To decide that, we reparse the incoming text as a bool. If that parse
    // fails, we still report a duplicate for simplicity.
    if (!field->attrs.positional && !opt_field_is_multi_valued(field) &&
        opt_field_is_set(field, options) &&
        opt_field_provenance(field, options) == OPT_PROVENANCE_CLI) {
        if (field->attrs.bool_mode != OPT_BOOL_MODE_NONE) {
            bool parsed_value = false;
            if (field->attrs.parse(&parsed_value, value_text, value_text_len,
                                   NULL)) {
                if ((field->attrs.validate == NULL ||
                     field->attrs.validate(&parsed_value, NULL)) &&
                    *(const bool *)opt_const_field_value_ptr(field, options) ==
                        parsed_value) {
                    return OPT_PARSE_STATUS_OK;
                }
            }
        }
        return opt_fail_duplicate_option(field);
    }

    if (opt_assign_field_value(field, options, value_text, value_text_len,
                               OPT_PROVENANCE_CLI, &parse_error)) {
        str_free(parse_error);
        return OPT_PARSE_STATUS_OK;
    }

    opt_append_field_display_name(&field_name, field);
    OptParseStatus status =
        opt_fail_invalid_value(field_name.cstr, diagnostic_value, &parse_error);
    str_free(field_name);
    str_free(parse_error);
    return status;
}

// Assign a CLI option occurrence after its alias has already been resolved.
static OptParseStatus opt_assign_cli_option(const OptFieldSpec *field,
                                            void *options,
                                            const char *value_text,
                                            bool is_negated,
                                            const char *diagnostic_value) {
    if (!opt_field_takes_value(field)) {
        return opt_assign_field_or_fail(field, options, is_negated ? "0" : "1",
                                        1, diagnostic_value);
    }

    return opt_assign_field_or_fail(field, options, value_text,
                                    strlen(value_text), diagnostic_value);
}

//------------------------------------------------------------------------------
// CLI Parsing
//------------------------------------------------------------------------------

// Return true when one raw argv token requests help.
static bool opt_is_help_argument(const char *arg) {
    return strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0;
}

// Return true when one raw argv token is a `--long` option.
static bool opt_is_long_option_argument(const char *arg) {
    // IMGNEKO_UNCOVERED_OK
    return arg[0] == '-' && arg[1] == '-' && arg[2] != '\0';
}

// Return true when one raw argv token starts with a short option like `-v` or
// `-n7`.
static bool opt_is_short_option_argument(const char *arg) {
    // IMGNEKO_UNCOVERED_OK
    return arg[0] == '-' && arg[1] != '\0' && arg[1] != '-';
}

// Parse a `--long` or `--long=value` token and assign it when recognized.
// Unknown options report `OPT_PARSE_STATUS_NO_MATCH` so callers can keep
// probing other active schemas.
static OptParseStatus opt_parse_long_option_text(const OptSchema *schema,
                                                 const char *arg, int argc,
                                                 char **argv, int *arg_index,
                                                 void *options) {
    const char *equals = strchr(arg + 2, '=');
    const char *option_name = arg + 2;
    size_t option_name_len =
        equals == NULL ? strlen(option_name) : (size_t)(equals - option_name);
    OptOptionQuery query = opt_long_option_query(option_name, option_name_len);
    bool is_negated = false;
    const OptFieldSpec *field = opt_find_option(schema, &query, &is_negated);
    const char *value_text = NULL;

    if (field == NULL)
        return OPT_PARSE_STATUS_NO_MATCH;

    if (!opt_field_takes_value(field)) {
        if (equals != NULL) {
            fprintf(stderr, "error: option does not take a value: %s\n", arg);
            return OPT_PARSE_STATUS_ERROR;
        }
        return opt_assign_cli_option(field, options, NULL, is_negated, arg);
    }

    if (equals != NULL)
        value_text = equals + 1;
    else if (*arg_index + 1 < argc)
        value_text = argv[++(*arg_index)];
    else
        return opt_fail_missing_value(field);

    return opt_assign_cli_option(field, options, value_text, is_negated,
                                 value_text);
}

// Parse a short option token. Compact attached values like `-n7` are
// accepted, but clustered short options like `-vn7` are rejected.
static OptParseStatus opt_parse_short_option_text(const OptSchema *schema,
                                                  const char *arg, int argc,
                                                  char **argv, int *arg_index,
                                                  void *options) {
    OptOptionQuery query = opt_short_option_query(arg[1]);
    bool is_negated = false;
    const OptFieldSpec *field = opt_find_option(schema, &query, &is_negated);
    char option_text[3] = {'-', arg[1], '\0'};
    const char *value_text = NULL;

    if (field == NULL)
        return OPT_PARSE_STATUS_NO_MATCH;

    if (!opt_field_takes_value(field)) {
        // Reject any extra suffix for flag-like short options so `-abc` is not
        // interpreted as a cluster. Value-taking short options still accept
        // compact attached values like `-n7`.
        if (arg[2] != '\0')
            return opt_fail_unknown_option(arg);
        return opt_assign_cli_option(field, options, NULL, is_negated,
                                     option_text);
    }

    if (arg[2] != '\0')
        value_text = arg + 2;
    else if (*arg_index + 1 < argc)
        value_text = argv[++(*arg_index)];
    else
        return opt_fail_missing_value(field);

    return opt_assign_cli_option(field, options, value_text, is_negated,
                                 value_text);
}

// Try to parse one option token against the currently active schemas in order.
// After a command is selected, command-local aliases win and top-level aliases
// are only consulted as a fallback for later global options.
static OptParseStatus
opt_try_parse_active_option(OptRunCtx *ctx, const char *arg,
                            OptOptionParseFn parse_option_fn) {
    const OptSchema *primary_schema = opt_run_ctx_primary_schema(ctx);
    void *primary_options = opt_run_ctx_primary_options(ctx);
    OptParseStatus status = OPT_PARSE_STATUS_OK;

    if (primary_schema != NULL) {
        status = parse_option_fn(primary_schema, arg, ctx->argc, ctx->argv,
                                 &ctx->arg_index, primary_options);
        if (status != OPT_PARSE_STATUS_NO_MATCH)
            return status;
    }

    if (!opt_run_ctx_has_top_level_option_fallback(ctx))
        return OPT_PARSE_STATUS_NO_MATCH;

    status =
        parse_option_fn(ctx->parser->top_level_schema, arg, ctx->argc,
                        ctx->argv, &ctx->arg_index, ctx->top_level_options);
    return status;
}

// Try to assign one positional argument to the current primary schema. When no
// positional field is available, report a miss without consuming the token.
static OptParseStatus opt_try_assign_active_positional(OptRunCtx *ctx,
                                                       const char *arg) {
    const OptSchema *schema = opt_run_ctx_primary_schema(ctx);
    void *options = opt_run_ctx_primary_options(ctx);
    size_t *positional_index = opt_run_ctx_primary_positional_index(ctx);
    size_t field_index = 0;
    const OptFieldSpec *field = NULL;
    OptParseStatus status = OPT_PARSE_STATUS_OK;

    if (schema == NULL || positional_index == NULL) // IMGNEKO_UNCOVERED_OK
        return OPT_PARSE_STATUS_NO_MATCH;

    // Probe with a copy first so a failed parse does not advance the real
    // positional cursor. Only after successful assignment do we commit the
    // cursor for single-valued positionals.
    field_index = *positional_index;
    field = opt_next_assignable_positional_field(schema, &field_index,
                                                 ctx->stop_options);
    if (field == NULL)
        return OPT_PARSE_STATUS_NO_MATCH;

    status = opt_assign_field_or_fail(field, options, arg, strlen(arg), arg);
    if (status != OPT_PARSE_STATUS_OK)
        return status;

    if (!opt_field_is_multi_valued(field))
        *positional_index = field_index + 1;

    return OPT_PARSE_STATUS_OK;
}

// Activate the command whose name matches the current raw token.
static bool opt_try_activate_matching_command(OptRunCtx *ctx, const char *arg) {
    const OptProgramCommand *program_command = NULL;

    // IMGNEKO_UNCOVERED_OK[2 lines]
    if (!opt_run_ctx_awaiting_command(ctx))
        return false;

    program_command = opt_find_program_command(ctx->parser, arg);
    if (program_command == NULL)
        return false;

    opt_run_ctx_activate_command(ctx, program_command);
    ctx->selected_command_explicitly = true;
    return true;
}

// Activate the configured default command when one exists.
static bool opt_try_activate_default_command(OptRunCtx *ctx) {
    if (!opt_run_ctx_awaiting_command(ctx) || // IMGNEKO_UNCOVERED_OK
        ctx->default_command == NULL) {
        return false;
    }

    opt_run_ctx_activate_command(ctx, ctx->default_command);
    ctx->selected_command_explicitly = false;
    return true;
}

// Print the help text selected by the deferred-help rules.
static void opt_print_requested_help(const OptRunCtx *ctx) {
    // IMGNEKO_UNCOVERED_OK[2 lines]
    if (ctx->selected_command_explicitly &&
        opt_run_ctx_has_active_command(ctx)) {
        opt_print_command_help(stdout, ctx->parser,
                               ctx->selected_command->command);
        return;
    }

    opt_print_program_help(stdout, ctx->parser);
}

// Print any deferred help text and translate the parse result to HELP.
static OptParseStatus opt_finish_requested_help(const OptRunCtx *ctx) {
    opt_print_requested_help(ctx);
    return OPT_PARSE_STATUS_HELP;
}

// Look ahead through the remaining raw argv entries for a later `-h`/`--help`.
// This is only used on the error path so that "help plus parse error" still
// prints help even when the parser fails before reaching the help token.
static void opt_note_later_help_request(OptRunCtx *ctx) {
    const OptProgramCommand *later_explicit_command = NULL;
    bool stop_options = ctx->stop_options;

    if (ctx->help_requested)
        return;

    for (int i = ctx->arg_index + 1; i < ctx->argc; ++i) {
        const char *arg = ctx->argv[i];

        if (!stop_options && opt_is_help_argument(arg)) {
            ctx->help_requested = true;
            // IMGNEKO_UNCOVERED_OK[2 lines]
            if (later_explicit_command != NULL &&
                ctx->selected_command == NULL) {
                ctx->selected_command = later_explicit_command;
                ctx->selected_command_explicitly = true;
            }
            return;
        }

        if (!stop_options && strcmp(arg, "--") == 0) {
            stop_options = true;
            continue;
        }

        // IMGNEKO_UNCOVERED_OK[2 lines]
        if (!stop_options && ctx->selected_command == NULL &&
            later_explicit_command == NULL) {
            later_explicit_command = opt_find_program_command(ctx->parser, arg);
        }
    }
}

// Handle a raw `--` token.
static OptParseStatus opt_handle_double_dash(OptRunCtx *ctx) {
    ctx->stop_options = true;
    return OPT_PARSE_STATUS_OK;
}

// Process a raw argv token. If parsing misses before command selection, we
// may activate the default command and ask the caller to retry that same token
// without advancing the raw argv cursor.
static OptParseStatus opt_process_arg_item(OptRunCtx *ctx) {
    const char *arg = ctx->argv[ctx->arg_index];
    OptOptionParseFn parse_option_fn = NULL;
    OptParseStatus status = OPT_PARSE_STATUS_OK;

    if (!ctx->stop_options && opt_is_help_argument(arg)) {
        ctx->help_requested = true;
        ++ctx->arg_index;
        return OPT_PARSE_STATUS_OK;
    }

    if (!ctx->stop_options && strcmp(arg, "--") == 0) {
        status = opt_handle_double_dash(ctx);
        ++ctx->arg_index;
        return status;
    }

    if (!ctx->stop_options && opt_is_long_option_argument(arg))
        parse_option_fn = opt_parse_long_option_text;
    else if (!ctx->stop_options && opt_is_short_option_argument(arg))
        parse_option_fn = opt_parse_short_option_text;

    // Try to parse the argument as an option or positional argument.
    status = parse_option_fn != NULL
                 ? opt_try_parse_active_option(ctx, arg, parse_option_fn)
                 : opt_try_assign_active_positional(ctx, arg);
    if (status == OPT_PARSE_STATUS_OK) {
        ++ctx->arg_index;
        return OPT_PARSE_STATUS_OK;
    }
    if (status != OPT_PARSE_STATUS_NO_MATCH)
        return status;

    // If we are not awaiting a command name, report a parse error.
    if (!opt_run_ctx_awaiting_command(ctx)) {
        if (parse_option_fn != NULL)
            return opt_fail_unknown_option(arg);
        if (opt_run_ctx_next_positional_requires_double_dash(ctx))
            return opt_fail_missing_double_dash(arg);
        return opt_fail_unexpected_positional(arg);
    }

    // Otherwise try to interpret this argement as a command name.
    if (opt_try_activate_matching_command(ctx, arg)) {
        ++ctx->arg_index;
        return OPT_PARSE_STATUS_OK;
    }

    // If the argument is not a command name, try to activate the default
    // command and retry parsing this token by returning OPT_PARSE_STATUS_RETRY.
    if (!opt_try_activate_default_command(ctx)) {
        if (parse_option_fn != NULL)
            return opt_fail_unknown_option(arg);
        return opt_fail_unknown_command(arg);
    }

    require(!opt_run_ctx_awaiting_command(ctx),
            "retry requested without changing parser mode");
    return OPT_PARSE_STATUS_RETRY;
}

// Finalize a completed parse after all argv tokens have been processed.
static OptParseStatus opt_finalize_run_ctx(OptRunCtx *ctx) {
    if (ctx->parser->command_count == 0)
        return OPT_PARSE_STATUS_OK;
    if (opt_run_ctx_has_active_command(ctx))
        return OPT_PARSE_STATUS_OK;
    // IMGNEKO_UNCOVERED_OK
    if (ctx->help_requested && !ctx->selected_command_explicitly)
        return OPT_PARSE_STATUS_OK;

    // Reaching end-of-argv still activates the default command, even when the
    // parser already consumed some top-level values first. Only programs
    // without a default command can succeed in a command-less state.
    if (opt_try_activate_default_command(ctx))
        return OPT_PARSE_STATUS_OK;

    if (ctx->parser->top_level_schema != NULL &&
        opt_schema_has_any_value(ctx->parser->top_level_schema,
                                 ctx->top_level_options)) {
        return OPT_PARSE_STATUS_OK;
    }

    // Programs without a default command may finish without selecting one. The
    // caller should handle the `OPT_CMD_NONE` case to report an error.
    return OPT_PARSE_STATUS_OK;
}

// Parse argv according to parser, print help or diagnostics as needed, and
// return a process exit code: 0 on success/help, 2 on CLI errors.
// `result` must point to a `Parsed<Name>` object (`Parsed<Name>` type is
// produced by one of the `OPT_DEFINE_PROGRAM_PARSER*` macros).
int opt_run_program_parser(const OptProgramParser *parser, int argc,
                           char **argv, void *result) {
    OptRunCtx ctx;
    OptParseStatus status = OPT_PARSE_STATUS_OK;

    opt_run_ctx_init(&ctx, parser, argc, argv, result);
    ctx.arg_index = 1;

    while (ctx.arg_index < argc) {
        status = opt_process_arg_item(&ctx);
        if (status == OPT_PARSE_STATUS_RETRY)
            continue; // Retry the same token on default command activation.
        if (status != OPT_PARSE_STATUS_OK) {
            opt_note_later_help_request(&ctx);
            if (ctx.help_requested)
                status = opt_finish_requested_help(&ctx);
            return opt_finish_program_parse(parser, result, status);
        }
    }

    status = opt_finalize_run_ctx(&ctx);
    if (ctx.help_requested)
        status = opt_finish_requested_help(&ctx);
    return opt_finish_program_parse(parser, result, status);
}

// Free the selected parsed command payload and reset the result to zero.
void opt_program_result_deinit(const OptProgramParser *parser, void *result) {
    int command_id = *(int *)((char *)result + parser->command_id_offset);
    const OptProgramCommand *program_command =
        opt_find_program_command_by_id(parser, command_id);

    if (parser->top_level_schema != NULL) {
        opt_schema_deinit(parser->top_level_schema,
                          (char *)result + parser->top_level_offset);
    }

    if (program_command != NULL) {
        opt_schema_deinit(program_command->command->schema,
                          (char *)result + parser->command_offset);
    }

    memset(result, 0, parser->result_size);
}

//------------------------------------------------------------------------------
// Generic schema-based option-struct operations
//------------------------------------------------------------------------------

// Initialize an option struct according to schema defaults.
void opt_schema_init(const OptSchema *schema, void *options) {
    memset(options, 0, schema->size);

    for (size_t i = 0; i < schema->field_count; ++i)
        opt_apply_default(&schema->fields[i], options);
}

// Free any owned storage inside an option struct.
void opt_schema_deinit(const OptSchema *schema, void *options) {
    for (size_t i = 0; i < schema->field_count; ++i)
        opt_clear_field(&schema->fields[i], options);
}

// Find a field with the given name inside schema.
const OptFieldSpec *opt_find_field_by_name(const OptSchema *schema,
                                           const char *field_name) {
    for (size_t i = 0; i < schema->field_count; ++i) {
        if (strcmp(schema->fields[i].name, field_name) == 0)
            return &schema->fields[i];
    }

    return NULL;
}

// Return true when any field of an option struct currently stores a value.
static bool opt_schema_has_any_value(const OptSchema *schema,
                                     const void *options) {
    for (size_t i = 0; i < schema->field_count; ++i) {
        if (opt_field_is_set(&schema->fields[i], options))
            return true;
    }

    return false;
}

// Copy matching set fields from src into dst. Returns false on type mismatch.
bool opt_merge_matching(const OptSchema *dst_schema, void *dst,
                        const OptSchema *src_schema, const void *src) {
    for (size_t i = 0; i < src_schema->field_count; ++i) {
        const OptFieldSpec *src_field = &src_schema->fields[i];
        const OptFieldSpec *dst_field =
            opt_find_field_by_name(dst_schema, src_field->name);

        if (dst_field == NULL)
            continue;
        if (strcmp(dst_field->type_name, src_field->type_name) != 0)
            return false;
        if (!opt_field_is_set(src_field, src))
            continue;

        opt_copy_field_value(dst_field, dst, src_field, src);
    }

    return true;
}

// Record whether a field currently stores a value and where it came from.
void opt_set_field_state(const OptFieldSpec *field, void *options, bool is_set,
                         OptProvenance provenance) {
    *opt_field_is_set_ptr(field, options) = is_set;
    *opt_field_provenance_ptr(field, options) = (uint8_t)provenance;
}

// Reset a field to an empty state, freeing any owned storage first.
void opt_clear_field(const OptFieldSpec *field, void *options) {
    void *value_ptr = opt_field_value_ptr(field, options);

    if (field->attrs.clear != NULL)
        field->attrs.clear(value_ptr);
    else
        memset(value_ptr, 0, field->value_size);

    opt_set_field_state(field, options, false, OPT_PROVENANCE_NONE);
}

// Apply a schema default value after zero-initialization.
void opt_apply_default(const OptFieldSpec *field, void *options) {
    if (field->attrs.dflt == NULL)
        return;

    require(opt_assign_field_value(field, options, field->attrs.dflt,
                                   strlen(field->attrs.dflt),
                                   OPT_PROVENANCE_DEFAULT, NULL),
            "option field has an invalid default value");
}

// Copy the current field value into temporary wrapper storage so parsing and
// validation can run without mutating the committed destination value.
static void opt_copy_field_value_for_validation(const OptFieldSpec *field,
                                                void *temp_wrapper,
                                                const void *options) {
    void *temp_value_ptr = (char *)temp_wrapper + field->value_offset;

    memset(temp_wrapper, 0, field->size);

    if (!opt_field_is_set(field, options))
        return;

    require(field->attrs.copy != NULL || field->attrs.clear == NULL,
            "validated owned option fields require a copy callback");
    if (field->attrs.copy != NULL) {
        field->attrs.copy(temp_value_ptr,
                          opt_const_field_value_ptr(field, options));
    } else {
        memcpy(temp_value_ptr, opt_const_field_value_ptr(field, options),
               field->value_size);
    }
}

// Release any owned storage held by a temporary parsed value.
static void opt_clear_tentative_field_value(const OptFieldSpec *field,
                                            void *temp_wrapper) {
    if (field->attrs.clear == NULL)
        return;

    field->attrs.clear((char *)temp_wrapper + field->value_offset);
}

// Assign a parsed textual value to the destination field.
bool opt_assign_field_value(const OptFieldSpec *field, void *options,
                            const char *text, size_t text_len,
                            OptProvenance provenance, String *error_out) {
    char temp_wrapper[field->size];
    void *temp_value_ptr = (char *)temp_wrapper + field->value_offset;
    bool success = false;

    if (field->attrs.parse == NULL)
        return false;

    if (error_out != NULL)
        str_free(*error_out);

    if (field->attrs.validate == NULL) {
        if (!field->attrs.parse(opt_field_value_ptr(field, options), text,
                                text_len, error_out)) {
            return false;
        }

        opt_set_field_state(field, options, true, provenance);
        return true;
    }

    opt_copy_field_value_for_validation(field, temp_wrapper, options);

    if (!field->attrs.parse(temp_value_ptr, text, text_len, error_out))
        goto cleanup;
    if (!field->attrs.validate(temp_value_ptr, error_out))
        goto cleanup;

    opt_clear_field(field, options);
    memcpy(opt_field_ptr(field, options), temp_wrapper, field->size);
    opt_set_field_state(field, options, true, provenance);
    success = true;

cleanup:
    if (!success)
        opt_clear_tentative_field_value(field, temp_wrapper);
    return success;
}

// Deep-copy a matching field value from src to dst.
void opt_copy_field_value(const OptFieldSpec *dst_field, void *dst_options,
                          const OptFieldSpec *src_field,
                          const void *src_options) {
    void *dst_value_ptr = opt_field_value_ptr(dst_field, dst_options);
    const void *src_value_ptr =
        opt_const_field_value_ptr(src_field, src_options);

    opt_clear_field(dst_field, dst_options);

    if (dst_field->attrs.copy != NULL)
        dst_field->attrs.copy(dst_value_ptr, src_value_ptr);
    else
        memcpy(dst_value_ptr, src_value_ptr, dst_field->value_size);

    opt_set_field_state(
        dst_field, dst_options,
        *opt_const_field_is_set_ptr(src_field, src_options),
        (OptProvenance)*opt_const_field_provenance_ptr(src_field, src_options));
}
