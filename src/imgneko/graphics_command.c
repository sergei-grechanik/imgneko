// SPDX-License-Identifier: MIT-0

// Graphics-command validation, control-data serialization, and parsing.
//
// The command reader and writer are intentionally implemented separately: the
// code in this file does not consume payload readers or split payloads into
// protocol chunks.

#include "imgneko/graphics_command.h"

#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define DEFAULT_START_SEQUENCE "\x1b_G"
#define DEFAULT_END_SEQUENCE "\x1b\\"

// State used to serialize a header into a bounded output buffer. `len` tracks
// the complete required size, including bytes that do not fit in `out`.
// `first` remains true until a key/value pair has been added, so the first pair
// is written without a leading comma.
typedef struct HeaderBuilder {
    char *out;
    size_t out_cap;
    size_t len;
    bool first;
} HeaderBuilder;

// Reset a detail buffer. The buffer must already be validated by the caller.
static void clear_detail(ImgnekoErrorDetail *detail) {
    if (detail == NULL)
        return;

    detail->message_len = 0;
    if (detail->message_cap != 0)
        detail->message[0] = '\0';
}

// Append a newline-separated diagnostic while retaining the complete required
// length when the caller's buffer is too small. The detail must have been
// cleared or initialized by an earlier diagnostic before calling this helper.
static void append_detail_v(ImgnekoErrorDetail *detail, const char *format,
                            va_list args) {
    va_list copied_args;

    if (detail == NULL)
        return;

    va_copy(copied_args, args);
    int required = vsnprintf(NULL, 0, format, copied_args);
    va_end(copied_args);
    // These formats use no wide-character conversions, so vsnprintf() cannot
    // report an encoding error.
    // IMGNEKO_UNCOVERED_OK
    size_t diagnostic_len = required < 0 ? 0 : (size_t)required;
    size_t separator_len = detail->message_len == 0 ? 0 : 1;

    // Once earlier text fills the buffer, only the complete required length
    // changes. Otherwise, append as much of the separator and message as fits.
    if (detail->message_cap != 0 &&
        detail->message_len < detail->message_cap - 1) {
        size_t offset = detail->message_len;
        if (separator_len != 0)
            detail->message[offset++] = '\n';

        va_list writing_args;
        va_copy(writing_args, args);
        (void)vsnprintf(detail->message + offset, detail->message_cap - offset,
                        format, writing_args);
        va_end(writing_args);
    }

    size_t appended_len = separator_len + diagnostic_len;
    // IMGNEKO_UNCOVERED_OK[2 lines]: Saturation requires unaddressable input.
    if (detail->message_len > SIZE_MAX - appended_len)
        detail->message_len = SIZE_MAX;
    else
        detail->message_len += appended_len;
}

static void append_detail(ImgnekoErrorDetail *detail, const char *format, ...) {
    va_list args;

    va_start(args, format);
    append_detail_v(detail, format, args);
    va_end(args);
}

// Return whether an optional detail-buffer description is internally valid.
static bool detail_is_valid(const ImgnekoErrorDetail *detail) {
    return detail == NULL || detail->message_cap == 0 ||
           detail->message != NULL;
}

const char *imgneko_command_error_string(ImgnekoCommandError error) {
    switch (error) {
    case IMGNEKO_COMMAND_OK:
        return "success";
    case IMGNEKO_COMMAND_INVALID_ARGUMENT:
        return "invalid argument";
    case IMGNEKO_COMMAND_INVALID_FIELD:
        return "invalid command field";
    case IMGNEKO_COMMAND_OVERFLOW:
        return "size overflow";
    case IMGNEKO_COMMAND_TOO_LARGE:
        return "command too large";
    case IMGNEKO_COMMAND_READ_FAILED:
        return "payload read failed";
    case IMGNEKO_COMMAND_BUFFER_TOO_SMALL:
        return "buffer too small";
    case IMGNEKO_COMMAND_WORKSPACE_TOO_SMALL:
        return "workspace too small";
    case IMGNEKO_COMMAND_WRITE_FAILED:
        return "write failed";
    default:
        return "unknown graphics command error";
    }
}

//===----------------------------------------------------------------------===//
// Validation
//===----------------------------------------------------------------------===//

static bool command_kind_is_known(ImgnekoCommandKindChar kind) {
    switch (kind) {
    case IMGNEKO_COMMAND_ANIMATION:
    case IMGNEKO_COMMAND_COMPOSE:
    case IMGNEKO_COMMAND_DELETE:
    case IMGNEKO_COMMAND_FRAME:
    case IMGNEKO_COMMAND_PUT:
    case IMGNEKO_COMMAND_QUERY:
    case IMGNEKO_COMMAND_TRANSMIT:
    case IMGNEKO_COMMAND_TRANSMIT_AND_PUT:
        return true;
    default:
        return false;
    }
}

static bool transmission_medium_is_known(ImgnekoTransmissionMediumChar medium) {
    return medium == IMGNEKO_TRANSMISSION_MEDIUM_DIRECT ||
           medium == IMGNEKO_TRANSMISSION_MEDIUM_FILE ||
           medium == IMGNEKO_TRANSMISSION_MEDIUM_TEMP_FILE ||
           medium == IMGNEKO_TRANSMISSION_MEDIUM_SHARED_MEMORY;
}

static bool delete_target_is_known(ImgnekoDeleteTargetChar target) {
    switch (target) {
    case IMGNEKO_DELETE_VISIBLE_PLACEMENTS:
    case IMGNEKO_DELETE_IMAGE_OR_PLACEMENT_BY_ID:
    case IMGNEKO_DELETE_IMAGE_OR_PLACEMENT_BY_NUMBER:
    case IMGNEKO_DELETE_PLACEMENTS_AT_CURSOR:
    case IMGNEKO_DELETE_ANIMATION_FRAMES:
    case IMGNEKO_DELETE_PLACEMENTS_AT_POSITION:
    case IMGNEKO_DELETE_PLACEMENTS_AT_POSITION_AND_Z_INDEX:
    case IMGNEKO_DELETE_IMAGES_BY_ID_RANGE:
    case IMGNEKO_DELETE_PLACEMENTS_AT_COLUMN:
    case IMGNEKO_DELETE_PLACEMENTS_AT_ROW:
    case IMGNEKO_DELETE_PLACEMENTS_AT_Z_INDEX:
        return true;
    default:
        return false;
    }
}

static bool quietness_is_known(uint32_t quietness) {
    return quietness <= IMGNEKO_QUIETNESS_SILENT;
}

static bool continuation_is_known(uint32_t more_data) { return more_data <= 1; }

static bool image_format_is_known(uint32_t format) {
    return format == IMGNEKO_IMAGE_FORMAT_DEFAULT ||
           format == IMGNEKO_IMAGE_FORMAT_RGB ||
           format == IMGNEKO_IMAGE_FORMAT_RGBA ||
           format == IMGNEKO_IMAGE_FORMAT_PNG;
}

static bool compression_is_known(char compression) {
    return compression == IMGNEKO_PAYLOAD_COMPRESSION_NONE ||
           compression == IMGNEKO_PAYLOAD_COMPRESSION_ZLIB;
}

static bool image_usage_hints_are_known(uint32_t usage_hints) {
    return (usage_hints & ~IMGNEKO_IMAGE_USAGE_TRANSIENT) == 0;
}

static bool bool_is_known(uint32_t value) { return value <= 1; }

static bool composition_mode_is_known(uint32_t composition) {
    return composition == IMGNEKO_COMPOSITION_ALPHA_BLEND ||
           composition == IMGNEKO_COMPOSITION_OVERWRITE;
}

static bool animation_state_is_known(uint32_t state) {
    return state == IMGNEKO_ANIMATION_STATE_UNCHANGED ||
           state == IMGNEKO_ANIMATION_STATE_STOPPED ||
           state == IMGNEKO_ANIMATION_STATE_LOADING ||
           state == IMGNEKO_ANIMATION_STATE_RUNNING;
}

// Protocol character values cannot contain control-data separators.
static bool protocol_char_is_representable(char value) {
    return value != '\0' && value != ',' && value != ';';
}

// The always true predicate for fields without a restricted value set.
#define ALWAYS_KNOWN(value_) true

// A field scope disambiguates protocol keys whose meanings depend on the
// command kind.
typedef enum CommandFieldScope {
    SCOPE_COMMON,
    SCOPE_TRANSMISSION,
    SCOPE_PLACEMENT,
    SCOPE_FRAME,
    SCOPE_ANIMATION,
    SCOPE_COMPOSE,
    SCOPE_DELETE,
} CommandFieldScope;

// Each X-macro entry supplies a protocol key, storage type, structure member,
// diagnostic description, and known-value predicate. The lists group fields
// by semantic scope and define canonical serialization order. The `a`, `t`,
// and `d` fields are also listed (but often handled separately).

#define COMMAND_COMMON_FIELDS(X)                                               \
    X('a', char, kind, "command kind", command_kind_is_known)                  \
    X('i', u32, image_id, "image ID", ALWAYS_KNOWN)                            \
    X('I', u32, image_number, "image number", ALWAYS_KNOWN)                    \
    X('q', u32, quietness, "quietness value", quietness_is_known)

#define COMMAND_TRANSMISSION_FIELDS(X)                                         \
    X('t', char, medium, "transmission medium", transmission_medium_is_known)  \
    X('S', u32, data_size, "data size", ALWAYS_KNOWN)                          \
    X('O', u32, data_offset, "data offset", ALWAYS_KNOWN)                      \
    X('m', u32, more_data, "continuation value", continuation_is_known)        \
    X('f', u32, format, "image format", image_format_is_known)                 \
    X('o', char, compression, "payload compression", compression_is_known)     \
    X('s', u32, pixel_width, "pixel width", ALWAYS_KNOWN)                      \
    X('v', u32, pixel_height, "pixel height", ALWAYS_KNOWN)                    \
    X('N', u32, usage_hints, "image usage hints", image_usage_hints_are_known)

#define COMMAND_PLACEMENT_FIELDS(X)                                            \
    X('p', u32, placement_id, "placement ID", ALWAYS_KNOWN)                    \
    X('U', u32, virtual_placement, "virtual placement value", bool_is_known)   \
    X('c', u32, cols, "placement columns", ALWAYS_KNOWN)                       \
    X('r', u32, rows, "placement rows", ALWAYS_KNOWN)                          \
    X('C', u32, do_not_move_cursor, "cursor movement value", bool_is_known)    \
    X('x', i32, src_x, "placement source x", ALWAYS_KNOWN)                     \
    X('y', i32, src_y, "placement source y", ALWAYS_KNOWN)                     \
    X('w', u32, src_w, "placement source width", ALWAYS_KNOWN)                 \
    X('h', u32, src_h, "placement source height", ALWAYS_KNOWN)                \
    X('X', i32, cell_x_offset, "placement cell x offset", ALWAYS_KNOWN)        \
    X('Y', i32, cell_y_offset, "placement cell y offset", ALWAYS_KNOWN)        \
    X('z', i32, z_index, "placement z-index", ALWAYS_KNOWN)                    \
    X('P', u32, parent_image_id, "parent image ID", ALWAYS_KNOWN)              \
    X('Q', u32, parent_placement_id, "parent placement ID", ALWAYS_KNOWN)      \
    X('H', i32, parent_x_offset, "parent x offset", ALWAYS_KNOWN)              \
    X('V', i32, parent_y_offset, "parent y offset", ALWAYS_KNOWN)

#define COMMAND_FRAME_FIELDS(X)                                                \
    X('x', u32, data_x, "frame data x", ALWAYS_KNOWN)                          \
    X('y', u32, data_y, "frame data y", ALWAYS_KNOWN)                          \
    X('c', u32, base_frame_number, "base frame number", ALWAYS_KNOWN)          \
    X('r', u32, frame_number, "frame number", ALWAYS_KNOWN)                    \
    X('z', i32, gap_ms, "frame gap", ALWAYS_KNOWN)                             \
    X('X', u32, composition, "composition mode", composition_mode_is_known)    \
    X('Y', u32, background_rgba, "frame background", ALWAYS_KNOWN)

#define COMMAND_ANIMATION_FIELDS(X)                                            \
    X('s', u32, state, "animation state", animation_state_is_known)            \
    X('r', u32, frame_number, "animation frame number", ALWAYS_KNOWN)          \
    X('z', i32, gap_ms, "animation gap", ALWAYS_KNOWN)                         \
    X('c', u32, current_frame_number, "current frame number", ALWAYS_KNOWN)    \
    X('v', u32, loop_count, "animation loop count", ALWAYS_KNOWN)

#define COMMAND_COMPOSE_FIELDS(X)                                              \
    X('r', u32, src_frame_number, "source frame number", ALWAYS_KNOWN)         \
    X('c', u32, dst_frame_number, "destination frame number", ALWAYS_KNOWN)    \
    X('X', u32, src_x, "source x", ALWAYS_KNOWN)                               \
    X('Y', u32, src_y, "source y", ALWAYS_KNOWN)                               \
    X('x', u32, dst_x, "destination x", ALWAYS_KNOWN)                          \
    X('y', u32, dst_y, "destination y", ALWAYS_KNOWN)                          \
    X('w', u32, width, "composition width", ALWAYS_KNOWN)                      \
    X('h', u32, height, "composition height", ALWAYS_KNOWN)                    \
    X('C', u32, composition, "composition mode", composition_mode_is_known)

#define COMMAND_DELETE_TARGET_FIELD(X)                                         \
    X('d', char, target, "delete target", delete_target_is_known)

#define COMMAND_DELETE_PREFIX_FIELDS(X)                                        \
    COMMAND_DELETE_TARGET_FIELD(X)                                             \
    X('p', u32, placement_id, "placement ID", ALWAYS_KNOWN)

#define COMMAND_DELETE_POSITION_X_FIELD(X)                                     \
    X('x', i32, x, "delete position coordinate", ALWAYS_KNOWN)

#define COMMAND_DELETE_POSITION_Y_FIELD(X)                                     \
    X('y', i32, y, "delete position coordinate", ALWAYS_KNOWN)

#define COMMAND_DELETE_POSITION_FIELDS(X)                                      \
    COMMAND_DELETE_POSITION_X_FIELD(X)                                         \
    COMMAND_DELETE_POSITION_Y_FIELD(X)

#define COMMAND_DELETE_Z_INDEX_FIELD(X)                                        \
    X('z', i32, z_index, "delete z-index", ALWAYS_KNOWN)

#define COMMAND_DELETE_DATA_FIELDS(X)                                          \
    X('p', u32, placement_id, "placement ID", ALWAYS_KNOWN)                    \
    COMMAND_DELETE_POSITION_FIELDS(X)                                          \
    COMMAND_DELETE_Z_INDEX_FIELD(X)

#define COMMAND_DELETE_FIELDS(X)                                               \
    COMMAND_DELETE_TARGET_FIELD(X)                                             \
    COMMAND_DELETE_DATA_FIELDS(X)

#define COMMAND_DELETE_RANGE_FIELDS(X)                                         \
    X('x', u32, first_image_id, "delete image ID range", ALWAYS_KNOWN)         \
    X('y', u32, last_image_id, "delete image ID range", ALWAYS_KNOWN)

// Return the human-readable name associated with a protocol key in a scope.
static const char *command_field_description(CommandFieldScope scope,
                                             char key) {
#define RETURN_DESCRIPTION(key_, type_, member_, description_, predicate_)     \
    case key_:                                                                 \
        return description_;

    // IMGNEKO_UNCOVERED_OK_START: Every generated key case returns before its
    // following break.
    switch (scope) {
    case SCOPE_COMMON:
        switch (key) { COMMAND_COMMON_FIELDS(RETURN_DESCRIPTION) }
        break;
    case SCOPE_TRANSMISSION:
        switch (key) { COMMAND_TRANSMISSION_FIELDS(RETURN_DESCRIPTION) }
        break;
    case SCOPE_PLACEMENT:
        switch (key) { COMMAND_PLACEMENT_FIELDS(RETURN_DESCRIPTION) }
        break;
    case SCOPE_FRAME:
        switch (key) { COMMAND_FRAME_FIELDS(RETURN_DESCRIPTION) }
        break;
    case SCOPE_ANIMATION:
        switch (key) { COMMAND_ANIMATION_FIELDS(RETURN_DESCRIPTION) }
        break;
    case SCOPE_COMPOSE:
        switch (key) { COMMAND_COMPOSE_FIELDS(RETURN_DESCRIPTION) }
        break;
    case SCOPE_DELETE:
        switch (key) { COMMAND_DELETE_FIELDS(RETURN_DESCRIPTION) }
        break;
    }
    // IMGNEKO_UNCOVERED_OK_END
#undef RETURN_DESCRIPTION

    // IMGNEKO_UNCOVERED_OK: Callers pass known scope/key combinations.
    return "command field";
}

static ImgnekoCommandError invalid_value(ImgnekoErrorDetail *error_out,
                                         const char *format, ...) {
    va_list args;
    va_start(args, format);
    append_detail_v(error_out, format, args);
    va_end(args);
    return IMGNEKO_COMMAND_INVALID_FIELD;
}

// Format `value` as a printable character or a hexadecimal byte. `out`
// receives a null-terminated string of at most four characters.
static void format_diagnostic_char(char value, char out[5]) {
    unsigned char byte = (unsigned char)value;
    if (byte >= ' ' && byte <= '~')
        snprintf(out, 5, "%c", byte);
    else
        snprintf(out, 5, "0x%02x", byte);
}

// Report a character value that cannot be represented in control data.
static ImgnekoCommandError unrepresentable_char(ImgnekoErrorDetail *error_out,
                                                CommandFieldScope scope,
                                                char key, char value) {
    const char *description = command_field_description(scope, key);
    char formatted_value[5];
    format_diagnostic_char(value, formatted_value);
    return invalid_value(error_out, "%s is not representable (%c=%s)",
                         description, key, formatted_value);
}

// Report a representable but unknown character value.
static ImgnekoCommandError unknown_char(ImgnekoErrorDetail *error_out,
                                        CommandFieldScope scope, char key,
                                        char value) {
    const char *description = command_field_description(scope, key);
    char formatted_value[5];
    format_diagnostic_char(value, formatted_value);
    return invalid_value(error_out, "unknown %s (%c=%s)", description, key,
                         formatted_value);
}

// Report a representable but unknown integer value.
static ImgnekoCommandError unknown_integer(ImgnekoErrorDetail *error_out,
                                           CommandFieldScope scope, char key,
                                           int64_t value) {
    const char *description = command_field_description(scope, key);
    return invalid_value(error_out, "unknown %s (%c=%" PRId64 ")", description,
                         key, value);
}

// Check fields that must have a protocol representation under every
// validation policy. Semantic extension values remain accepted here.
static ImgnekoCommandError
validate_representability(const ImgnekoCommand *command,
                          ImgnekoErrorDetail *error_out) {
    if (command->kind == IMGNEKO_COMMAND_UNSET)
        return invalid_value(error_out, "command kind is unset (a=0)");
    if (!protocol_char_is_representable(command->kind))
        return unrepresentable_char(error_out, SCOPE_COMMON, 'a',
                                    command->kind);

    const ImgnekoTransmission *transmission =
        imgneko_command_get_transmission_const(command);
    if (transmission != NULL) {
        if (transmission->medium == IMGNEKO_TRANSMISSION_MEDIUM_UNSET)
            return invalid_value(error_out,
                                 "transmission medium is unset (t=0)");
        if (!protocol_char_is_representable(transmission->medium))
            return unrepresentable_char(error_out, SCOPE_TRANSMISSION, 't',
                                        transmission->medium);
        if (transmission->compression != IMGNEKO_PAYLOAD_COMPRESSION_NONE &&
            !protocol_char_is_representable(transmission->compression))
            return unrepresentable_char(error_out, SCOPE_TRANSMISSION, 'o',
                                        transmission->compression);
    }

    if (command->kind == IMGNEKO_COMMAND_DELETE) {
        const ImgnekoDeleteCommand *delete_cmd = &command->data.delete_cmd;
        if (delete_cmd->target == IMGNEKO_DELETE_UNSET)
            return invalid_value(error_out, "delete target is unset (d=0)");
        if (!protocol_char_is_representable(delete_cmd->target))
            return unrepresentable_char(error_out, SCOPE_DELETE, 'd',
                                        delete_cmd->target);
        if (delete_cmd->delete_data &&
            (delete_cmd->target < 'a' || delete_cmd->target > 'z'))
            return invalid_value(
                error_out,
                "delete_data requires a lowercase ASCII target (d=0x%02x)",
                (unsigned char)delete_cmd->target);

        // The x/y keys use separate storage for range deletion. Reject field
        // combinations that no header could parse back into the same command.
        if (delete_cmd->target == IMGNEKO_DELETE_IMAGES_BY_ID_RANGE &&
            (delete_cmd->x != 0 || delete_cmd->y != 0))
            return invalid_value(
                error_out,
                "delete position coordinates cannot be represented for "
                "delete-range target (d=r)");
        if (delete_cmd->target != IMGNEKO_DELETE_IMAGES_BY_ID_RANGE &&
            (delete_cmd->first_image_id != 0 || delete_cmd->last_image_id != 0))
            return invalid_value(
                error_out,
                "image ID range endpoints require delete-range target (d=r)");
    }

    return IMGNEKO_COMMAND_OK;
}

// Expand every field into a predicate check and a typed diagnostic. We assume
// there is a `field_owner` pointer and a `field_scope` variable in scope.
#define UNKNOWN_VALUE_char unknown_char
#define UNKNOWN_VALUE_u32 unknown_integer
#define UNKNOWN_VALUE_i32 unknown_integer // IMGNEKO_UNCOVERED_OK
#define VALIDATE_FIELD(key_, type_, member_, description_, predicate_)         \
    do {                                                                       \
        if (!predicate_(field_owner->member_))                                 \
            return UNKNOWN_VALUE_##type_(error_out, field_scope, key_,         \
                                         field_owner->member_);                \
    } while (0);

// Validate fields whose accepted values are enumerated by the current
// protocol.
static ImgnekoCommandError validate_values(const ImgnekoCommand *command,
                                           ImgnekoErrorDetail *error_out) {
    {
        const ImgnekoCommand *field_owner = command;
        const CommandFieldScope field_scope = SCOPE_COMMON;
        COMMAND_COMMON_FIELDS(VALIDATE_FIELD)
    }

    const ImgnekoTransmission *transmission =
        imgneko_command_get_transmission_const(command);
    if (transmission != NULL) {
        const ImgnekoTransmission *field_owner = transmission;
        const CommandFieldScope field_scope = SCOPE_TRANSMISSION;
        COMMAND_TRANSMISSION_FIELDS(VALIDATE_FIELD)
    }

    const ImgnekoPlacement *placement =
        imgneko_command_get_placement_const(command);
    if (placement != NULL) {
        const ImgnekoPlacement *field_owner = placement;
        const CommandFieldScope field_scope = SCOPE_PLACEMENT;
        COMMAND_PLACEMENT_FIELDS(VALIDATE_FIELD)

        if (placement->src_x < 0 || placement->src_y < 0)
            return invalid_value(
                error_out,
                "placement source coordinates (x=... and y=...) must be "
                "nonnegative");
    }

    if (command->kind == IMGNEKO_COMMAND_FRAME) {
        const ImgnekoFrame *field_owner = &command->data.frame.frame;
        const CommandFieldScope field_scope = SCOPE_FRAME;
        COMMAND_FRAME_FIELDS(VALIDATE_FIELD)
    }
    if (command->kind == IMGNEKO_COMMAND_ANIMATION) {
        const ImgnekoAnimationCommand *field_owner = &command->data.animation;
        const CommandFieldScope field_scope = SCOPE_ANIMATION;
        COMMAND_ANIMATION_FIELDS(VALIDATE_FIELD)
    }
    if (command->kind == IMGNEKO_COMMAND_COMPOSE) {
        const ImgnekoComposeCommand *field_owner = &command->data.compose;
        const CommandFieldScope field_scope = SCOPE_COMPOSE;
        COMMAND_COMPOSE_FIELDS(VALIDATE_FIELD)
    }
    if (command->kind == IMGNEKO_COMMAND_DELETE) {
        const ImgnekoDeleteCommand *field_owner = &command->data.delete_cmd;
        const CommandFieldScope field_scope = SCOPE_DELETE;
        COMMAND_DELETE_FIELDS(VALIDATE_FIELD)

        if ((field_owner->target == IMGNEKO_DELETE_PLACEMENTS_AT_POSITION ||
             field_owner->target ==
                 IMGNEKO_DELETE_PLACEMENTS_AT_POSITION_AND_Z_INDEX ||
             field_owner->target == IMGNEKO_DELETE_PLACEMENTS_AT_COLUMN ||
             field_owner->target == IMGNEKO_DELETE_PLACEMENTS_AT_ROW) &&
            (field_owner->x < 0 || field_owner->y < 0))
            return invalid_value(
                error_out,
                "delete position coordinates (x=... and y=...) must be "
                "positive");
    }

    return IMGNEKO_COMMAND_OK;
}
#undef VALIDATE_FIELD
#undef UNKNOWN_VALUE_i32
#undef UNKNOWN_VALUE_u32
#undef UNKNOWN_VALUE_char

static bool command_has_identifier(const ImgnekoCommand *command) {
    return command->image_id != 0 || command->image_number != 0;
}

// Validate relationships shared by every data-bearing command.
static ImgnekoCommandError
validate_transmission_relationships(const ImgnekoTransmission *transmission,
                                    ImgnekoErrorDetail *error_out) {
    bool known_medium = transmission_medium_is_known(transmission->medium);

    if (known_medium && transmission->data_offset != 0 &&
        transmission->medium != IMGNEKO_TRANSMISSION_MEDIUM_FILE &&
        transmission->medium != IMGNEKO_TRANSMISSION_MEDIUM_TEMP_FILE &&
        transmission->medium != IMGNEKO_TRANSMISSION_MEDIUM_SHARED_MEMORY)
        return invalid_value(
            error_out,
            "data offset (O=...) requires file (t=f), temporary-file (t=t), "
            "or shared-memory (t=s) transmission");
    if (known_medium && transmission->more_data != 0 &&
        transmission->medium != IMGNEKO_TRANSMISSION_MEDIUM_DIRECT)
        return invalid_value(
            error_out,
            "continuation (m=...) requires direct transmission (t=d)");
    if ((transmission->format == IMGNEKO_IMAGE_FORMAT_RGB ||
         transmission->format == IMGNEKO_IMAGE_FORMAT_RGBA) &&
        (transmission->pixel_width == 0 || transmission->pixel_height == 0))
        return invalid_value(
            error_out,
            "raw image format (f=24 or f=32) requires pixel width (s=...) and "
            "height (v=...)");
    if (transmission->format == IMGNEKO_IMAGE_FORMAT_PNG &&
        transmission->compression == IMGNEKO_PAYLOAD_COMPRESSION_ZLIB &&
        transmission->data_size == 0)
        return invalid_value(
            error_out,
            "compressed PNG (f=100 and o=z) requires data size (S=...)");

    return IMGNEKO_COMMAND_OK;
}

static ImgnekoCommandError
validate_placement_relationships(const ImgnekoPlacement *placement,
                                 ImgnekoErrorDetail *error_out) {
    if (placement->parent_image_id == 0 && placement->parent_placement_id != 0)
        return invalid_value(
            error_out,
            "parent placement ID (Q=...) requires a parent image ID (P=...)");
    if (placement->parent_image_id == 0 &&
        (placement->parent_x_offset != 0 || placement->parent_y_offset != 0))
        return invalid_value(
            error_out,
            "parent offsets (H=... or V=...) require a parent image ID "
            "(P=...)");
    if (placement->virtual_placement != 0 && placement->parent_image_id != 0)
        return invalid_value(
            error_out,
            "virtual placement (U=...) cannot have a parent (P=...)");

    return IMGNEKO_COMMAND_OK;
}

static ImgnekoCommandError
validate_delete_relationships(const ImgnekoCommand *command,
                              ImgnekoErrorDetail *error_out) {
    const ImgnekoDeleteCommand *delete_cmd = &command->data.delete_cmd;

    // Check required fields first so diagnostics describe the target's primary
    // requirement before any unrelated selector supplied alongside it.
    switch (delete_cmd->target) {
    case IMGNEKO_DELETE_VISIBLE_PLACEMENTS:
    case IMGNEKO_DELETE_PLACEMENTS_AT_CURSOR:
        break;
    case IMGNEKO_DELETE_IMAGE_OR_PLACEMENT_BY_ID:
        if (command->image_id == 0)
            return invalid_value(
                error_out,
                "delete-by-ID target (d=i) requires an image ID (i=...)");
        break;
    case IMGNEKO_DELETE_IMAGE_OR_PLACEMENT_BY_NUMBER:
        if (command->image_number == 0)
            return invalid_value(
                error_out,
                "delete-by-number target (d=n) requires an image number "
                "(I=...)");
        break;
    case IMGNEKO_DELETE_ANIMATION_FRAMES:
        if (!command_has_identifier(command))
            return invalid_value(
                error_out,
                "animation-frame deletion (d=f) requires an image identifier "
                "(i=...) or number (I=...)");
        break;
    case IMGNEKO_DELETE_PLACEMENTS_AT_POSITION:
        if (delete_cmd->x <= 0 || delete_cmd->y <= 0)
            return invalid_value(
                error_out,
                "delete position target (d=p or d=q) requires positive x "
                "(x=...) and y (y=...)");
        break;
    case IMGNEKO_DELETE_PLACEMENTS_AT_POSITION_AND_Z_INDEX:
        if (delete_cmd->x <= 0 || delete_cmd->y <= 0)
            return invalid_value(
                error_out,
                "delete position target (d=p or d=q) requires positive x "
                "(x=...) and y (y=...)");
        break;
    case IMGNEKO_DELETE_IMAGES_BY_ID_RANGE:
        if (delete_cmd->first_image_id == 0 || delete_cmd->last_image_id == 0)
            return invalid_value(
                error_out,
                "delete image ID range target (d=r) requires positive "
                "endpoints (x=... and y=...)");
        if (delete_cmd->first_image_id > delete_cmd->last_image_id)
            return invalid_value(
                error_out, "delete image ID range is reversed (x=... > y=...)");
        break;
    case IMGNEKO_DELETE_PLACEMENTS_AT_COLUMN:
        if (delete_cmd->x <= 0)
            return invalid_value(
                error_out,
                "delete-column target (d=x) requires a positive column "
                "(x=...)");
        break;
    case IMGNEKO_DELETE_PLACEMENTS_AT_ROW:
        if (delete_cmd->y <= 0)
            return invalid_value(
                error_out,
                "delete-row target (d=y) requires a positive row (y=...)");
        break;
    case IMGNEKO_DELETE_PLACEMENTS_AT_Z_INDEX:
        break;
    default:
        // Representable selectors may have extension-defined meanings.
        return IMGNEKO_COMMAND_OK;
    }

    // Reject fields that the known target does not use. Unknown targets return
    // above so extensions may give these generic fields their own meanings.
    if (command->image_id != 0 &&
        delete_cmd->target != IMGNEKO_DELETE_IMAGE_OR_PLACEMENT_BY_ID &&
        delete_cmd->target != IMGNEKO_DELETE_ANIMATION_FRAMES)
        return invalid_value(
            error_out, "image ID (i=...) is not used by delete target (d=%c)",
            delete_cmd->target);
    if (command->image_number != 0 &&
        delete_cmd->target != IMGNEKO_DELETE_IMAGE_OR_PLACEMENT_BY_NUMBER &&
        delete_cmd->target != IMGNEKO_DELETE_ANIMATION_FRAMES)
        return invalid_value(
            error_out,
            "image number (I=...) is not used by delete target (d=%c)",
            delete_cmd->target);
    if (delete_cmd->placement_id != 0 &&
        delete_cmd->target != IMGNEKO_DELETE_IMAGE_OR_PLACEMENT_BY_ID &&
        delete_cmd->target != IMGNEKO_DELETE_IMAGE_OR_PLACEMENT_BY_NUMBER)
        return invalid_value(
            error_out, "placement ID (p=...) requires delete-by-ID (d=i) or "
                       "delete-by-number (d=n)");

    if (delete_cmd->x != 0 &&
        delete_cmd->target != IMGNEKO_DELETE_PLACEMENTS_AT_POSITION &&
        delete_cmd->target !=
            IMGNEKO_DELETE_PLACEMENTS_AT_POSITION_AND_Z_INDEX &&
        delete_cmd->target != IMGNEKO_DELETE_PLACEMENTS_AT_COLUMN)
        return invalid_value(
            error_out,
            "x coordinate (x=...) is not used by delete target (d=%c)",
            delete_cmd->target);
    if (delete_cmd->y != 0 &&
        delete_cmd->target != IMGNEKO_DELETE_PLACEMENTS_AT_POSITION &&
        delete_cmd->target !=
            IMGNEKO_DELETE_PLACEMENTS_AT_POSITION_AND_Z_INDEX &&
        delete_cmd->target != IMGNEKO_DELETE_PLACEMENTS_AT_ROW)
        return invalid_value(
            error_out,
            "y coordinate (y=...) is not used by delete target (d=%c)",
            delete_cmd->target);
    if (delete_cmd->z_index != 0 &&
        delete_cmd->target !=
            IMGNEKO_DELETE_PLACEMENTS_AT_POSITION_AND_Z_INDEX &&
        delete_cmd->target != IMGNEKO_DELETE_PLACEMENTS_AT_Z_INDEX)
        return invalid_value(
            error_out, "z-index (z=...) is not used by delete target (d=%c)",
            delete_cmd->target);

    return IMGNEKO_COMMAND_OK;
}

static ImgnekoCommandError
validate_relationships(const ImgnekoCommand *command,
                       ImgnekoErrorDetail *error_out) {
    if (command->image_id != 0 && command->image_number != 0)
        return invalid_value(
            error_out,
            "image ID (i=...) and image number (I=...) are both set");

    const ImgnekoTransmission *transmission =
        imgneko_command_get_transmission_const(command);
    if (transmission != NULL) {
        ImgnekoCommandError error =
            validate_transmission_relationships(transmission, error_out);
        if (error != IMGNEKO_COMMAND_OK)
            return error;
    }

    const ImgnekoPlacement *placement =
        imgneko_command_get_placement_const(command);
    if (placement != NULL) {
        ImgnekoCommandError error =
            validate_placement_relationships(placement, error_out);
        if (error != IMGNEKO_COMMAND_OK)
            return error;
    }

    switch (command->kind) {
    case IMGNEKO_COMMAND_PUT:
        if (!command_has_identifier(command))
            return invalid_value(
                error_out,
                "put command (a=p) requires an image identifier (i=...) or "
                "number (I=...)");
        break;
    case IMGNEKO_COMMAND_QUERY:
        if (command->image_id == 0)
            return invalid_value(
                error_out, "query command (a=q) requires an image ID (i=...)");
        break;
    case IMGNEKO_COMMAND_FRAME: {
        const ImgnekoFrame *frame = &command->data.frame.frame;
        if (!command_has_identifier(command))
            return invalid_value(
                error_out,
                "frame command (a=f) requires an image identifier (i=...) or "
                "number (I=...)");
        if (frame->base_frame_number != 0 && frame->background_rgba != 0)
            return invalid_value(
                error_out,
                "base frame (c=...) and background (Y=...) cannot be combined");
        if (frame->frame_number != 0 && frame->base_frame_number != 0)
            return invalid_value(
                error_out,
                "existing frame (r=...) cannot use a base frame (c=...)");
        if (frame->frame_number != 0 && frame->background_rgba != 0)
            return invalid_value(
                error_out,
                "existing frame (r=...) cannot use a background color "
                "(Y=...)");
        break;
    }
    case IMGNEKO_COMMAND_ANIMATION:
        if (!command_has_identifier(command))
            return invalid_value(
                error_out,
                "animation command (a=a) requires an image identifier "
                "(i=...) or number (I=...)");
        if (command->data.animation.gap_ms != 0 &&
            command->data.animation.frame_number == 0)
            return invalid_value(
                error_out,
                "animation gap (z=...) requires a frame number (r=...)");
        break;
    case IMGNEKO_COMMAND_COMPOSE:
        if (!command_has_identifier(command))
            return invalid_value(
                error_out,
                "compose command (a=c) requires an image identifier (i=...) "
                "or number (I=...)");
        if (command->data.compose.src_frame_number == 0 ||
            command->data.compose.dst_frame_number == 0)
            return invalid_value(
                error_out, "compose command (a=c) requires source (r=...) and "
                           "destination (c=...) frames");
        break;
    case IMGNEKO_COMMAND_DELETE:
        return validate_delete_relationships(command, error_out);
    default:
        break;
    }

    return IMGNEKO_COMMAND_OK;
}

ImgnekoCommandError
imgneko_command_validate(const ImgnekoCommand *command,
                         ImgnekoCommandValidationFlags flags,
                         ImgnekoErrorDetail *error_out) {
    if (!detail_is_valid(error_out))
        return IMGNEKO_COMMAND_INVALID_ARGUMENT;
    clear_detail(error_out);
    if (command == NULL) {
        append_detail(error_out, "command must not be NULL");
        return IMGNEKO_COMMAND_INVALID_ARGUMENT;
    }

    ImgnekoCommandError error = validate_representability(command, error_out);
    if (error != IMGNEKO_COMMAND_OK)
        return error;
    if (!(flags & IMGNEKO_COMMAND_VALIDATION_ALLOW_UNKNOWN_VALUES)) {
        error = validate_values(command, error_out);
        if (error != IMGNEKO_COMMAND_OK)
            return error;
    }
    if (!(flags & IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS))
        return validate_relationships(command, error_out);

    return IMGNEKO_COMMAND_OK;
}

//===----------------------------------------------------------------------===//
// Serialization
//===----------------------------------------------------------------------===//

// Copy the bytes that fit and increase `len` by the full input length.
static void header_append(HeaderBuilder *builder, const char *data,
                          size_t data_len) {
    size_t write_len = 0;
    if (builder->len < builder->out_cap) {
        write_len = builder->out_cap - builder->len;
        if (write_len > data_len)
            write_len = data_len;
    }
    if (write_len != 0)
        memcpy(builder->out + builder->len, data, write_len);
    builder->len += data_len;
}

// Append a key/value pair while preserving the complete required size when the
// output buffer is too small.
static void header_add_raw(HeaderBuilder *builder, char key, const char *value,
                           size_t value_len) {
    if (!builder->first)
        header_append(builder, ",", 1);
    header_append(builder, &key, 1);
    header_append(builder, "=", 1);
    header_append(builder, value, value_len);
    builder->first = false;
}

static void header_add_char(HeaderBuilder *builder, char key, char value) {
    header_add_raw(builder, key, &value, 1);
}

static void header_add_u32(HeaderBuilder *builder, char key, uint32_t value) {
    char number[sizeof("4294967295")];
    int len = snprintf(number, sizeof(number), "%" PRIu32, value);
    header_add_raw(builder, key, number, (size_t)len);
}

static void header_add_i32(HeaderBuilder *builder, char key, int32_t value) {
    char number[sizeof("-2147483648")];
    int len = snprintf(number, sizeof(number), "%" PRId32, value);
    header_add_raw(builder, key, number, (size_t)len);
}

static void header_add_u32_nonzero(HeaderBuilder *builder, char key,
                                   uint32_t value) {
    if (value != 0)
        header_add_u32(builder, key, value);
}

static void header_add_i32_nonzero(HeaderBuilder *builder, char key,
                                   int32_t value) {
    if (value != 0)
        header_add_i32(builder, key, value);
}

static void header_add_char_nonzero(HeaderBuilder *builder, char key,
                                    char value) {
    if (value != '\0')
        header_add_char(builder, key, value);
}

// Expand a field list into typed serialization calls. Discriminator keys are
// handled explicitly. Each serializer declares an appropriate `field_owner`.
// IMGNEKO_UNCOVERED_OK[2 lines]
#define SERIALIZE_FIELD(key_, type_, member_, description_, predicate_)        \
    if (key_ != 'a' && key_ != 't' && key_ != 'd')                             \
        header_add_##type_##_nonzero(builder, key_, field_owner->member_);

// Some target-selected fields are emitted explicitly even when their value is
// zero.
#define SERIALIZE_REQUIRED_FIELD(key_, type_, member_, description_,           \
                                 predicate_)                                   \
    header_add_##type_(builder, key_, field_owner->member_);

static void serialize_common(HeaderBuilder *builder,
                             const ImgnekoCommand *command) {
    if (!(command->kind == IMGNEKO_COMMAND_TRANSMIT && command->kind_implicit))
        header_add_char(builder, 'a', command->kind);
    const ImgnekoCommand *field_owner = command;
    COMMAND_COMMON_FIELDS(SERIALIZE_FIELD)
}

static void serialize_transmission(HeaderBuilder *builder,
                                   const ImgnekoTransmission *transmission) {
    if (!(transmission->medium == IMGNEKO_TRANSMISSION_MEDIUM_DIRECT &&
          transmission->medium_implicit))
        header_add_char(builder, 't', transmission->medium);
    const ImgnekoTransmission *field_owner = transmission;
    COMMAND_TRANSMISSION_FIELDS(SERIALIZE_FIELD)
}

static void serialize_placement(HeaderBuilder *builder,
                                const ImgnekoPlacement *placement) {
    const ImgnekoPlacement *field_owner = placement;
    COMMAND_PLACEMENT_FIELDS(SERIALIZE_FIELD)
}

static void serialize_delete(HeaderBuilder *builder,
                             const ImgnekoDeleteCommand *delete_cmd) {
    char target = delete_cmd->target;
    // imgneko_command_header_to_buffer() rejects non-lowercase targets before
    // this ASCII case conversion when `delete_data` is set.
    if (delete_cmd->delete_data)
        target = (char)(target - 'a' + 'A');
    if (!(delete_cmd->target == IMGNEKO_DELETE_VISIBLE_PLACEMENTS &&
          delete_cmd->target_implicit && !delete_cmd->delete_data))
        header_add_char(builder, 'd', target);
    const ImgnekoDeleteCommand *field_owner = delete_cmd;
    COMMAND_DELETE_PREFIX_FIELDS(SERIALIZE_FIELD)

    // Preserve every parsed field, including fields that strict relationship
    // validation rejects. Only x/y storage depends on the delete target.
    if (delete_cmd->target == IMGNEKO_DELETE_IMAGES_BY_ID_RANGE) {
        COMMAND_DELETE_RANGE_FIELDS(SERIALIZE_FIELD)
    } else {
        COMMAND_DELETE_POSITION_FIELDS(SERIALIZE_FIELD)
    }

    switch (delete_cmd->target) {
    case IMGNEKO_DELETE_PLACEMENTS_AT_POSITION_AND_Z_INDEX:
    case IMGNEKO_DELETE_PLACEMENTS_AT_Z_INDEX:
        COMMAND_DELETE_Z_INDEX_FIELD(SERIALIZE_REQUIRED_FIELD)
        break;
    default:
        COMMAND_DELETE_Z_INDEX_FIELD(SERIALIZE_FIELD)
        break;
    }
}

static void serialize_kind_fields(HeaderBuilder *builder,
                                  const ImgnekoCommand *command) {
    switch (command->kind) {
    case IMGNEKO_COMMAND_TRANSMIT:
        serialize_transmission(builder, &command->data.transmit.transmission);
        break;
    case IMGNEKO_COMMAND_TRANSMIT_AND_PUT:
        serialize_transmission(builder,
                               &command->data.transmit_and_put.transmission);
        serialize_placement(builder, &command->data.transmit_and_put.placement);
        break;
    case IMGNEKO_COMMAND_QUERY:
        serialize_transmission(builder, &command->data.query.transmission);
        break;
    case IMGNEKO_COMMAND_FRAME: {
        serialize_transmission(builder, &command->data.frame.transmission);
        const ImgnekoFrame *field_owner = &command->data.frame.frame;
        COMMAND_FRAME_FIELDS(SERIALIZE_FIELD)
        break;
    }
    case IMGNEKO_COMMAND_PUT:
        serialize_placement(builder, &command->data.put.placement);
        break;
    case IMGNEKO_COMMAND_ANIMATION: {
        const ImgnekoAnimationCommand *field_owner = &command->data.animation;
        COMMAND_ANIMATION_FIELDS(SERIALIZE_FIELD)
        break;
    }
    case IMGNEKO_COMMAND_COMPOSE: {
        const ImgnekoComposeCommand *field_owner = &command->data.compose;
        COMMAND_COMPOSE_FIELDS(SERIALIZE_FIELD)
        break;
    }
    case IMGNEKO_COMMAND_DELETE:
        serialize_delete(builder, &command->data.delete_cmd);
        break;
    default:
        break;
    }
}
#undef SERIALIZE_REQUIRED_FIELD
#undef SERIALIZE_FIELD

ImgnekoCommandError
imgneko_command_header_to_buffer(const ImgnekoCommand *command, char *out,
                                 size_t out_cap, size_t *len_out,
                                 ImgnekoErrorDetail *error_out) {
    if (len_out != NULL)
        *len_out = 0;
    if (!detail_is_valid(error_out))
        return IMGNEKO_COMMAND_INVALID_ARGUMENT;
    clear_detail(error_out);
    if (command == NULL || len_out == NULL || (out_cap != 0 && out == NULL)) {
        append_detail(error_out, "invalid header serialization argument");
        return IMGNEKO_COMMAND_INVALID_ARGUMENT;
    }

    ImgnekoCommandError error = validate_representability(command, error_out);
    if (error != IMGNEKO_COMMAND_OK)
        return error;

    HeaderBuilder builder = {
        .out = out,
        .out_cap = out_cap,
        .first = true,
    };
    serialize_common(&builder, command);
    serialize_kind_fields(&builder, command);
    *len_out = builder.len;
    if (builder.len > out_cap) {
        append_detail(error_out,
                      "header requires %zu bytes, but buffer has %zu",
                      builder.len, out_cap);
        return IMGNEKO_COMMAND_BUFFER_TOO_SMALL;
    }
    return IMGNEKO_COMMAND_OK;
}

//===----------------------------------------------------------------------===//
// Parsing
//===----------------------------------------------------------------------===//

// Borrowed key and value spans for a graphics-command header pair.
typedef struct HeaderPair {
    const char *key;
    size_t key_len;
    const char *value;
    size_t value_len;
} HeaderPair;

// Cursor for repeated scans over a borrowed header.
typedef struct HeaderPairIterator {
    const char *header;
    size_t header_len;
    size_t next;
    bool done;
} HeaderPairIterator;

// Result of advancing a header-pair iterator.
typedef enum HeaderPairScanStatus {
    HEADER_PAIR_SCAN_END = 0,
    HEADER_PAIR_SCAN_PAIR,
    HEADER_PAIR_SCAN_ERROR,
} HeaderPairScanStatus;

// Advance an iterator and validate the next pair. `pair_out` receives borrowed
// spans on HEADER_PAIR_SCAN_PAIR and is unchanged otherwise. `error_out`
// receives a syntax diagnostic on HEADER_PAIR_SCAN_ERROR.
static HeaderPairScanStatus scan_header_pair(HeaderPairIterator *iterator,
                                             HeaderPair *pair_out,
                                             ImgnekoErrorDetail *error_out) {
    if (iterator->done)
        return HEADER_PAIR_SCAN_END;
    if (iterator->header_len == 0) {
        iterator->done = true;
        return HEADER_PAIR_SCAN_END;
    }

    size_t start = iterator->next;
    size_t end = start;
    while (end < iterator->header_len && iterator->header[end] != ',')
        ++end;
    if (end == start) {
        append_detail(error_out, "header contains an empty pair");
        iterator->done = true;
        return HEADER_PAIR_SCAN_ERROR;
    }

    size_t equals = start;
    while (equals < end && iterator->header[equals] != '=')
        ++equals;
    if (equals == end) {
        append_detail(error_out, "header pair is missing an equals sign");
        iterator->done = true;
        return HEADER_PAIR_SCAN_ERROR;
    }
    if (equals == start) {
        append_detail(error_out, "header pair has an empty key");
        iterator->done = true;
        return HEADER_PAIR_SCAN_ERROR;
    }

    *pair_out = (HeaderPair){
        .key = iterator->header + start,
        .key_len = equals - start,
        .value = iterator->header + equals + 1,
        .value_len = end - equals - 1,
    };
    if (end == iterator->header_len) {
        iterator->done = true;
    } else {
        iterator->next = end + 1;
    }
    return HEADER_PAIR_SCAN_PAIR;
}

// Parse a signed decimal integer using the graphics protocol's exact syntax.
// The general option parsers accept syntax such as hexadecimal prefixes, which
// is intentionally not valid in graphics command fields.
static bool parse_i64(const char *data, size_t len, int64_t *value_out) {
    bool negative = len != 0 && data[0] == '-';
    size_t offset = negative ? 1 : 0;
    uint64_t magnitude = 0;
    uint64_t limit = negative ? (uint64_t)INT64_MAX + 1u : (uint64_t)INT64_MAX;

    if (offset == len)
        return false;
    for (size_t i = offset; i < len; ++i) {
        unsigned char byte = (unsigned char)data[i];
        if (byte < '0' || byte > '9')
            return false;
        uint64_t digit = (uint64_t)(byte - '0');
        if (magnitude > (limit - digit) / 10)
            return false;
        magnitude = magnitude * 10 + digit;
    }
    if (negative && magnitude == (uint64_t)INT64_MAX + 1u)
        *value_out = INT64_MIN;
    else
        *value_out = negative ? -(int64_t)magnitude : (int64_t)magnitude;
    return true;
}

// Parse a decimal integer after confirming that it fits the unsigned field.
static bool parse_u32(const char *data, size_t len, uint32_t *value_out) {
    int64_t value;

    // Preserve the unsigned field syntax: even negative zero has a sign.
    if ((len != 0 && data[0] == '-') || !parse_i64(data, len, &value) ||
        value > UINT32_MAX)
        return false;
    *value_out = (uint32_t)value;
    return true;
}

// Parse a decimal integer after confirming that it fits the signed field.
static bool parse_i32(const char *data, size_t len, int32_t *value_out) {
    int64_t value;

    if (!parse_i64(data, len, &value) || value < INT32_MIN || value > INT32_MAX)
        return false;
    *value_out = (int32_t)value;
    return true;
}

// Parse a single character that can be emitted in a control-data value.
// `value_out` receives the parsed character on success.
static bool parse_protocol_char(const HeaderPair *pair, char *value_out) {
    if (pair->value_len != 1 || !protocol_char_is_representable(pair->value[0]))
        return false;
    *value_out = pair->value[0];
    return true;
}

// Apply the selected unknown-key policy and identify a rejected key in the
// diagnostic. Keys are borrowed spans and are not necessarily NUL-terminated.
static bool handle_unknown_key(const HeaderPair *pair,
                               ImgnekoCommandParseFlags flags,
                               ImgnekoErrorDetail *error_out) {
    bool drop = flags & IMGNEKO_COMMAND_PARSE_DROP_UNKNOWN_KEYS;
    // IMGNEKO_UNCOVERED_OK: Need a very large input span to test.
    int key_len = pair->key_len > INT_MAX ? INT_MAX : (int)pair->key_len;
    append_detail(error_out, "unknown graphics command key '%.*s'", key_len,
                  pair->key);
    return drop;
}

// Context shared by typed field parsers.
typedef struct CommandFieldParseContext {
    const HeaderPair *pair;
    ImgnekoCommandParseFlags flags;
    ImgnekoErrorDetail *error_out;
} CommandFieldParseContext;

// Apply the selected policy to a value that cannot be represented by its
// destination type: always append a diagnostic, and return true only when
// dropping unrepresentable fields is allowed.
static bool
handle_unrepresentable_field(const CommandFieldParseContext *context,
                             const char *description) {
    bool drop =
        context->flags & IMGNEKO_COMMAND_PARSE_DROP_UNREPRESENTABLE_VALUES;
    char key = context->pair->key[0];
    append_detail(context->error_out, "%s is not representable (%c)",
                  description, key);
    return drop;
}

// Parse an unsigned integer field. `destination` receives its value when the
// input is representable.
static bool parse_u32_field(const CommandFieldParseContext *context,
                            uint32_t *destination, const char *description) {
    uint32_t value;
    if (!parse_u32(context->pair->value, context->pair->value_len, &value))
        return handle_unrepresentable_field(context, description);
    *destination = value;
    return true;
}

// Parse a signed integer field. `destination` receives its value when the input
// is representable.
static bool parse_i32_field(const CommandFieldParseContext *context,
                            int32_t *destination, const char *description) {
    int32_t value;
    if (!parse_i32(context->pair->value, context->pair->value_len, &value))
        return handle_unrepresentable_field(context, description);
    *destination = value;
    return true;
}

// Parse a character field. `destination` receives its value when the input is
// representable.
static bool parse_char_field(const CommandFieldParseContext *context,
                             char *destination, const char *description) {
    char value;
    if (!parse_protocol_char(context->pair, &value))
        return handle_unrepresentable_field(context, description);
    *destination = value;
    return true;
}

// Parse a character discriminator and record that it was explicit only when
// its value was representable and stored.
//
// `context`
//     Current pair and structural error-handling policy.
// `destination`
//     Output parameter receiving a representable discriminator value.
// `implicit`
//     Output parameter cleared when `destination` is assigned.
// `scope`
//     Semantic scope used to resolve the field description on failure.
static bool parse_discriminator_field(const CommandFieldParseContext *context,
                                      char *destination, bool *implicit,
                                      CommandFieldScope scope) {
    char value;
    if (!parse_protocol_char(context->pair, &value))
        return handle_unrepresentable_field(
            context, command_field_description(scope, context->pair->key[0]));
    *destination = value;
    *implicit = false;
    return true;
}

// Parse a delete target into `delete_cmd`. An uppercase ASCII value requests
// data deletion and is stored as a lowercase target with `delete_data` set.
static bool parse_delete_target_field(const CommandFieldParseContext *context,
                                      ImgnekoDeleteCommand *delete_cmd) {
    char value;
    if (!parse_protocol_char(context->pair, &value))
        return handle_unrepresentable_field(
            context,
            command_field_description(SCOPE_DELETE, context->pair->key[0]));

    bool uppercase = value >= 'A' && value <= 'Z';
    char target = uppercase ? (char)(value - 'A' + 'a') : value;
    delete_cmd->target = target;
    delete_cmd->target_implicit = false;
    delete_cmd->delete_data = uppercase;
    return true;
}

// Expand a field list into type-checked switch cases. Discriminators are
// handled before entering their switches. Every switch declares `field_owner`
// with the structure type named by that field list.
#define PARSE_FIELD(key_, type_, member_, description_, predicate_)            \
    case key_:                                                                 \
        return parse_##type_##_field(context, &field_owner->member_,           \
                                     description_);

// Parse a pair directly into its typed command field. The union-selecting
// discriminators have already been resolved, so they determine which switches
// and union members are active. A recognized, representable value is stored in
// `command`.
static bool parse_command_pair(ImgnekoCommand *command,
                               const CommandFieldParseContext *context) {
    if (context->pair->key_len != 1)
        return handle_unknown_key(context->pair, context->flags,
                                  context->error_out);

    // Parse common fields like i= and I=.
    char key = context->pair->key[0];
    ImgnekoCommand *field_owner = command;
    switch (key) { COMMAND_COMMON_FIELDS(PARSE_FIELD) }

    // If we don't know the command kind, we can't parse anything else.
    if (!command_kind_is_known(command->kind))
        return handle_unknown_key(context->pair, context->flags,
                                  context->error_out);

    ImgnekoTransmission *transmission =
        imgneko_command_get_transmission(command);
    if (transmission != NULL) {
        ImgnekoTransmission *field_owner = transmission;
        switch (key) { COMMAND_TRANSMISSION_FIELDS(PARSE_FIELD) }
    }

    ImgnekoPlacement *placement = imgneko_command_get_placement(command);
    if (placement != NULL) {
        ImgnekoPlacement *field_owner = placement;
        switch (key) { COMMAND_PLACEMENT_FIELDS(PARSE_FIELD) }
    }

    switch (command->kind) {
    case IMGNEKO_COMMAND_FRAME: {
        ImgnekoFrame *field_owner = &command->data.frame.frame;
        switch (key) { COMMAND_FRAME_FIELDS(PARSE_FIELD) }
        break;
    }
    case IMGNEKO_COMMAND_ANIMATION: {
        ImgnekoAnimationCommand *field_owner = &command->data.animation;
        switch (key) { COMMAND_ANIMATION_FIELDS(PARSE_FIELD) }
        break;
    }
    case IMGNEKO_COMMAND_COMPOSE: {
        ImgnekoComposeCommand *field_owner = &command->data.compose;
        switch (key) { COMMAND_COMPOSE_FIELDS(PARSE_FIELD) }
        break;
    }
    case IMGNEKO_COMMAND_DELETE: {
        ImgnekoDeleteCommand *field_owner = &command->data.delete_cmd;
        if (field_owner->target == IMGNEKO_DELETE_IMAGES_BY_ID_RANGE) {
            switch (key) { COMMAND_DELETE_RANGE_FIELDS(PARSE_FIELD) }
        }
        switch (key) { COMMAND_DELETE_DATA_FIELDS(PARSE_FIELD) }
        break;
    }
    default:
        break;
    }

    return handle_unknown_key(context->pair, context->flags,
                              context->error_out);
}
#undef PARSE_FIELD

// Parse a command header. The first scan validates syntax and captures the
// fields needed to select command storage. The second scan applies duplicate
// policy and parses retained fields in source order.
//
// `header`, `header_len`
//     Borrowed header bytes.
// `flags`
//     Error-handling policies.
// `command`
//     Output parameter receiving the parsed command fields.
// `error_out`
//     Optional caller-owned storage receiving a detailed diagnostic.
static bool parse_command_header(const char *header, size_t header_len,
                                 ImgnekoCommandParseFlags flags,
                                 ImgnekoCommand *command,
                                 ImgnekoErrorDetail *error_out) {
    // Borrow the first union-selecting discriminator pairs. A NULL key means
    // that the corresponding discriminator was absent.
    HeaderPair action_pair = {0};
    HeaderPair delete_target_pair = {0};
    HeaderPairIterator iterator = {
        .header = header,
        .header_len = header_len,
    };
    HeaderPair pair;
    HeaderPairScanStatus status;

    // Validate the complete header before interpreting any fields, while also
    // finding the first discriminators needed to select union storage.
    while ((status = scan_header_pair(&iterator, &pair, error_out)) ==
           HEADER_PAIR_SCAN_PAIR) {
        if (pair.key_len != 1)
            continue;

        unsigned char key = (unsigned char)pair.key[0];
        if (key == 'a' && action_pair.key == NULL)
            action_pair = pair;
        else if (key == 'd' && delete_target_pair.key == NULL)
            delete_target_pair = pair;
    }
    if (status == HEADER_PAIR_SCAN_ERROR)
        return false;

    CommandFieldParseContext context = {
        .flags = flags,
        .error_out = error_out,
    };

    // Parse and set the command kind.
    if (action_pair.key != NULL) {
        context.pair = &action_pair;
        if (!parse_discriminator_field(&context, &command->kind,
                                       &command->kind_implicit, SCOPE_COMMON))
            return false;
    }
    if (command->kind == IMGNEKO_COMMAND_UNSET) {
        command->kind = IMGNEKO_COMMAND_TRANSMIT;
        command->kind_implicit = true;
    }

    ImgnekoTransmission *transmission =
        imgneko_command_get_transmission(command);

    // Parse and set the delete target.
    if (command->kind == IMGNEKO_COMMAND_DELETE) {
        ImgnekoDeleteCommand *delete_cmd = &command->data.delete_cmd;
        if (delete_target_pair.key != NULL) {
            context.pair = &delete_target_pair;
            if (!parse_delete_target_field(&context, delete_cmd))
                return false;
        }
        if (delete_cmd->target == IMGNEKO_DELETE_UNSET) {
            delete_cmd->target = IMGNEKO_DELETE_VISIBLE_PLACEMENTS;
            delete_cmd->target_implicit = true;
        }
    }

    // Rescan the header and parse the rest of the keys.
    bool seen_keys[256] = {false};
    iterator = (HeaderPairIterator){
        .header = header,
        .header_len = header_len,
    };
    while ((status = scan_header_pair(&iterator, &pair, error_out)) ==
           HEADER_PAIR_SCAN_PAIR) {
        if (pair.key_len == 1) {
            unsigned char key = (unsigned char)pair.key[0];
            if (seen_keys[key]) {
                bool drop = flags & IMGNEKO_COMMAND_PARSE_DROP_DUPLICATE_KEYS;
                append_detail(error_out, "duplicate key '%c'", pair.key[0]);
                if (!drop)
                    return false;
                continue;
            }
            seen_keys[key] = true;
            if (key == 'a' ||
                (key == 'd' && command->kind == IMGNEKO_COMMAND_DELETE))
                continue;
        }

        context.pair = &pair;
        if (!parse_command_pair(command, &context))
            return false;
    }
    // The first pass already validated this immutable header, so the rescan
    // cannot end with a structural error.

    // Set the implicit transmission medium if unset.
    if (transmission != NULL &&
        transmission->medium == IMGNEKO_TRANSMISSION_MEDIUM_UNSET) {
        transmission->medium = IMGNEKO_TRANSMISSION_MEDIUM_DIRECT;
        transmission->medium_implicit = true;
    }
    return true;
}

ImgnekoCommandParseError imgneko_command_parse(
    const char *data, size_t len, const ImgnekoCommandParseOptions *options,
    ImgnekoParsedCommand *parsed_out, ImgnekoErrorDetail *error_out) {
    // Check the parameters.
    if (!detail_is_valid(error_out))
        return IMGNEKO_COMMAND_PARSE_FAILED;
    clear_detail(error_out);
    if ((data == NULL && len != 0) || options == NULL || parsed_out == NULL) {
        append_detail(error_out, "invalid graphics command parse argument");
        return IMGNEKO_COMMAND_PARSE_FAILED;
    }

    if ((options->start_sequence == NULL && options->start_sequence_len != 0) ||
        (options->end_sequence == NULL && options->end_sequence_len != 0)) {
        append_detail(error_out, "framing pointer is NULL for a nonempty span");
        return IMGNEKO_COMMAND_PARSE_FAILED;
    }

    // Remove the framing.
    const char *start = options->start_sequence;
    size_t start_len = options->start_sequence_len;
    if (start == NULL) {
        start = DEFAULT_START_SEQUENCE;
        start_len = sizeof(DEFAULT_START_SEQUENCE) - 1;
    }

    const char *end = options->end_sequence;
    size_t end_len = options->end_sequence_len;
    if (end == NULL) {
        end = DEFAULT_END_SEQUENCE;
        end_len = sizeof(DEFAULT_END_SEQUENCE) - 1;
    }

    if (start_len > len || end_len > len - start_len ||
        (start_len != 0 && memcmp(data, start, start_len) != 0) ||
        (end_len != 0 && memcmp(data + len - end_len, end, end_len) != 0)) {
        append_detail(error_out, "graphics command framing does not match");
        return IMGNEKO_COMMAND_PARSE_FAILED;
    }

    // Separate the payload.
    memset(parsed_out, 0, sizeof(*parsed_out));
    const char *body = data == NULL ? NULL : data + start_len;
    size_t body_len = len - start_len - end_len;
    size_t header_len = body_len;
    const char *separator = body_len == 0 ? NULL : memchr(body, ';', body_len);
    if (separator != NULL) {
        header_len = (size_t)(separator - body);
        parsed_out->payload = separator + 1;
        parsed_out->payload_len = body_len - header_len - 1;
    }
    parsed_out->header = body;
    parsed_out->header_len = header_len;

    // Parse the header.
    if (!parse_command_header(body, header_len, options->flags,
                              &parsed_out->command, error_out))
        return IMGNEKO_COMMAND_PARSE_FAILED;

    return IMGNEKO_COMMAND_PARSE_OK;
}
