// SPDX-License-Identifier: MIT-0

// Keep Darwin extension declarations, such as mkdtemp(), visible when strict
// POSIX feature-test macros are enabled.
#define _DARWIN_C_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <regex.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "util/array.h"
#include "util/error.h"
#include "util/file.h"
#include "util/klib/khash.h"
#include "util/path.h"
#include "util/string.h"

typedef enum DirectiveKind {
    DIRECTIVE_CHECK,
    DIRECTIVE_CHECK_SAME,
    DIRECTIVE_CHECK_NEXT,
    DIRECTIVE_CHECK_NOT,
} DirectiveKind;

typedef enum PatternSegmentKind {
    SEGMENT_LITERAL,
    SEGMENT_REGEX,
    SEGMENT_VARIABLE_DEF,
    SEGMENT_VARIABLE_USE,
} PatternSegmentKind;

// One parsed pattern fragment. `name` is used for variable defs/uses, and
// `text` holds either literal bytes or the regex body.
typedef struct PatternSegment {
    PatternSegmentKind kind;
    String name;
    String text;
} PatternSegment;

// One captured output line without its trailing newline.
typedef struct OutputLine {
    String text;
} OutputLine;

// One source location in the captured output. `column` is a byte offset into
// the line text.
typedef struct OutputPosition {
    size_t line_index;
    size_t column;
} OutputPosition;

// One successful regex match on a specific output line.
typedef struct LineMatch {
    size_t line_index;
    size_t start_column;
    size_t end_column;
} LineMatch;

// One variable binding emitted by a compiled pattern. `group_index` refers to
// the regex match array entry that contains the variable's text.
typedef struct CaptureBinding {
    const char *name;
    size_t group_index;
} CaptureBinding;

DEFINE_ARRAY_TYPE(PatternSegmentArray, PatternSegment)
DEFINE_ARRAY_TYPE(CaptureBindingArray, CaptureBinding)

// One check directive parsed from the source file.
typedef struct CheckDirective {
    DirectiveKind kind;
    int line_number;
    String raw_pattern;
    PatternSegmentArray segments;
} CheckDirective;

// One compiled regex plus metadata needed to bind named captures back into the
// variable table.
typedef struct CompiledPattern {
    regex_t regex;
    String regex_text;
    CaptureBindingArray bindings;
    size_t capture_group_count;
    bool regex_ready;
} CompiledPattern;

DEFINE_ARRAY_TYPE(CheckDirectiveArray, CheckDirective)
DEFINE_ARRAY_TYPE(CheckDirectivePtrArray, CheckDirective *)
DEFINE_ARRAY_TYPE(OutputLineArray, OutputLine)

// Map variable names to owned String values.
KHASH_MAP_INIT_STR(VariableMap, String)

// Parsed contents of one run-and-check test file.
typedef struct ParsedTest {
    String run_command;
    CheckDirectiveArray directives;
} ParsedTest;

// Mutable variable table used while matching directives.
typedef struct VariableContext {
    khash_t(VariableMap) * map;
} VariableContext;

// Per-test environment variable that points tests at their personal output
// directory.
static const char *const test_output_dir_env = "IMGNEKO_TEST_OUTPUT_DIR";

// Fixed file names for the RUN command's redirected streams.
static const char *const stdout_file_name = "run-stdout";
static const char *const stderr_file_name = "run-stderr";

static void die_usage(const char *argv0) {
    fprintf(stderr, "usage: %s TEST_FILE\n", argv0);
    exit(2);
}

// Return whether `name` is a valid named variable identifier.
static bool is_valid_variable_name(const char *name) {
    size_t i;

    if (name[0] == '\0')
        return false;
    if (!(str_char_is_ascii_alpha(name[0]) || name[0] == '_'))
        return false;

    for (i = 1; name[i] != '\0'; ++i) {
        if (!(str_char_is_ascii_alnum(name[i]) || name[i] == '_')) {
            return false;
        }
    }

    return true;
}

// Escape literal text so it matches itself under POSIX extended regex rules.
static void append_regex_escaped_literal(String *out, const char *text,
                                         size_t len) {
    static const char *const metacharacters = ".^$[()\\*+?{|";

    for (size_t i = 0; i < len; ++i) {
        if (strchr(metacharacters, text[i]) != NULL)
            str_push(*out, '\\');
        str_push(*out, text[i]);
    }
}

// Count capturing groups in a POSIX ERE fragment so later named captures can
// be mapped onto the correct `regmatch_t` slots.
static size_t regex_capture_group_count(const char *regex_text) {
    size_t groups = 0;
    bool escaped = false;
    bool in_class = false;

    for (size_t i = 0; regex_text[i] != '\0'; ++i) {
        char ch = regex_text[i];

        if (escaped) {
            escaped = false;
            continue;
        }

        if (ch == '\\') {
            escaped = true;
            continue;
        }

        if (in_class) {
            if (ch == ']')
                in_class = false;
            continue;
        }

        if (ch == '[') {
            in_class = true;
            continue;
        }

        if (ch == '(')
            groups++;
    }

    return groups;
}

// Return the user-facing name for the directive kind.
static const char *directive_kind_name(DirectiveKind kind) {
    switch (kind) { // IMGNEKO_UNCOVERED_OK
    case DIRECTIVE_CHECK:
        return "CHECK";
    case DIRECTIVE_CHECK_SAME:
        return "CHECK-SAME";
    case DIRECTIVE_CHECK_NEXT:
        return "CHECK-NEXT";
    case DIRECTIVE_CHECK_NOT:
        return "CHECK-NOT";
    }

    abort(); // IMGNEKO_UNCOVERED_OK
}

static void pattern_segment_array_free(PatternSegmentArray *segments) {
    for (size_t i = 0; i < segments->size; ++i) {
        str_free(segments->data[i].name);
        str_free(segments->data[i].text);
    }
    arr_free(*segments);
}

static void check_directive_array_free(CheckDirectiveArray *directives) {
    for (size_t i = 0; i < directives->size; ++i) {
        str_free(directives->data[i].raw_pattern);
        pattern_segment_array_free(&directives->data[i].segments);
    }
    arr_free(*directives);
}

static void output_line_array_free(OutputLineArray *lines) {
    for (size_t i = 0; i < lines->size; ++i)
        str_free(lines->data[i].text);
    arr_free(*lines);
}

static void capture_binding_array_free(CaptureBindingArray *bindings) {
    arr_free(*bindings);
}

static void compiled_pattern_free(CompiledPattern *pattern) {
    if (pattern->regex_ready)
        regfree(&pattern->regex);
    str_free(pattern->regex_text);
    capture_binding_array_free(&pattern->bindings);
    pattern->capture_group_count = 0;
    pattern->regex_ready = false;
}

static void parsed_test_free(ParsedTest *parsed) {
    str_free(parsed->run_command);
    check_directive_array_free(&parsed->directives);
}

static void variable_context_init(VariableContext *ctx) {
    ctx->map = kh_init(VariableMap);
    require(ctx->map != NULL, "failed to allocate variable map: %errno");
}

static void variable_context_free(VariableContext *ctx) {
    for (khiter_t it = kh_begin(ctx->map); it != kh_end(ctx->map); ++it) {
        if (!kh_exist(ctx->map, it))
            continue;
        str_free(kh_value(ctx->map, it));
        free((char *)kh_key(ctx->map, it));
    }
    kh_destroy(VariableMap, ctx->map);
}

// Look up a named variable. The returned pointer stays valid until the
// variable context is mutated or destroyed.
static const char *variable_context_get(const VariableContext *ctx,
                                        const char *name) {
    khiter_t it = kh_get(VariableMap, ctx->map, name);

    if (it == kh_end(ctx->map))
        return NULL;

    return kh_value(ctx->map, it).cstr;
}

// Replace the stored value for `name` with a newly allocated copy.
static void variable_context_set(VariableContext *ctx, const char *name,
                                 const char *value, size_t value_len) {
    int absent = 0;
    khiter_t it = kh_put(VariableMap, ctx->map, name, &absent);

    require(it != kh_end(ctx->map),
            "failed to insert into variable map: %errno");

    if (absent) {
        String key = str_from_cstr(name);
        kh_key(ctx->map, it) = key.cstr;
        kh_value(ctx->map, it) = str_from_data(value, value_len);
        return;
    }

    str_free(kh_value(ctx->map, it));
    kh_value(ctx->map, it) = str_from_data(value, value_len);
}

// Write a descriptive parse error.
static void parse_error(const char *path, int line_number,
                        const char *message) {
    fprintf(stderr, "%s:%d: error: %s\n", path, line_number, message);
}

// Write an invalid-variable-name parse error with the name quoted so the empty
// name case is still explicit in diagnostics.
static void parse_invalid_variable_name_error(const char *path, int line_number,
                                              const char *name) {
    fprintf(stderr, "%s:%d: error: invalid variable name: '%s'\n", path,
            line_number, name);
}

static void check_directive_free(CheckDirective *directive) {
    str_free(directive->raw_pattern);
    pattern_segment_array_free(&directive->segments);
}

// Append a parsed pattern segment to the directive currently being built.
static void append_pattern_segment(PatternSegmentArray *segments,
                                   PatternSegmentKind kind, const char *name,
                                   const char *text, size_t text_len) {
    PatternSegment segment = {
        .kind = kind,
        .name = str_empty,
        .text = str_empty,
    };

    if (name != NULL)
        segment.name = str_from_cstr(name);
    if (text != NULL)
        segment.text = str_from_data(text, text_len);

    arr_push(*segments, segment);
}

// Parse a `CHECK` pattern into a sequence of literal, regex, and variable
// fragments.
static bool parse_pattern_segments(const char *path, int line_number,
                                   const char *pattern_text,
                                   PatternSegmentArray *segments) {
    size_t cursor = 0;

    while (pattern_text[cursor] != '\0') {
        size_t next_special = cursor;

        while (pattern_text[next_special] != '\0' &&
               strncmp(pattern_text + next_special, "{{", 2) != 0 &&
               strncmp(pattern_text + next_special, "[[", 2) != 0) {
            next_special++;
        }

        if (next_special > cursor) {
            append_pattern_segment(segments, SEGMENT_LITERAL, NULL,
                                   pattern_text + cursor,
                                   next_special - cursor);
            cursor = next_special;
            continue;
        }

        if (strncmp(pattern_text + cursor, "{{", 2) == 0) {
            const char *end = strstr(pattern_text + cursor + 2, "}}");

            if (end == NULL) {
                parse_error(path, line_number,
                            "unterminated {{...}} regex fragment");
                return false;
            }

            append_pattern_segment(segments, SEGMENT_REGEX, NULL,
                                   pattern_text + cursor + 2,
                                   (size_t)(end - (pattern_text + cursor + 2)));
            cursor = (size_t)(end - pattern_text) + 2;
            continue;
        }

        // IMGNEKO_UNCOVERED_OK: It's always `[[` at this point
        if (strncmp(pattern_text + cursor, "[[", 2) == 0) {
            const char *end = strstr(pattern_text + cursor + 2, "]]");
            const char *body;
            size_t body_len;
            const char *colon;

            if (end == NULL) {
                parse_error(path, line_number,
                            "unterminated [[...]] variable fragment");
                return false;
            }

            body = pattern_text + cursor + 2;
            body_len = (size_t)(end - body);
            colon = memchr(body, ':', body_len);
            if (colon == NULL) {
                String name = str_from_data(body, body_len);
                bool valid = is_valid_variable_name(name.cstr);

                if (!valid) {
                    parse_invalid_variable_name_error(path, line_number,
                                                      name.cstr);
                    str_free(name);
                    return false;
                }

                append_pattern_segment(segments, SEGMENT_VARIABLE_USE,
                                       name.cstr, NULL, 0);
                str_free(name);
                cursor = (size_t)(end - pattern_text) + 2;
                continue;
            }

            String name = str_from_data(body, (size_t)(colon - body));
            bool valid = is_valid_variable_name(name.cstr);

            if (!valid) {
                parse_invalid_variable_name_error(path, line_number, name.cstr);
                str_free(name);
                return false;
            }

            append_pattern_segment(segments, SEGMENT_VARIABLE_DEF, name.cstr,
                                   colon + 1,
                                   body_len - (size_t)(colon - body) - 1);
            str_free(name);
            cursor = (size_t)(end - pattern_text) + 2;
            continue;
        }
    }

    return true;
}

// Return whether `line` contains a recognized directive, and populate the
// parsed pieces when it does.
static bool parse_directive_prefix(const char *line, const char **name_start,
                                   const char **pattern_start) {
    const char *cursor = line;

    while (str_char_is_ascii_space(*cursor))
        cursor++;
    if (*cursor != '#')
        return false;
    cursor++;
    while (str_char_is_ascii_space(*cursor))
        cursor++;

    *name_start = cursor;
    while (*cursor != '\0' && *cursor != ':')
        cursor++;
    if (*cursor != ':')
        return false;

    *pattern_start = cursor + 1;
    while (str_char_is_ascii_space(**pattern_start))
        (*pattern_start)++;
    return true;
}

// Parse the input test file and extract the single RUN command plus ordered
// CHECK directives.
static bool parse_test_file(const char *path, ParsedTest *parsed) {
    FILE *stream = fopen(path, "r");
    char *line = NULL;
    size_t line_capacity = 0;
    int line_number = 0;
    bool ok = true;

    require(stream != NULL, "failed to open test file: %errno");

    while (getline(&line, &line_capacity, stream) >= 0) {
        const char *directive_name = NULL;
        const char *payload = NULL;

        line_number++;
        str_trim_trailing_chars_cstr(line, "\r\n");
        if (!parse_directive_prefix(line, &directive_name, &payload))
            continue;

        size_t name_len =
            (size_t)(strchr(directive_name, ':') - directive_name);

        if (name_len == 3 && strncmp(directive_name, "RUN", 3) == 0) {
            if (parsed->run_command.len != 0) {
                parse_error(path, line_number,
                            "multiple RUN directives are not supported");
                ok = false;
                break;
            }
            if (payload[0] == '\0') {
                parse_error(path, line_number,
                            "RUN directive requires a command");
                ok = false;
                break;
            }

            parsed->run_command = str_from_cstr(payload);
            continue;
        }

        DirectiveKind kind;
        if (name_len == 5 && strncmp(directive_name, "CHECK", 5) == 0) {
            kind = DIRECTIVE_CHECK;
        } else if (name_len == 10 &&
                   strncmp(directive_name, "CHECK-SAME", 10) == 0) {
            kind = DIRECTIVE_CHECK_SAME;
        } else if (name_len == 10 &&
                   strncmp(directive_name, "CHECK-NEXT", 10) == 0) {
            kind = DIRECTIVE_CHECK_NEXT;
        } else if (name_len == 9 &&
                   strncmp(directive_name, "CHECK-NOT", 9) == 0) {
            kind = DIRECTIVE_CHECK_NOT;
        } else {
            continue;
        }

        CheckDirective directive = {
            .kind = kind,
            .line_number = line_number,
            .raw_pattern = str_from_cstr(payload),
            .segments = arr_empty,
        };

        ok = parse_pattern_segments(path, line_number, payload,
                                    &directive.segments);
        if (!ok) {
            check_directive_free(&directive);
            break;
        }

        arr_push(parsed->directives, directive);
    }

    require(!ferror(stream), "failed to read test file: %errno");

    free(line);
    fclose(stream);

    if (!ok)
        return false;
    if (parsed->run_command.len == 0) {
        parse_error(path, 1, "missing RUN directive");
        return false;
    }
    if (parsed->directives.size == 0) {
        parse_error(path, 1, "missing CHECK directives");
        return false;
    }
    return true;
}

// Expand lit-style substitutions in one RUN command. `%s` becomes the
// shell-quoted absolute test path, and `%%` becomes a literal percent sign.
static String expand_run_command(const char *command, const char *test_path) {
    String expanded = str_empty;

    for (size_t i = 0; command[i] != '\0'; ++i) {
        if (command[i] != '%') {
            str_push(expanded, command[i]);
            continue;
        }

        if (command[i + 1] == 's') {
            str_append_shell_quoted_word(&expanded, test_path);
            ++i;
            continue;
        }
        if (command[i + 1] == '%') {
            str_push(expanded, '%');
            ++i;
            continue;
        }

        str_push(expanded, command[i]);
    }

    return expanded;
}

// Resolve the per-test output directory to an absolute path. When the caller
// did not provide one, create a fresh temporary directory and export it so the
// child RUN command sees a stable absolute path. The caller owns the returned
// String and frees it with str_free.
static String resolve_test_output_dir(void) {
    const char *configured_dir = getenv(test_output_dir_env);
    String output_dir = str_empty;

    if (configured_dir != NULL && configured_dir[0] != '\0') {
        require(path_resolve_absolute(&output_dir, configured_dir),
                "failed to resolve test output directory: %errno");
    } else {
        char template[] = "/tmp/imgneko-run-and-check.XXXXXX";
        char *created_dir = mkdtemp(template);

        require(created_dir != NULL, "mkdtemp failed: %errno");
        output_dir = str_from_cstr(created_dir);
    }

    require(mkdir_p(output_dir.cstr),
            "failed to create test output directory: %errno");
    require(setenv(test_output_dir_env, output_dir.cstr, 1) == 0,
            "failed to update test output env: %errno");

    return output_dir;
}

// Build a regex from a parsed directive using the currently visible variable
// values and compile it for matching.
static bool compile_pattern(const char *path, const CheckDirective *directive,
                            const VariableContext *variables,
                            CompiledPattern *compiled) {
    for (size_t i = 0; i < directive->segments.size; ++i) {
        const PatternSegment *segment = &directive->segments.data[i];

        switch (segment->kind) { // IMGNEKO_UNCOVERED_OK
        case SEGMENT_LITERAL:
            append_regex_escaped_literal(&compiled->regex_text,
                                         segment->text.cstr, segment->text.len);
            break;
        case SEGMENT_REGEX:
            str_append_str(compiled->regex_text, segment->text);
            compiled->capture_group_count +=
                regex_capture_group_count(segment->text.cstr);
            break;
        case SEGMENT_VARIABLE_DEF: {
            if (directive->kind == DIRECTIVE_CHECK_NOT) {
                fprintf(stderr,
                        "%s:%d: error: variable definitions are not allowed in "
                        "%s: [[%s]]\n",
                        path, directive->line_number,
                        directive_kind_name(directive->kind),
                        segment->name.cstr);
                return false;
            }

            CaptureBinding binding = {
                .name = segment->name.cstr,
                .group_index = compiled->capture_group_count + 1,
            };

            str_push(compiled->regex_text, '(');
            str_append_str(compiled->regex_text, segment->text);
            str_push(compiled->regex_text, ')');
            arr_push(compiled->bindings, binding);
            compiled->capture_group_count +=
                1 + regex_capture_group_count(segment->text.cstr);
            break;
        }
        case SEGMENT_VARIABLE_USE: {
            const char *value =
                variable_context_get(variables, segment->name.cstr);

            if (value == NULL) {
                fprintf(stderr,
                        "%s:%d: error: undefined variable [[%s]] in %s\n", path,
                        directive->line_number, segment->name.cstr,
                        directive_kind_name(directive->kind));
                return false;
            }

            append_regex_escaped_literal(&compiled->regex_text, value,
                                         strlen(value));
            break;
        }
        }
    }

    int rc = regcomp(&compiled->regex, compiled->regex_text.cstr, REG_EXTENDED);
    if (rc != 0) {
        char buffer[256];

        regerror(rc, &compiled->regex, buffer, sizeof(buffer));
        fprintf(stderr, "%s:%d: error: invalid regex in %s: %s\n", path,
                directive->line_number, directive_kind_name(directive->kind),
                buffer);
        fprintf(stderr, "%s:%d: note: expanded regex: %s\n", path,
                directive->line_number, compiled->regex_text.cstr);
        return false;
    }

    compiled->regex_ready = true;
    return true;
}

// Search one output line segment for a match. When `pmatch` is non-NULL it
// must point at `nmatch` writable entries.
static bool regex_search_segment(const CompiledPattern *pattern,
                                 const OutputLine *line, size_t start_column,
                                 size_t end_column, regmatch_t *pmatch,
                                 size_t nmatch) {
    assert(pmatch != NULL);
    assert(nmatch != 0);

    regmatch_t *matches = pmatch;
    size_t match_count = nmatch;
    String window = str_empty;
    const char *subject = line->text.cstr + start_column;
    int regexec_flags = 0;

    // IMGNEKO_UNCOVERED_OK_START
    if (start_column > line->text.len || end_column > line->text.len ||
        start_column > end_column) {
        return false;
    }
    // IMGNEKO_UNCOVERED_OK_END

    if (line->text.cstr[end_column] != '\0') {
        window = str_from_data(subject, end_column - start_column);
        subject = window.cstr;
    }
    if (start_column != 0)
        regexec_flags |= REG_NOTBOL;
    if (end_column != line->text.len)
        regexec_flags |= REG_NOTEOL;

    int rc =
        regexec(&pattern->regex, subject, match_count, matches, regexec_flags);
    if (rc == 0) {
        for (size_t i = 0; i < match_count; ++i) {
            // regexec() reports offsets relative to `subject`. Shift every
            // present match back to the original line coordinates. Optional
            // groups that did not participate are returned as negative offsets.
            // IMGNEKO_UNCOVERED_OK: `rm_eo < 0` is always false
            if (matches[i].rm_so < 0 || matches[i].rm_eo < 0)
                continue;
            matches[i].rm_so += (regoff_t)start_column;
            matches[i].rm_eo += (regoff_t)start_column;
        }
        str_free(window);
        return true;
    }
    // IMGNEKO_UNCOVERED_OK: Hard to trigger anything but REG_NOMATCH or success
    if (rc == REG_NOMATCH) {
        str_free(window);
        return false;
    }

    // IMGNEKO_UNCOVERED_OK_START
    {
        char buffer[256];
        regerror(rc, &pattern->regex, buffer, sizeof(buffer));
        str_free(window);
        fprintf(stderr, "error: regexec failed: %s\n", buffer);
        exit(1);
    }
    // IMGNEKO_UNCOVERED_OK_END
}

// Read the stdout file into logical lines so matching keeps its line-oriented
// CHECK, CHECK-SAME, and CHECK-NEXT semantics without holding the raw file
// contents in one large string.
static void read_output_lines(const char *path, OutputLineArray *lines) {
    StringArray file_lines = arr_empty;

    if (!file_read_lines(&file_lines, path, -1)) {
        fprintf(stderr, "error: failed to read output file %s: %s\n", path,
                strerror(errno));
        exit(1);
    }

    for (size_t i = 0; i < file_lines.size; ++i) {
        str_trim_trailing_chars(&file_lines.data[i], "\r\n");
        arr_push(*lines, ((OutputLine){.text = file_lines.data[i]}));
        file_lines.data[i] = (String)str_empty;
    }

    str_array_free(&file_lines);
}

// Return the logical EOF position for the captured output.
static OutputPosition output_eof_position(const OutputLineArray *lines) {
    if (lines->size == 0)
        return (OutputPosition){.line_index = 0, .column = 0};

    return (OutputPosition){
        .line_index = lines->size - 1,
        .column = lines->data[lines->size - 1].text.len,
    };
}

// Search forward from `start` for the next CHECK match.
static bool find_check_match(const CompiledPattern *pattern,
                             const OutputLineArray *lines, OutputPosition start,
                             LineMatch *match_out, regmatch_t *captures,
                             size_t capture_count) {
    for (size_t line_index = start.line_index; line_index < lines->size;
         ++line_index) {
        size_t line_start = line_index == start.line_index ? start.column : 0;

        if (!regex_search_segment(
                pattern, &lines->data[line_index],
                /*start_column=*/line_start,
                /*end_column=*/lines->data[line_index].text.len,
                /*pmatch=*/captures, /*nmatch=*/capture_count)) {
            continue;
        }

        *match_out = (LineMatch){
            .line_index = line_index,
            .start_column = (size_t)captures[0].rm_so,
            .end_column = (size_t)captures[0].rm_eo,
        };
        return true;
    }

    return false;
}

// Search the remainder of the current line for a CHECK-SAME match.
static bool find_check_same_match(const CompiledPattern *pattern,
                                  const OutputLineArray *lines,
                                  const LineMatch *previous_match,
                                  LineMatch *match_out, regmatch_t *captures,
                                  size_t capture_count) {
    // IMGNEKO_UNCOVERED_OK[2 lines]
    if (previous_match->line_index >= lines->size)
        return false;

    if (!regex_search_segment(
            pattern, &lines->data[previous_match->line_index],
            /*start_column=*/previous_match->end_column,
            /*end_column=*/lines->data[previous_match->line_index].text.len,
            /*pmatch=*/captures, /*nmatch=*/capture_count)) {
        return false;
    }

    *match_out = (LineMatch){
        .line_index = previous_match->line_index,
        .start_column = (size_t)captures[0].rm_so,
        .end_column = (size_t)captures[0].rm_eo,
    };
    return true;
}

// Search only the next line for a CHECK-NEXT match.
static bool find_check_next_match(const CompiledPattern *pattern,
                                  const OutputLineArray *lines,
                                  const LineMatch *previous_match,
                                  LineMatch *match_out, regmatch_t *captures,
                                  size_t capture_count) {
    size_t line_index = previous_match->line_index + 1;

    if (line_index >= lines->size)
        return false;

    if (!regex_search_segment(pattern, &lines->data[line_index],
                              /*start_column=*/0,
                              /*end_column=*/lines->data[line_index].text.len,
                              /*pmatch=*/captures, /*nmatch=*/capture_count)) {
        return false;
    }

    *match_out = (LineMatch){
        .line_index = line_index,
        .start_column = (size_t)captures[0].rm_so,
        .end_column = (size_t)captures[0].rm_eo,
    };
    return true;
}

// Return whether the half-open output region [start, end) contains no bytes.
static bool output_region_is_empty(const OutputLineArray *lines,
                                   OutputPosition start, OutputPosition end) {
    if (lines->size == 0)
        return true;
    if (start.line_index == end.line_index)
        return start.column >= end.column;

    // IMGNEKO_UNCOVERED_OK[2 lines]
    if (start.line_index >= lines->size || end.line_index >= lines->size)
        return false;

    if (start.column < lines->data[start.line_index].text.len)
        return false;

    for (size_t i = start.line_index + 1; i < end.line_index; ++i) {
        if (lines->data[i].text.len != 0)
            return false;
    }

    return end.column == 0;
}

// Print an output line after escaping non-printable bytes so diagnostics stay
// readable and unambiguous even for binary output.
static void print_escaped_line(FILE *stream, const char *text, size_t len) {
    String escaped = str_from_escaped_bytes(text, len);
    fputs(escaped.cstr, stream);
    str_free(escaped);
}

// Render one output line for diagnostics.
static void print_output_line_note(const char *path, int directive_line,
                                   size_t output_line_number,
                                   const OutputLine *line) {
    fprintf(stderr, "%s:%d: note: output line %zu: ", path, directive_line,
            output_line_number);
    print_escaped_line(stderr, line->text.cstr, line->text.len);
    fputc('\n', stderr);
}

// Verify that every pending CHECK-NOT directive is absent from the given
// output region, including same-line gaps between adjacent positive matches.
// When a CHECK-NOT matches forbidden output, this prints an error and a
// matching output line note before returning false.
static bool verify_negative_region(const char *path,
                                   const CheckDirectivePtrArray *pending_not,
                                   OutputPosition region_start,
                                   OutputPosition region_end,
                                   const OutputLineArray *lines,
                                   const VariableContext *variables) {
    if (pending_not->size == 0 ||
        output_region_is_empty(lines, region_start, region_end)) {
        return true;
    }

    for (size_t i = 0; i < pending_not->size; ++i) {
        const CheckDirective *directive = pending_not->data[i];
        CompiledPattern pattern = {
            .regex_text = str_empty,
            .bindings = arr_empty,
        };
        bool found = false;
        LineMatch match = {0};

        if (!compile_pattern(path, directive, variables, &pattern)) {
            compiled_pattern_free(&pattern);
            return false;
        }

        for (size_t line_index = region_start.line_index;
             line_index < lines->size && line_index <= region_end.line_index;
             ++line_index) {
            size_t start_column = 0;
            size_t end_column = lines->data[line_index].text.len;
            regmatch_t whole_match;

            if (line_index == region_start.line_index)
                start_column = region_start.column;
            if (line_index == region_end.line_index)
                end_column = region_end.column;
            if (start_column == end_column)
                continue;
            if (!regex_search_segment(&pattern, &lines->data[line_index],
                                      /*start_column=*/start_column,
                                      /*end_column=*/end_column,
                                      /*pmatch=*/&whole_match,
                                      /*nmatch=*/1)) {
                continue;
            }

            match = (LineMatch){
                .line_index = line_index,
                .start_column = (size_t)whole_match.rm_so,
                .end_column = (size_t)whole_match.rm_eo,
            };
            found = true;
            break;
        }

        compiled_pattern_free(&pattern);
        if (!found)
            continue;

        fprintf(stderr, "%s:%d: error: %s matched forbidden output\n", path,
                directive->line_number, directive_kind_name(directive->kind));
        fprintf(stderr, "%s:%d: note: pattern: %s\n", path,
                directive->line_number, directive->raw_pattern.cstr);
        print_output_line_note(path, directive->line_number,
                               match.line_index + 1,
                               &lines->data[match.line_index]);
        return false;
    }

    return true;
}

// Store all named captures produced by a successful positive match.
static void apply_captures(VariableContext *variables,
                           const CompiledPattern *pattern,
                           const OutputLine *line, const regmatch_t *captures) {
    for (size_t i = 0; i < pattern->bindings.size; ++i) {
        const CaptureBinding *binding = &pattern->bindings.data[i];
        regmatch_t match = captures[binding->group_index];

        // IMGNEKO_UNCOVERED_OK[2 lines]
        if (match.rm_so < 0 || match.rm_eo < 0)
            continue;

        variable_context_set(variables, binding->name,
                             line->text.cstr + match.rm_so,
                             (size_t)(match.rm_eo - match.rm_so));
    }
}

// Emit a helpful failure explaining why a positive directive did not match.
static bool report_positive_match_failure(const char *path,
                                          const CheckDirective *directive,
                                          const OutputLineArray *lines,
                                          OutputPosition search_start,
                                          const LineMatch *previous_positive) {
    fprintf(stderr, "%s:%d: error: %s did not match\n", path,
            directive->line_number, directive_kind_name(directive->kind));
    fprintf(stderr, "%s:%d: note: pattern: %s\n", path, directive->line_number,
            directive->raw_pattern.cstr);

    if (directive->kind == DIRECTIVE_CHECK_SAME) {
        assert(previous_positive != NULL);
        print_output_line_note(path, directive->line_number,
                               previous_positive->line_index + 1,
                               &lines->data[previous_positive->line_index]);
    } else if (directive->kind == DIRECTIVE_CHECK_NEXT) {
        assert(previous_positive != NULL);
        if (previous_positive->line_index + 1 < lines->size) {
            print_output_line_note(
                path, directive->line_number, previous_positive->line_index + 2,
                &lines->data[previous_positive->line_index + 1]);
        } else {
            fprintf(
                stderr,
                "%s:%d: note: there is no next output line after line %zu\n",
                path, directive->line_number,
                previous_positive->line_index + 1);
        }
    } else if (search_start.line_index < lines->size) {
        print_output_line_note(path, directive->line_number,
                               search_start.line_index + 1,
                               &lines->data[search_start.line_index]);
    } else {
        fprintf(stderr, "%s:%d: note: there is no remaining output to search\n",
                path, directive->line_number);
    }

    return false;
}

// Evaluate all CHECK directives against the redirected stdout file.
static bool run_checks(const char *path, const ParsedTest *parsed,
                       const char *stdout_path) {
    OutputLineArray lines = arr_empty;
    VariableContext variables;
    CheckDirectivePtrArray pending_not = arr_empty;
    OutputPosition region_start = {.line_index = 0, .column = 0};
    LineMatch previous_positive = {0};
    bool have_previous_positive = false;
    bool ok = true;

    read_output_lines(stdout_path, &lines);
    variable_context_init(&variables);

    for (size_t i = 0; i < parsed->directives.size; ++i) {
        const CheckDirective *directive = &parsed->directives.data[i];

        if (directive->kind == DIRECTIVE_CHECK_NOT) {
            arr_push(pending_not, (CheckDirective *)directive);
            continue;
        }

        if ((directive->kind == DIRECTIVE_CHECK_SAME ||
             directive->kind == DIRECTIVE_CHECK_NEXT) &&
            !have_previous_positive) {
            fprintf(stderr,
                    "%s:%d: error: %s requires a previous positive match\n",
                    path, directive->line_number,
                    directive_kind_name(directive->kind));
            ok = false;
            goto cleanup;
        }

        CompiledPattern pattern = {
            .regex_text = str_empty,
            .bindings = arr_empty,
        };
        OutputPosition search_start = {.line_index = 0, .column = 0};
        regmatch_t *captures = NULL;
        size_t capture_count;
        LineMatch match = {0};
        bool matched = false;

        if (!compile_pattern(path, directive, &variables, &pattern)) {
            compiled_pattern_free(&pattern);
            ok = false;
            goto cleanup;
        }

        capture_count = pattern.capture_group_count + 1;
        captures = calloc(capture_count, sizeof(*captures));
        require(captures != NULL,
                "failed to allocate regex match array: %errno");

        if (directive->kind == DIRECTIVE_CHECK) {
            if (have_previous_positive) {
                search_start.line_index = previous_positive.line_index + 1;
                search_start.column = 0;
            }
            matched = find_check_match(&pattern, &lines, search_start, &match,
                                       captures, capture_count);
        } else if (directive->kind == DIRECTIVE_CHECK_SAME) {
            matched =
                find_check_same_match(&pattern, &lines, &previous_positive,
                                      &match, captures, capture_count);
        } else if (/*IMGNEKO_UNCOVERED_OK*/ directive->kind ==
                   DIRECTIVE_CHECK_NEXT) {
            matched =
                find_check_next_match(&pattern, &lines, &previous_positive,
                                      &match, captures, capture_count);
        }

        if (!matched) {
            OutputPosition failure_start = search_start;

            if (directive->kind == DIRECTIVE_CHECK_SAME ||
                directive->kind == DIRECTIVE_CHECK_NEXT) {
                failure_start.line_index = previous_positive.line_index;
                failure_start.column = previous_positive.end_column;
            }
            ok = report_positive_match_failure(
                path, directive, &lines, failure_start,
                have_previous_positive ? &previous_positive : NULL);
            free(captures);
            compiled_pattern_free(&pattern);
            goto cleanup;
        }

        if (!verify_negative_region(path, &pending_not, region_start,
                                    (OutputPosition){
                                        .line_index = match.line_index,
                                        .column = match.start_column,
                                    },
                                    &lines, &variables)) {
            free(captures);
            compiled_pattern_free(&pattern);
            ok = false;
            goto cleanup;
        }

        apply_captures(&variables, &pattern, &lines.data[match.line_index],
                       captures);
        arr_clear(pending_not);
        previous_positive = match;
        have_previous_positive = true;
        region_start = (OutputPosition){
            .line_index = match.line_index,
            .column = match.end_column,
        };

        free(captures);
        compiled_pattern_free(&pattern);
    }

    if (!verify_negative_region(path, &pending_not, region_start,
                                output_eof_position(&lines), &lines,
                                &variables)) {
        ok = false;
        goto cleanup;
    }

cleanup:
    arr_free(pending_not);
    variable_context_free(&variables);
    output_line_array_free(&lines);
    return ok;
}

// Run the configured shell command from the test output directory with stdout
// and stderr redirected to dedicated files there.
static int run_command_capture(const char *command, const char *output_dir,
                               const char *stdout_path,
                               const char *stderr_path) {
    int stdout_fd = -1;
    int stderr_fd = -1;
    pid_t pid;
    int status = 1;

    stdout_fd = open(stdout_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    require(stdout_fd >= 0, "failed to open stdout output file: %errno");
    stderr_fd = open(stderr_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (stderr_fd < 0) { // IMGNEKO_UNCOVERED_OK_START
        close(stdout_fd);
        die_errno("failed to open stderr output file");
    } // IMGNEKO_UNCOVERED_OK_END

    pid = fork();
    if (pid < 0) { // IMGNEKO_UNCOVERED_OK_START
        close(stderr_fd);
        close(stdout_fd);
        die_errno("fork failed");
    } // IMGNEKO_UNCOVERED_OK_END

    // IMGNEKO_UNCOVERED_OK_START
    if (pid == 0) {
        if (dup2(stdout_fd, STDOUT_FILENO) < 0 ||
            dup2(stderr_fd, STDERR_FILENO) < 0) {
            fprintf(stderr, "error: dup2 failed: %s\n", strerror(errno));
            _exit(127);
        }

        close(stderr_fd);
        close(stdout_fd);

        if (setenv(test_output_dir_env, output_dir, 1) != 0) {
            fprintf(stderr, "error: failed to update test output env: %s\n",
                    strerror(errno));
            _exit(127);
        }
        if (chdir(output_dir) != 0) {
            fprintf(stderr, "error: failed to chdir to %s: %s\n", output_dir,
                    strerror(errno));
            _exit(127);
        }

        execl("/bin/sh", "sh", "-c", command, (char *)NULL);

        fprintf(stderr, "error: failed to exec /bin/sh: %s\n", strerror(errno));
        _exit(127);
    }
    // IMGNEKO_UNCOVERED_OK_END

    close(stderr_fd);
    close(stdout_fd);

    require(waitpid(pid, &status, 0) >= 0, "waitpid failed: %errno");
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    if (WIFSIGNALED(status))
        return 128 + WTERMSIG(status);
    return 1; // IMGNEKO_UNCOVERED_OK
}

// Print the RUN command metadata so direct invocations show exactly which
// command ran and where the redirected streams were written.
static void print_run_metadata(const char *path, const char *command,
                               const char *stdout_path,
                               const char *stderr_path) {
    fprintf(stderr, "%s: note: RUN: %s\n", path, command);
    fprintf(stderr, "%s: note: stdout file: %s\n", path, stdout_path);
    fprintf(stderr, "%s: note: stderr file: %s\n", path, stderr_path);
}

// Print the shell exit-like status after the RUN command finishes.
static void print_run_exit_code(const char *path, int exit_code) {
    fprintf(stderr, "%s: note: RUN exit code: %d\n", path, exit_code);
}

// Print the final high-level run-and-check outcome for this test file.
static void print_result(const char *path, bool ok) {
    fprintf(stderr, "%s: note: run-and-check result: %s\n", path,
            ok ? "PASS" : "FAIL");
}

// Print the last few lines from one redirected output file to aid failure
// diagnosis without dumping the entire file.
static void print_file_tail(const char *path, const char *label,
                            const char *file_path, size_t max_lines) {
    StringArray lines = arr_empty;

    if (!file_read_lines(&lines, file_path, (ptrdiff_t)max_lines)) {
        fprintf(stderr, "%s: note: failed to open %s file %s: %s\n", path,
                label, file_path, strerror(errno));
        return;
    }

    if (lines.size == 0) {
        fprintf(stderr, "%s: note: %s file is empty: %s\n", path, label,
                file_path);
        str_array_free(&lines);
        return;
    }

    fprintf(stderr, "%s: note: last %zu lines of %s (%s):\n", path, lines.size,
            label, file_path);
    for (size_t i = 0; i < lines.size; ++i) {
        str_trim_trailing_chars(&lines.data[i], "\r\n");
        print_escaped_line(stderr, lines.data[i].cstr, lines.data[i].len);
        fputc('\n', stderr);
    }
    str_array_free(&lines);
}

// Print redirected output tails after any failure so the recent stdout/stderr
// context is available even when the full files are large.
static void print_failure_output_tails(const char *path,
                                       const char *stdout_path,
                                       const char *stderr_path) {
    print_file_tail(path, "stdout", stdout_path, 10);
    print_file_tail(path, "stderr", stderr_path, 10);
}

int main(int argc, char **argv) {
    ParsedTest parsed = {
        .run_command = str_empty,
        .directives = arr_empty,
    };
    String test_path = str_empty;
    String output_dir = str_empty;
    String stdout_path = str_empty;
    String stderr_path = str_empty;
    String expanded_command = str_empty;
    int run_status;
    int exit_code = 1;
    bool run_completed = false;

    if (argc != 2)
        die_usage(argv[0]);

    if (!parse_test_file(argv[1], &parsed))
        goto cleanup;

    require(path_resolve_absolute(&test_path, argv[1]),
            "failed to resolve test path: %errno");
    output_dir = resolve_test_output_dir();
    stdout_path = path_join(output_dir.cstr, stdout_file_name);
    stderr_path = path_join(output_dir.cstr, stderr_file_name);
    expanded_command =
        expand_run_command(parsed.run_command.cstr, test_path.cstr);

    print_run_metadata(argv[1], expanded_command.cstr, stdout_path.cstr,
                       stderr_path.cstr);
    run_status = run_command_capture(expanded_command.cstr, output_dir.cstr,
                                     stdout_path.cstr, stderr_path.cstr);
    run_completed = true;
    print_run_exit_code(argv[1], run_status);
    if (run_status != 0) {
        fprintf(stderr, "%s: error: RUN command failed with exit code %d\n",
                argv[1], run_status);
        goto cleanup;
    }

    if (!run_checks(argv[1], &parsed, stdout_path.cstr))
        goto cleanup;

    exit_code = 0;

cleanup:
    if (run_completed && exit_code != 0)
        print_failure_output_tails(argv[1], stdout_path.cstr, stderr_path.cstr);

    assert(argc > 1);
    print_result(argv[1], exit_code == 0);

    str_free(expanded_command);
    str_free(stderr_path);
    str_free(stdout_path);
    str_free(output_dir);
    str_free(test_path);
    parsed_test_free(&parsed);
    return exit_code;
}
