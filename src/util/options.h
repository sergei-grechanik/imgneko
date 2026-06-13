// SPDX-License-Identifier: MIT-0

#ifndef UTIL_OPTIONS_H
#define UTIL_OPTIONS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "util/array.h"
#include "util/common.h"
#include "util/string.h"

// Tracks where the currently stored option value came from.
typedef enum OptProvenance {
    OPT_PROVENANCE_NONE = 0,
    OPT_PROVENANCE_DEFAULT,
    OPT_PROVENANCE_CLI,
} OptProvenance;

// Supported behaviors for built-in boolean options.
typedef enum OptBoolMode {
    // Not a boolean field. Non-boolean options consume a separate value token.
    OPT_BOOL_MODE_NONE = 0,
    // A flag that defaults to false unless set by the user (like --verbose).
    OPT_BOOL_MODE_FLAG,
    // A long option that also accepts `--no-...` (like --cache / --no-cache).
    OPT_BOOL_MODE_NEGATABLE,
    // Explicit boolean value (like --cache=true / --cache=false).
    OPT_BOOL_MODE_EXPLICIT,
} OptBoolMode;

// Define an option wrapper with a required `.value` field plus framework-owned
// metadata. Value types are expected to use all-zero memory as their empty
// state so schemas can be initialized with `memset()` before defaults are
// applied.
#define OPT_DEFINE_WRAPPER_STRUCT(WrapperName, ValueType)                      \
    typedef struct WrapperName {                                               \
        ValueType value;                                                       \
        bool is_set;                                                           \
        uint8_t provenance;                                                    \
    } WrapperName

// Option wrapper structs for the most common value types.
OPT_DEFINE_WRAPPER_STRUCT(OptBool, bool);
OPT_DEFINE_WRAPPER_STRUCT(OptDouble, double);
OPT_DEFINE_WRAPPER_STRUCT(OptInt, int);
OPT_DEFINE_WRAPPER_STRUCT(OptString, String);
OPT_DEFINE_WRAPPER_STRUCT(OptStringList, StringArray);

// Parse a textual option value into the underlying `.value` object.
// `text == NULL` means the option was present without an explicit value.
// On failure, parsers may populate `error_out` with a short human-readable
// reason; the caller owns the final diagnostic formatting.
typedef bool (*OptValueParseFn)(void *value, const char *text, size_t text_len,
                                String *error_out);

// Validate an already parsed `.value` object before the new value is
// committed to the destination field.
typedef bool (*OptValueValidateFn)(const void *value, String *error_out);

// Clear the underlying `.value` object, releasing any owned resources.
typedef void (*OptValueClearFn)(void *value);

// Deep-copy the underlying `.value` object from src to dst.
typedef void (*OptValueCopyFn)(void *dst_value, const void *src_value);

// Store a short custom parse-error reason in `error_out`, replacing any
// previous contents. Returns false so parsers can write
// `return opt_parse_error(error_out, "...");`.
bool opt_parse_error(String *error_out, const char *message);

// Per-field schema metadata used by the generic parser and help generator.
typedef struct OptFieldAttrs {
    const char *cli;
    const char *cli_negate;
    const char *descr;
    bool positional;
    // Positional arguments with this flag are only accepted after a bare `--`.
    bool double_dash_only;
    // argparse-like cardinality:
    //   NULL or "1" = exactly one
    //   "?" = zero or one
    //   "*" = zero or more
    //   "+" = one or more
    const char *nargs;
    OptBoolMode bool_mode;
    const char *dflt;
    OptValueParseFn parse;
    OptValueValidateFn validate;
    OptValueClearFn clear;
    OptValueCopyFn copy;
} OptFieldAttrs;

// A field inside an option schema.
typedef struct OptFieldSpec {
    const char *name;
    const char *type_name;
    // Offset and size of the whole wrapper struct (e.g. OptInt).
    size_t offset;
    size_t size;
    // Offsets and size of the subfields (.value, .is_set, .provenance) within
    // the wrapper struct.
    size_t value_offset;
    size_t value_size;
    size_t is_set_offset;
    size_t provenance_offset;
    OptFieldAttrs attrs;
} OptFieldSpec;

// The generated schema for an option struct.
typedef struct OptSchema {
    const char *name;
    size_t size;
    const OptFieldSpec *fields;
    size_t field_count;
} OptSchema;

// Per-command metadata used by the generic program parser.
typedef struct OptCommandAttrs {
    const char *descr;
} OptCommandAttrs;

// A parsed command plus the schema of its option payload.
typedef struct OptCommandDesc {
    const char *name;
    OptCommandAttrs attrs;
    const OptSchema *schema;
} OptCommandDesc;

// A command entry inside a program parser.
typedef struct OptProgramCommand {
    const OptCommandDesc *command;
    // The numeric command id stored in the generated parsed-result struct.
    int command_id;
} OptProgramCommand;

// Top-level program parser metadata.
typedef struct OptProgramAttrs {
    const char *program_name;
    const char *descr;
    const char *default_command;
} OptProgramAttrs;

// A parser for a program with multiple subcommands.
typedef struct OptProgramParser {
    OptProgramAttrs attrs;
    const OptSchema *top_level_schema;
    const OptProgramCommand *commands;
    size_t command_count;
    // Byte offsets into the generated `Parsed<Name>` result object fields.
    size_t top_level_offset;
    size_t command_id_offset;
    size_t command_offset;
    size_t must_exit_offset;
    // Total size of the generated `Parsed<Name>` result object.
    size_t result_size;
} OptProgramParser;

// Shared alias for the "no selected command" value across all generated
// parser-specific command-id enums.
enum {
    OPT_CMD_NONE = 0,
};

// Build an OptFieldAttrs initializer fragment in a macro-friendly way.
#define OPT_ATTRS(...) (__VA_ARGS__)

// Build an OptCommandAttrs initializer fragment in a macro-friendly way.
#define OPT_COMMAND(...) (__VA_ARGS__)

// Build an OptProgramAttrs initializer fragment in a macro-friendly way.
#define OPT_PROGRAM(...) (__VA_ARGS__)

// Build presence-only boolean flag attrs. These flags default to false and
// become true when supplied.
#define OPT_BOOL_FLAG(...)                                                     \
    OPT_ATTRS(.bool_mode = OPT_BOOL_MODE_FLAG, .parse = opt_parse_bool_option, \
              .dflt = "false", __VA_ARGS__)

// Build negatable boolean flag attrs. These flags default to unset unless
// `.dflt` is provided, become true for `.cli` aliases, and false for
// `.cli_negate` aliases. Negative aliases are only those explicitly declared
// in `.cli_negate`.
#define OPT_BOOL_NEGATABLE(...)                                                \
    OPT_ATTRS(.bool_mode = OPT_BOOL_MODE_NEGATABLE,                            \
              .parse = opt_parse_bool_option, __VA_ARGS__)

// Build explicit boolean value attrs. These options require an explicit value
// such as `true`, `false`, `on`, or `off`.
#define OPT_BOOL_VALUE(...)                                                    \
    OPT_ATTRS(.bool_mode = OPT_BOOL_MODE_EXPLICIT,                             \
              .parse = opt_parse_bool_option, __VA_ARGS__)

// Build built-in int field attrs with the default parser.
#define OPT_INT(...) OPT_ATTRS(.parse = opt_parse_int_option, __VA_ARGS__)

// Build built-in double field attrs with the default parser.
#define OPT_DOUBLE(...) OPT_ATTRS(.parse = opt_parse_double_option, __VA_ARGS__)

// Build built-in string field attrs with the default parser and ownership
// hooks.
#define OPT_STRING(...)                                                        \
    OPT_ATTRS(.parse = opt_parse_string_option,                                \
              .clear = opt_clear_string_option,                                \
              .copy = opt_copy_string_option, __VA_ARGS__)

// Build built-in string-list field attrs with the default parser and ownership
// hooks.
#define OPT_STRING_LIST(...)                                                   \
    OPT_ATTRS(.parse = opt_parse_string_list_option,                           \
              .clear = opt_clear_string_list_option,                           \
              .copy = opt_copy_string_list_option, .nargs = "*", __VA_ARGS__)

// Build custom field attrs that take a value. Callers spell out `.parse`, and
// optionally `.copy` and `.clear` when the field owns resources.
#define OPT_CUSTOM(...) OPT_ATTRS(__VA_ARGS__)

// Build custom list field attrs that append one parsed value per occurrence.
#define OPT_CUSTOM_LIST(...) OPT_ATTRS(.nargs = "*", __VA_ARGS__)

// Parse a strict base-10 int from raw text, rejecting empty input, overflow,
// and junk.
bool opt_parse_int_span(const char *text, size_t text_len, int *out);

// Parse a strict base-10 int64_t from raw text, rejecting empty input,
// overflow, and junk.
bool opt_parse_int64_span(const char *text, size_t text_len, int64_t *out);

// Parse a strict unsigned 64-bit integer from raw text. Values without a
// prefix are parsed as decimal; values with a 0x or 0X prefix are parsed as
// hexadecimal. Empty input, signs, overflow, and junk are rejected.
bool opt_parse_uint64_hex_or_decimal_span(const char *text, size_t text_len,
                                          uint64_t *out);

// Parse a strict floating-point value from raw text, rejecting empty input,
// overflow, and junk.
bool opt_parse_double_span(const char *text, size_t text_len, double *out);

// Parse a bool value from an explicit or synthesized true/false-like string.
bool opt_parse_bool_option(void *value, const char *text, size_t text_len,
                           String *error_out);

// Parse a floating-point value from a textual number.
bool opt_parse_double_option(void *value, const char *text, size_t text_len,
                             String *error_out);

// Parse an int value from a textual decimal integer.
bool opt_parse_int_option(void *value, const char *text, size_t text_len,
                          String *error_out);

// Validate that an already parsed double is finite and non-negative.
bool opt_validate_non_negative_double(const void *value, String *error_out);

// Validate that an already parsed int is positive.
bool opt_validate_positive_int(const void *value, String *error_out);

// Validate that an already parsed double is a finite probability in the
// inclusive range [0, 1].
bool opt_validate_probability(const void *value, String *error_out);

// Parse a string value from raw text, replacing any previous owned string.
bool opt_parse_string_option(void *value, const char *text, size_t text_len,
                             String *error_out);

// Parse a string-list value by appending one raw textual value.
bool opt_parse_string_list_option(void *value, const char *text,
                                  size_t text_len, String *error_out);

// Clear a string value.
void opt_clear_string_option(void *value);

// Clear a string-list value.
void opt_clear_string_list_option(void *value);

// Deep-copy a string value.
void opt_copy_string_option(void *dst_value, const void *src_value);

// Deep-copy a string-list value.
void opt_copy_string_list_option(void *dst_value, const void *src_value);

// Return a human-readable provenance name.
const char *opt_provenance_name(OptProvenance provenance);

// Return a pointer to a mutable field wrapper stored inside an options
// object.
UTIL_INLINE void *opt_field_ptr(const OptFieldSpec *field, void *options) {
    return (char *)options + field->offset;
}

// Return a pointer to a const field wrapper stored inside an options object.
UTIL_INLINE const void *opt_const_field_ptr(const OptFieldSpec *field,
                                            const void *options) {
    return (const char *)options + field->offset;
}

// Return a pointer to the underlying `.value` object inside a mutable field
// wrapper.
UTIL_INLINE void *opt_field_value_ptr(const OptFieldSpec *field,
                                      void *options) {
    return (char *)opt_field_ptr(field, options) + field->value_offset;
}

// Return a pointer to the underlying `.value` object inside a const field
// wrapper.
UTIL_INLINE const void *opt_const_field_value_ptr(const OptFieldSpec *field,
                                                  const void *options) {
    return (const char *)opt_const_field_ptr(field, options) +
           field->value_offset;
}

// Return a pointer to a field's `is_set` flag.
UTIL_INLINE bool *opt_field_is_set_ptr(const OptFieldSpec *field,
                                       void *options) {
    return (bool *)((char *)opt_field_ptr(field, options) +
                    field->is_set_offset);
}

// Return a pointer to a const field's `is_set` flag.
UTIL_INLINE const bool *opt_const_field_is_set_ptr(const OptFieldSpec *field,
                                                   const void *options) {
    return (const bool *)((const char *)opt_const_field_ptr(field, options) +
                          field->is_set_offset);
}

// Return a pointer to a field's provenance.
UTIL_INLINE uint8_t *opt_field_provenance_ptr(const OptFieldSpec *field,
                                              void *options) {
    return (uint8_t *)((char *)opt_field_ptr(field, options) +
                       field->provenance_offset);
}

// Return a pointer to a const field's provenance.
UTIL_INLINE const uint8_t *
opt_const_field_provenance_ptr(const OptFieldSpec *field, const void *options) {
    return (const uint8_t *)((const char *)opt_const_field_ptr(field, options) +
                             field->provenance_offset);
}

// Return whether a field wrapper currently stores a value.
UTIL_INLINE bool opt_field_is_set(const OptFieldSpec *field,
                                  const void *options) {
    return *opt_const_field_is_set_ptr(field, options);
}

// Return the provenance stored in a field wrapper.
UTIL_INLINE OptProvenance opt_field_provenance(const OptFieldSpec *field,
                                               const void *options) {
    return (OptProvenance)*opt_const_field_provenance_ptr(field, options);
}

// Record whether a field currently stores a value and where it came from.
void opt_set_field_state(const OptFieldSpec *field, void *options, bool is_set,
                         OptProvenance provenance);

// Reset a field to an empty state, freeing any owned storage first.
void opt_clear_field(const OptFieldSpec *field, void *options);

// Apply a field's default string after the destination was zero-initialized.
void opt_apply_default(const OptFieldSpec *field, void *options);

// Parse, validate, and assign a textual value to a field, then update field
// state.
bool opt_assign_field_value(const OptFieldSpec *field, void *options,
                            const char *text, size_t text_len,
                            OptProvenance provenance, String *error_out);

// Deep-copy a matching field value from src to dst, including field state.
void opt_copy_field_value(const OptFieldSpec *dst_field, void *dst_options,
                          const OptFieldSpec *src_field,
                          const void *src_options);

// Initialize an option struct according to schema defaults.
void opt_schema_init(const OptSchema *schema, void *options);

// Free any owned storage inside an option struct.
void opt_schema_deinit(const OptSchema *schema, void *options);

// Find a field with the given name inside schema, or return NULL if absent.
const OptFieldSpec *opt_find_field_by_name(const OptSchema *schema,
                                           const char *field_name);

// Copy matching set fields from src into dst. Returns false on type mismatch.
bool opt_merge_matching(const OptSchema *dst_schema, void *dst,
                        const OptSchema *src_schema, const void *src);

// Free the selected parsed command payload and reset the result to zero.
void opt_program_result_deinit(const OptProgramParser *parser, void *result);

// Parse argv according to parser, print help or diagnostics as needed, and
// return a process exit code: 0 on success/help, 2 on CLI errors.
// `result` must point to the generated `Parsed<Name>` struct created by one of
// the `OPT_DEFINE_PROGRAM_PARSER*` macros. On successful parse,
// `result->must_exit` is false. After help or CLI errors, `result->must_exit`
// is true, so callers can skip normal processing after a fulfilled help
// request.
int opt_run_program_parser(const OptProgramParser *parser, int argc,
                           char **argv, void *result);

// Materialize a brace-wrapped initializer fragment after macro argument
// forwarding.
#define OPT__INIT_ARGS(args) OPT__INIT_ARGS_IMPL args
#define OPT__INIT_ARGS_IMPL(...)                                               \
    { __VA_ARGS__ }

#define OPT__DECLARE_FIELD(StructName, FieldName, FieldType, FieldAttrs)       \
    FieldType FieldName;

#define OPT__DEFINE_FIELD_SPEC(StructName, FieldName, FieldType, FieldAttrs)   \
    {                                                                          \
        .name = #FieldName,                                                    \
        .type_name = #FieldType,                                               \
        .offset = offsetof(StructName, FieldName),                             \
        .size = sizeof(FieldType),                                             \
        .value_offset = offsetof(FieldType, value),                            \
        .value_size = sizeof(((FieldType *)0)->value),                         \
        .is_set_offset = offsetof(FieldType, is_set),                          \
        .provenance_offset = offsetof(FieldType, provenance),                  \
        .attrs = OPT__INIT_ARGS(FieldAttrs),                                   \
    },

// Define a typed option struct, its schema, and init/deinit wrappers from an
// X-macro field list of the form:
//   #define MY_OPTIONS(X, S)
//       X(S, verbose, OptBool, OPT_BOOL_FLAG(.cli = "-v --verbose"))
//       X(S, output, OptString, OPT_STRING(.cli = "-o --output FILE"))
#define OPT_DEFINE_STRUCT(StructName, FIELD_LIST)                              \
    typedef struct StructName {                                                \
        FIELD_LIST(OPT__DECLARE_FIELD, StructName)                             \
    } StructName;                                                              \
    static const OptFieldSpec StructName##_field_specs[] = {                   \
        FIELD_LIST(OPT__DEFINE_FIELD_SPEC, StructName)};                       \
    static const OptSchema StructName##_schema = {                             \
        .name = #StructName,                                                   \
        .size = sizeof(StructName),                                            \
        .fields = StructName##_field_specs,                                    \
        .field_count = ARRAY_SIZE(StructName##_field_specs),                   \
    };                                                                         \
    UTIL_INLINE void StructName##_init(StructName *options) {                  \
        opt_schema_init(&StructName##_schema, options);                        \
    }                                                                          \
    UTIL_INLINE void StructName##_deinit(StructName *options) {                \
        opt_schema_deinit(&StructName##_schema, options);                      \
    }

#define OPT__PROGRAM_COMMAND_ID_ENUM(Name, CommandName, OptionsType,           \
                                     CommandAttrs)                             \
    OPT_CMD_##Name##_##CommandName,

#define OPT__PROGRAM_UNION_MEMBER(Name, CommandName, OptionsType,              \
                                  CommandAttrs)                                \
    OptionsType CommandName;

#define OPT__PROGRAM_COMMAND_DESC(Name, CommandName, OptionsType,              \
                                  CommandAttrs)                                \
    static const OptCommandDesc Name##_##CommandName##_command = {             \
        .name = #CommandName,                                                  \
        .attrs = OPT__INIT_ARGS(CommandAttrs),                                 \
        .schema = &OptionsType##_schema};

#define OPT__PROGRAM_COMMAND_ENTRY(Name, CommandName, OptionsType,             \
                                   CommandAttrs)                               \
    {                                                                          \
        .command = &Name##_##CommandName##_command,                            \
        .command_id = OPT_CMD_##Name##_##CommandName,                          \
    },

#define OPT__EMPTY_PROGRAM_COMMAND_LIST(Name, X)

#define OPT__DEFINE_PROGRAM_PARSER_IMPL(Name, ProgramAttrs, TopLevelSchema,    \
                                        TopLevelType, HasTopLevel,             \
                                        COMMAND_LIST)                          \
    COMMAND_LIST(Name, OPT__PROGRAM_COMMAND_DESC)                              \
    typedef enum Name##CommandId{                                              \
        OPT_CMD_NONE_##Name = OPT_CMD_NONE,                                    \
        COMMAND_LIST(Name, OPT__PROGRAM_COMMAND_ID_ENUM)} Name##CommandId;     \
    typedef union Name##CommandData {                                          \
        char __dummy;                                                          \
        COMMAND_LIST(Name, OPT__PROGRAM_UNION_MEMBER)                          \
    } Name##CommandData;                                                       \
    typedef struct Parsed##Name {                                              \
        TopLevelType top_level;                                                \
        Name##CommandId command_id;                                            \
        Name##CommandData command;                                             \
        bool must_exit;                                                        \
    } Parsed##Name;                                                            \
    static const OptProgramCommand Name##_program_commands[] = {               \
        COMMAND_LIST(Name, OPT__PROGRAM_COMMAND_ENTRY){                        \
            .command = NULL,                                                   \
            .command_id = OPT_CMD_NONE_##Name,                                 \
        },                                                                     \
    };                                                                         \
    static const OptProgramParser Name##_parser = {                            \
        .attrs = OPT__INIT_ARGS(ProgramAttrs),                                 \
        .top_level_schema = TopLevelSchema,                                    \
        .commands = Name##_program_commands,                                   \
        .command_count = ARRAY_SIZE(Name##_program_commands) - 1,              \
        .top_level_offset =                                                    \
            (HasTopLevel) ? offsetof(Parsed##Name, top_level) : 0,             \
        .command_id_offset = offsetof(Parsed##Name, command_id),               \
        .command_offset = offsetof(Parsed##Name, command),                     \
        .must_exit_offset = offsetof(Parsed##Name, must_exit),                 \
        .result_size = sizeof(Parsed##Name),                                   \
    }

// Define a top-level program parser plus the parsed result type from an
// X-macro command list of the form:
//   #define MY_COMMANDS(Name, X)
//       X(Name, show, ShowOptions, OPT_COMMAND(.descr = "Show items."))
//       X(Name, list, ListOptions, OPT_COMMAND(.descr = "List items."))
#define OPT_DEFINE_PROGRAM_PARSER(Name, ProgramAttrs, COMMAND_LIST)            \
    OPT__DEFINE_PROGRAM_PARSER_IMPL(Name, ProgramAttrs, NULL, char, false,     \
                                    COMMAND_LIST)

// Define a top-level program parser with options that are parsed regardless of
// which command is selected.
#define OPT_DEFINE_PROGRAM_PARSER_WITH_TOP_LEVEL_OPTIONS(                      \
    Name, ProgramAttrs, TopLevelOptionsType, COMMAND_LIST)                     \
    OPT__DEFINE_PROGRAM_PARSER_IMPL(Name, ProgramAttrs,                        \
                                    &TopLevelOptionsType##_schema,             \
                                    TopLevelOptionsType, true, COMMAND_LIST)

// Define a top-level parser for a program that has no subcommands. This uses
// the same OptProgramParser machinery as command-based programs, but with zero
// registered commands and the option schema stored in `Parsed<Name>.top_level`.
#define OPT_DEFINE_PROGRAM_PARSER_NO_COMMANDS(Name, ProgramAttrs, OptionsType) \
    OPT_DEFINE_PROGRAM_PARSER_WITH_TOP_LEVEL_OPTIONS(                          \
        Name, ProgramAttrs, OptionsType, OPT__EMPTY_PROGRAM_COMMAND_LIST)

#endif
