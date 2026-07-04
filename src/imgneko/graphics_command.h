// SPDX-License-Identifier: MIT-0

// Kitty graphics protocol command data, parsing, validation, and serialization.
//
// Most command fields use zero-initialized values for protocol defaults.
// Zero-valued fields are normally omitted from the serialized header, with two
// exceptions:
// - Generated final continuations include `m=0`.
// - Delete targets that use a z-index canonicalize zero as `z=0`.
// Command kind, transmission medium, and delete target must be set explicitly
// (must not be 0, if required at all), but their default values may still be
// omitted when their corresponding `_implicit` fields are true.
//
// Transmission payloads are borrowed. Direct transmissions use a reader;
// non-direct transmissions use a raw name span, which the serializer encodes
// as base64. Keep the active payload alive until command consumption ends.

// Transmission payload setup examples
// -----------------------------------
//
// Pass file and shared memory names as raw spans. Serialization base64-encodes
// them automatically:
//
//   static const char image_path[] = "/tmp/image.png";
//   ImgnekoTransmission filename_transmission = {
//       .medium = IMGNEKO_TRANSMISSION_MEDIUM_FILE,
//       .format = IMGNEKO_IMAGE_FORMAT_PNG,
//       .payload.name = {
//           .data = image_path,
//           .len = sizeof(image_path) - 1,
//       },
//   };
//
// To transmit file contents directly, open the file as a source and wrap it in
// a base64 encoder. Deinitialize the file reader after command consumption:
//
//   char file_base64_workspace[4096];
//   ImgnekoFileReader file_source = {0};
//   ImgnekoBase64EncodeReader encoded_file;
//
//   if (imgneko_file_reader_init(&file_source, "/tmp/image.png") != 0)
//       handle_error();
//   imgneko_base64_encode_reader_init(
//       &encoded_file, imgneko_file_reader_as_reader(&file_source),
//       file_base64_workspace, sizeof(file_base64_workspace));
//   ImgnekoTransmission direct_file_transmission = {
//       .medium = IMGNEKO_TRANSMISSION_MEDIUM_DIRECT,
//       .format = IMGNEKO_IMAGE_FORMAT_PNG,
//       .payload.direct =
//           imgneko_base64_encode_reader_as_reader(&encoded_file),
//   };
//   ...
//   // After the command is completely consumed:
//   imgneko_file_reader_deinit(&file_source);
//
// To compress direct file data, place a zlib compressor between the file and
// base64 readers. Deinitialize the zlib reader before the file reader:
//
//   char zlib_workspace[16384];
//   char compressed_base64_workspace[4096];
//   ImgnekoFileReader raw_file_source = {0};
//   ImgnekoZlibCompressReader compressed_file = {0};
//   ImgnekoBase64EncodeReader encoded_compressed_file;
//
//   if (imgneko_file_reader_init(&raw_file_source, "/tmp/image.rgba") != 0)
//       handle_error();
//   if (imgneko_zlib_compress_reader_init(
//           &compressed_file,
//           imgneko_file_reader_as_reader(&raw_file_source), zlib_workspace,
//           sizeof(zlib_workspace)) != IMGNEKO_ZLIB_OK)
//       handle_error();
//   imgneko_base64_encode_reader_init(
//       &encoded_compressed_file,
//       imgneko_zlib_compress_reader_as_reader(&compressed_file),
//       compressed_base64_workspace, sizeof(compressed_base64_workspace));
//   ImgnekoTransmission compressed_file_transmission = {
//       .medium = IMGNEKO_TRANSMISSION_MEDIUM_DIRECT,
//       .format = IMGNEKO_IMAGE_FORMAT_RGBA,
//       .compression = IMGNEKO_PAYLOAD_COMPRESSION_ZLIB,
//       .pixel_width = 64,
//       .pixel_height = 64,
//       .payload.direct =
//           imgneko_base64_encode_reader_as_reader(&encoded_compressed_file),
//   };
//   ...
//   // After the command is completely consumed:
//   imgneko_zlib_compress_reader_deinit(&compressed_file);
//   imgneko_file_reader_deinit(&raw_file_source);
//
// To send a transmission command, put it in its action wrapper and write it to
// stdout:
//
//   ImgnekoCommand command = {
//       .kind = IMGNEKO_COMMAND_TRANSMIT,
//       .image_id = 6,
//       .data.transmit = {.transmission = filename_transmission},
//   };
//   ImgnekoCommandSerializationOptions options = {0};
//   int output_fd = STDOUT_FILENO;
//
//   if (imgneko_command_write(
//           &command, &options, imgneko_writer_fd(&output_fd),
//           PIPE_BUF, NULL, NULL) != IMGNEKO_COMMAND_OK)
//       handle_error();

#ifndef IMGNEKO_GRAPHICS_COMMAND_H
#define IMGNEKO_GRAPHICS_COMMAND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "imgneko/reader.h"
#include "imgneko/writer.h"

// Maximum serialized command size enforced by imgneko. Direct data is split at
// this limit. A non-direct transmission must fit in a single command.
//
// The Kitty graphics protocol permits payload chunks of up to 4096 bytes;
// imgneko is more conservative and includes framing and the header in the same
// limit.
#define IMGNEKO_COMMAND_MAX_SIZE 4096

// Maximum header size produced from an ImgnekoCommand, excluding framing, a
// user-specified header suffix, the payload separator, and payload bytes.
#define IMGNEKO_COMMAND_HEADER_MAX_SIZE 356

// Errors reported by graphics command validation, serialization, and readers.
typedef enum ImgnekoCommandError {
    IMGNEKO_COMMAND_OK = 0,
    // A required pointer or other API argument is invalid.
    IMGNEKO_COMMAND_INVALID_ARGUMENT = 1,
    // A command field or combination of fields is invalid for the command.
    IMGNEKO_COMMAND_INVALID_FIELD = 2,
    // A serialized size cannot be represented by size_t.
    IMGNEKO_COMMAND_OVERFLOW = 3,
    // A serialized command would exceed IMGNEKO_COMMAND_MAX_SIZE.
    IMGNEKO_COMMAND_TOO_LARGE = 4,
    // A payload or transformer reader failed.
    IMGNEKO_COMMAND_READ_FAILED = 5,
    // Caller-owned output storage is too small for the complete operation.
    IMGNEKO_COMMAND_BUFFER_TOO_SMALL = 6,
    // Borrowed transformer workspace is too small for the operation.
    IMGNEKO_COMMAND_WORKSPACE_TOO_SMALL = 7,
    // A writer rejected a complete serialized command span.
    IMGNEKO_COMMAND_WRITE_FAILED = 8,
} ImgnekoCommandError;

// Caller-owned storage for an optional detailed error message.
//
// The caller initializes `message` and `message_cap`. `message_cap` is the
// total buffer capacity, including space for the null terminator. When it is
// nonzero, `message` must not be NULL. The stored message is always
// null-terminated, including when it is truncated to fit the buffer.
//
// APIs clear this structure before use and may append multiple diagnostics,
// separated by newlines. A successful operation may still report warnings.
//
// `message_len` reports the complete combined message length, excluding the
// null terminator, even if the message is truncated. It may therefore be
// larger than `message_cap`.
typedef struct ImgnekoErrorDetail {
    char *message;
    size_t message_cap;
    size_t message_len;
} ImgnekoErrorDetail;

// Response suppression requested with the protocol `q` key.
enum ImgnekoQuietnessEnum {
    // Report normal success and error responses (default).
    IMGNEKO_QUIETNESS_VERBOSE = 0,
    // Suppress successful responses but retain errors.
    IMGNEKO_QUIETNESS_ERRORS_ONLY = 1,
    // Suppress both successful and error responses.
    IMGNEKO_QUIETNESS_SILENT = 2,
};
typedef uint32_t ImgnekoQuietnessUInt32;

// Usage-hint bits sent through the protocol `N` key.
enum ImgnekoImageUsageHintEnum {
    IMGNEKO_IMAGE_USAGE_NONE = 0,
    // The image or frame is expected to be used only briefly.
    IMGNEKO_IMAGE_USAGE_TRANSIENT = 1,
};
typedef uint32_t ImgnekoImageUsageHintUInt32;

// Pixel composition mode selected with `X` for frame transmission and `C` for
// frame composition.
enum ImgnekoCompositionModeEnum {
    // Alpha-blend source pixels over destination pixels (default).
    IMGNEKO_COMPOSITION_ALPHA_BLEND = 0,
    // Replace destination pixels with source pixels.
    IMGNEKO_COMPOSITION_OVERWRITE = 1,
};
typedef uint32_t ImgnekoCompositionModeUInt32;

// State selected by an animation-control command's protocol `s` key.
enum ImgnekoAnimationStateEnum {
    // Do not change the animation state.
    IMGNEKO_ANIMATION_STATE_UNCHANGED = 0,
    // Stop the animation and reset its loop counter.
    IMGNEKO_ANIMATION_STATE_STOPPED = 1,
    // Run until the last available frame, then wait for more frames.
    IMGNEKO_ANIMATION_STATE_LOADING = 2,
    // Run normally, looping after the last frame.
    IMGNEKO_ANIMATION_STATE_RUNNING = 3,
};
typedef uint32_t ImgnekoAnimationStateUInt32;

// Pixel format requested with the protocol `f` key.
enum ImgnekoImageFormatEnum {
    // Leave the format unspecified and use the protocol default.
    IMGNEKO_IMAGE_FORMAT_DEFAULT = 0,
    // 32-bit RGBA pixels.
    IMGNEKO_IMAGE_FORMAT_RGBA = 32,
    // 24-bit RGB pixels.
    IMGNEKO_IMAGE_FORMAT_RGB = 24,
    // PNG data.
    IMGNEKO_IMAGE_FORMAT_PNG = 100,
};
typedef uint32_t ImgnekoImageFormatUInt32;

// Payload transmission medium requested with the protocol `t` key.
enum ImgnekoTransmissionMediumEnum {
    // Not initialized/parsed. This is not considered a valid protocol value,
    // use 'd' with `medium_implicit=true` to drop `t=d` from the header.
    IMGNEKO_TRANSMISSION_MEDIUM_UNSET = 0,
    // Payload bytes are carried directly by graphics commands.
    IMGNEKO_TRANSMISSION_MEDIUM_DIRECT = 'd',
    // Payload names a regular file.
    IMGNEKO_TRANSMISSION_MEDIUM_FILE = 'f',
    // Payload names a temporary file that the terminal may delete.
    IMGNEKO_TRANSMISSION_MEDIUM_TEMP_FILE = 't',
    // Payload names a shared-memory object.
    IMGNEKO_TRANSMISSION_MEDIUM_SHARED_MEMORY = 's',
};
typedef char ImgnekoTransmissionMediumChar;

// Compression applied to payload bytes before base64 encoding.
enum ImgnekoPayloadCompressionEnum {
    // No compression.
    IMGNEKO_PAYLOAD_COMPRESSION_NONE = 0,
    // RFC 1950 zlib compression.
    IMGNEKO_PAYLOAD_COMPRESSION_ZLIB = 'z',
};
typedef char ImgnekoPayloadCompressionChar;

// Selection performed by a delete command's protocol `d` key.
//
// The final value of the `d` key also depends on
// `ImgnekoDeleteCommand.delete_data`. When false, the selected target is
// serialized in lowercase. When true, it is serialized in uppercase, which
// also removes matching image data.
enum ImgnekoDeleteTargetEnum {
    // Not initialized/parsed. This is not considered a valid protocol value,
    // use 'a' with `target_implicit=true` to drop `d=a` from the header.
    IMGNEKO_DELETE_UNSET = 0,
    // Delete visible placements.
    IMGNEKO_DELETE_VISIBLE_PLACEMENTS = 'a',
    // Delete by image ID, optionally restricted by placement ID.
    IMGNEKO_DELETE_IMAGE_OR_PLACEMENT_BY_ID = 'i',
    // Delete by image number, optionally restricted by placement ID.
    IMGNEKO_DELETE_IMAGE_OR_PLACEMENT_BY_NUMBER = 'n',
    // Delete placements intersecting the cursor.
    IMGNEKO_DELETE_PLACEMENTS_AT_CURSOR = 'c',
    // Delete animation frames for the selected image.
    IMGNEKO_DELETE_ANIMATION_FRAMES = 'f',
    // Delete placements intersecting the cell at `ImgnekoDeleteCommand.x`
    // and `ImgnekoDeleteCommand.y`.
    IMGNEKO_DELETE_PLACEMENTS_AT_POSITION = 'p',
    // Delete placements intersecting the cell at `ImgnekoDeleteCommand.x`
    // and `ImgnekoDeleteCommand.y` with `ImgnekoDeleteCommand.z_index`.
    IMGNEKO_DELETE_PLACEMENTS_AT_POSITION_AND_Z_INDEX = 'q',
    // Delete images in the inclusive ID range stored in
    // `ImgnekoDeleteCommand.first_image_id` and
    // `ImgnekoDeleteCommand.last_image_id`.
    IMGNEKO_DELETE_IMAGES_BY_ID_RANGE = 'r',
    // Delete placements intersecting column `ImgnekoDeleteCommand.x`.
    IMGNEKO_DELETE_PLACEMENTS_AT_COLUMN = 'x',
    // Delete placements intersecting row `ImgnekoDeleteCommand.y`.
    IMGNEKO_DELETE_PLACEMENTS_AT_ROW = 'y',
    // Delete placements at `ImgnekoDeleteCommand.z_index`.
    IMGNEKO_DELETE_PLACEMENTS_AT_Z_INDEX = 'z',
};
typedef char ImgnekoDeleteTargetChar;

// Image placement fields shared by transmit-and-put and put commands.
typedef struct ImgnekoPlacement {
    // Client-selected placement ID (`p`). Zero creates an anonymous placement.
    uint32_t placement_id;
    // Virtual-placement mode (`U`). Zero creates a regular placement and one
    // creates a virtual placement for Unicode placeholders.
    uint32_t virtual_placement;
    // Placement width and height in terminal cells (`c` and `r`).
    uint32_t cols;
    uint32_t rows;
    // Cursor movement policy (`C`). Zero uses the default movement and one
    // prevents cursor movement.
    uint32_t do_not_move_cursor;
    // Source rectangle in image pixels (`x`, `y`, `w`, and `h`). Although the
    // current protocol defines nonnegative coordinates, signed storage leaves
    // room for future extensions and terminals with extended capabilities.
    int32_t src_x;
    int32_t src_y;
    uint32_t src_w;
    uint32_t src_h;
    // Pixel offsets inside the first placement cell (`X` and `Y`). Note that
    // terminal behavior differs when an offset lies outside that cell: some
    // terminals restrict it to the cell bounds, while others use it as given.
    // Signed storage also leaves room for future protocol extensions.
    int32_t cell_x_offset;
    int32_t cell_y_offset;
    // Signed stacking order (`z`). Zero selects the protocol default.
    int32_t z_index;
    // Parent image ID (`P`) and optional placement ID (`Q`) for relative
    // placement. `P=0` means there is no parent and requires `Q=0`. If `P` is
    // nonzero, `Q` may be zero to let the terminal autoselect a placement.
    // A virtual placement cannot have a parent.
    uint32_t parent_image_id;
    uint32_t parent_placement_id;
    // Signed horizontal and vertical cell offsets from the parent (`H` and
    // `V`). They require a nonzero parent image ID. Positive values move right
    // and down; negative values move left and up.
    int32_t parent_x_offset;
    int32_t parent_y_offset;
} ImgnekoPlacement;

// Animation-frame parameters used by the frame transmit action.
typedef struct ImgnekoFrame {
    // Position of the transmitted rectangle in the frame (`x` and `y`).
    uint32_t data_x;
    uint32_t data_y;
    // Base frame (`c`) for a newly created frame. Zero uses background_rgba;
    // a nonzero base frame and nonzero background must not be combined.
    uint32_t base_frame_number;
    // Existing frame to edit (`r`). Zero creates a new frame. A nonzero value
    // cannot be combined with a base frame or nonzero background color.
    uint32_t frame_number;
    // Delay before the next frame (`z`) in milliseconds. Positive values set a
    // delay, zero is ignored, and negative values create a gapless frame.
    int32_t gap_ms;
    // How transmitted pixels are combined with the frame canvas (`X`).
    ImgnekoCompositionModeUInt32 composition;
    // Canvas background color (`Y`) as 32-bit RGBA (0xRRGGBBAA).
    uint32_t background_rgba;
} ImgnekoFrame;

// Borrowed byte span.
typedef struct ImgnekoByteSpan {
    const char *data;
    size_t len;
} ImgnekoByteSpan;

// Transport, metadata, and payload fields shared by all data-bearing actions.
// Image ID/number and response suppression belong to the owning ImgnekoCommand.
//
// `medium` selects the active payload member:
// - 'd' uses `payload.direct`, a reader that must produce base64-encoded data.
// - 'f', 't', and 's' use `payload.name` for a raw name span. It will be
//   base64-encoded during serialization automatically.
typedef struct ImgnekoTransmission {
    // Transmission medium (`t`).
    ImgnekoTransmissionMediumChar medium;
    // True omits `t` when medium is IMGNEKO_TRANSMISSION_MEDIUM_DIRECT. It has
    // no effect for other medium values.
    bool medium_implicit;
    // Data size (`S`). For file and shared-memory media, this limits the number
    // of bytes read. A directly transmitted zlib-compressed PNG must provide
    // the uncompressed PNG data size. Zero omits the key.
    uint32_t data_size;
    // File/shared-memory byte offset (`O`). Zero omits the key.
    uint32_t data_offset;
    // Continuation state (`m`) for direct transmission. Zero closes the
    // transmission; one leaves it open for later data. If the command is split
    // into multiple segments, the last segment's `m` key will match this field.
    // Must be zero for non-direct media.
    uint32_t more_data;
    // Pixel format (`f`).
    ImgnekoImageFormatUInt32 format;
    // Compression (`o`).
    ImgnekoPayloadCompressionChar compression;
    // Width and height of the transmitted pixel rectangle (`s` and `v`).
    uint32_t pixel_width;
    uint32_t pixel_height;
    // Image usage-hint bitmask (`N`). Combine ImgnekoImageUsageHintEnum values.
    ImgnekoImageUsageHintUInt32 usage_hints;
    // Borrowed payload selected by `medium`.
    union {
        // Base64-encoded direct data. Reads must return complete four-byte
        // quartets. A non-NULL callback supplies a payload and may immediately
        // report EOF for an empty payload. If compression is set, the data must
        // already be compressed (before base64 encoding).
        ImgnekoReader direct;
        // Raw name for a non-direct medium. A non-NULL `data` supplies a name.
        // Serialization base64-encodes the span.
        ImgnekoByteSpan name;
    } payload;
} ImgnekoTransmission;

// Transmit image data with action `a=t`.
//
// Example:
//   `<ESC>_Ga=t,i=7,f=100;<...><ESC>\`
//     - command.image_id = 7
//     - transmission.format = IMGNEKO_IMAGE_FORMAT_PNG
typedef struct ImgnekoTransmitCommand {
    ImgnekoTransmission transmission;
} ImgnekoTransmitCommand;

// Transmit image data and create a placement with action `a=T`.
//
// An all-zero placement still requests a default-sized placement at the
// cursor.
//
// Example:
//   `<ESC>_Ga=T,i=7,f=100,c=2;<...><ESC>\`
//     - command.image_id = 7
//     - transmission.format = IMGNEKO_IMAGE_FORMAT_PNG
//     - placement.cols = 2
typedef struct ImgnekoTransmitAndPutCommand {
    ImgnekoTransmission transmission;
    ImgnekoPlacement placement;
} ImgnekoTransmitAndPutCommand;

// Query graphics-protocol support with action `a=q`.
//
// `ImgnekoCommand.image_id` must be nonzero and `ImgnekoCommand.image_number`
// must be zero.
//
// Example:
//   `<ESC>_Ga=q,i=7,f=24,s=1,v=1;AAAA<ESC>\`
//     - command.image_id = 7
//     - transmission.format = IMGNEKO_IMAGE_FORMAT_RGB
//     - transmission.pixel_width = 1
//     - transmission.pixel_height = 1
typedef struct ImgnekoQueryCommand {
    ImgnekoTransmission transmission;
} ImgnekoQueryCommand;

// Transmit animation-frame data with action `a=f`.
//
// Exactly one image identifier must be nonzero. Every generated segment retains
// action `a=f`. Continuation segments omit the selected image identifier by
// default; serialization options can request that it be repeated.
//
// Example:
//   `<ESC>_Ga=f,i=7,f=32,s=20,v=10,x=4,y=3,z=40;<...><ESC>\`
//     - command.image_id = 7
//     - transmission.format = IMGNEKO_IMAGE_FORMAT_RGBA
//     - transmission.pixel_width = 20
//     - transmission.pixel_height = 10
//     - frame.data_x = 4
//     - frame.data_y = 3
//     - frame.gap_ms = 40
typedef struct ImgnekoFrameTransmitCommand {
    ImgnekoTransmission transmission;
    ImgnekoFrame frame;
} ImgnekoFrameTransmitCommand;

// Change playback state or timing with animation-control action `a=a`.
//
// Example:
//   `<ESC>_Ga=a,i=7,s=3,v=1<ESC>\`
//     - command.image_id = 7
//     - state = IMGNEKO_ANIMATION_STATE_RUNNING
//     - loop_count = 1
typedef struct ImgnekoAnimationCommand {
    // Playback state (`s`). Zero leaves the state unchanged.
    ImgnekoAnimationStateUInt32 state;
    // Frame affected by gap_ms (`r`). Frame numbers are one-based.
    uint32_t frame_number;
    // Replacement gap (`z`) in milliseconds. Zero leaves the gap unchanged;
    // negative values make the selected frame gapless.
    int32_t gap_ms;
    // Frame to make current (`c`). Frame numbers are one-based.
    uint32_t current_frame_number;
    // Loop value (`v`). Zero leaves it unchanged, one loops indefinitely, and
    // larger values request one fewer loop than their numeric value.
    uint32_t loop_count;
} ImgnekoAnimationCommand;

// Compose a rectangle from one animation frame onto another with action `a=c`.
//
// Example:
//   `<ESC>_Ga=c,i=7,r=2,c=3,w=20,h=10,X=4,Y=5,x=1,y=2<ESC>\`
//     - command.image_id = 7
//     - src_frame_number = 2
//     - dst_frame_number = 3
//     - src_x = 4, src_y = 5
//     - dst_x = 1, dst_y = 2
//     - width = 20, height = 10
typedef struct ImgnekoComposeCommand {
    // Source (`r`) and destination (`c`) one-based frame numbers.
    uint32_t src_frame_number;
    uint32_t dst_frame_number;
    // Source rectangle position (`X` and `Y`).
    uint32_t src_x;
    uint32_t src_y;
    // Destination rectangle position (`x` and `y`).
    uint32_t dst_x;
    uint32_t dst_y;
    // Shared source and destination rectangle size (`w` and `h`). Zero uses
    // the full image dimension.
    uint32_t width;
    uint32_t height;
    // How source pixels are combined with destination pixels (`C`).
    ImgnekoCompositionModeUInt32 composition;
} ImgnekoComposeCommand;

// Place a previously transmitted image with action `a=p`.
//
// Example:
//   `<ESC>_Ga=p,i=7,p=3,c=2<ESC>\`
//     - command.image_id = 7
//     - placement.placement_id = 3
//     - placement.cols = 2
typedef struct ImgnekoPutCommand {
    // Placement parameters. An all-zero placement is valid.
    ImgnekoPlacement placement;
} ImgnekoPutCommand;

// Delete images, placements, or animation frames with action `a=d`.
// The requirements for `ImgnekoCommand.image_id` and
// `ImgnekoCommand.image_number` depend on the selected target.
//
// Example:
//   `<ESC>_Ga=d,i=10,d=i<ESC>\`
//     - command.image_id = 10
//     - target = IMGNEKO_DELETE_IMAGE_OR_PLACEMENT_BY_ID
//
//   `<ESC>_Ga=d,i=10,d=I<ESC>\`
//     - command.image_id = 10
//     - target = IMGNEKO_DELETE_IMAGE_OR_PLACEMENT_BY_ID
//     - delete_data = true
//
// Selecting IMGNEKO_DELETE_VISIBLE_PLACEMENTS with target_implicit=true
// produces `a=d` with no `d` key. With target_implicit=false, it produces
// `a=d,d=a`. Examples:
//
//   `<ESC>_Ga=d<ESC>\`
//     - target = IMGNEKO_DELETE_VISIBLE_PLACEMENTS
//     - target_implicit = true
//
//   `<ESC>_Ga=d,d=a<ESC>\`
//     - target = IMGNEKO_DELETE_VISIBLE_PLACEMENTS
//     - target_implicit = false
typedef struct ImgnekoDeleteCommand {
    // Optional placement restriction (`p`) for ID and number targets.
    uint32_t placement_id;
    // What the command deletes (`d`).
    ImgnekoDeleteTargetChar target;
    // True omits `d` when target is IMGNEKO_DELETE_VISIBLE_PLACEMENTS and
    // delete_data is false. It has no effect otherwise.
    bool target_implicit;
    // Use the target's uppercase form to also delete stored image data.
    bool delete_data;
    // Cell coordinates (`x` and `y`), as interpreted by target. Current
    // position-based targets require positive one-based values.
    int32_t x;
    int32_t y;
    // Signed z-index (`z`). A missing key defaults to zero; serialization emits
    // `z=0` when the target uses the z-index.
    int32_t z_index;
    // Inclusive image-ID range serialized through `x` and `y` for the range
    // target.
    uint32_t first_image_id;
    uint32_t last_image_id;
} ImgnekoDeleteCommand;

// Concrete command variant stored in ImgnekoCommand.
enum ImgnekoCommandKindEnum {
    // UNSET means not initialized/parsed. This is not considered a valid kind,
    // use 't' with `kind_implicit=true` to drop `a=t` from the header.
    IMGNEKO_COMMAND_UNSET = 0,
    IMGNEKO_COMMAND_ANIMATION = 'a',
    IMGNEKO_COMMAND_COMPOSE = 'c',
    IMGNEKO_COMMAND_DELETE = 'd',
    IMGNEKO_COMMAND_FRAME = 'f',
    IMGNEKO_COMMAND_PUT = 'p',
    IMGNEKO_COMMAND_QUERY = 'q',
    IMGNEKO_COMMAND_TRANSMIT = 't',
    IMGNEKO_COMMAND_TRANSMIT_AND_PUT = 'T',
};
typedef char ImgnekoCommandKindChar;

// A graphics command.
typedef struct ImgnekoCommand {
    ImgnekoCommandKindChar kind;
    // True omits `a` when kind is IMGNEKO_COMMAND_TRANSMIT. It has no effect
    // for other command kinds.
    bool kind_implicit;
    // Image ID (`i`) or image number (`I`). They identify an image
    // independently of command kind and must not both be nonzero. Individual
    // command kinds may impose stricter requirements.
    uint32_t image_id;
    uint32_t image_number;
    // Response suppression (`q`), independent of command kind.
    ImgnekoQuietnessUInt32 quietness;
    union {
        ImgnekoAnimationCommand animation;
        ImgnekoComposeCommand compose;
        ImgnekoDeleteCommand delete_cmd;
        ImgnekoFrameTransmitCommand frame;
        ImgnekoPutCommand put;
        ImgnekoQueryCommand query;
        ImgnekoTransmitCommand transmit;
        ImgnekoTransmitAndPutCommand transmit_and_put;
    } data;
} ImgnekoCommand;

//===----------------------------------------------------------------------===//
// Helpers
//===----------------------------------------------------------------------===//

// Return read-only transmission fields of a data-bearing command, or NULL when
// the command kind does not carry transmission data. `command` may be NULL.
static inline const ImgnekoTransmission *
imgneko_command_get_transmission_const(const ImgnekoCommand *command) {
    if (command == NULL)
        return NULL;

    switch (command->kind) {
    case IMGNEKO_COMMAND_FRAME:
        return &command->data.frame.transmission;
    case IMGNEKO_COMMAND_QUERY:
        return &command->data.query.transmission;
    case IMGNEKO_COMMAND_TRANSMIT:
        return &command->data.transmit.transmission;
    case IMGNEKO_COMMAND_TRANSMIT_AND_PUT:
        return &command->data.transmit_and_put.transmission;
    default:
        return NULL;
    }
}

// Return mutable transmission fields of a data-bearing command, or NULL when
// the command kind does not carry transmission data. `command` may be NULL.
static inline ImgnekoTransmission *
imgneko_command_get_transmission(ImgnekoCommand *command) {
    return (ImgnekoTransmission *)imgneko_command_get_transmission_const(
        command);
}

// Return read-only placement fields of a placement-bearing command, or NULL
// when the command kind does not carry placement data. `command` may be NULL.
static inline const ImgnekoPlacement *
imgneko_command_get_placement_const(const ImgnekoCommand *command) {
    if (command == NULL)
        return NULL;

    switch (command->kind) {
    case IMGNEKO_COMMAND_PUT:
        return &command->data.put.placement;
    case IMGNEKO_COMMAND_TRANSMIT_AND_PUT:
        return &command->data.transmit_and_put.placement;
    default:
        return NULL;
    }
}

// Return mutable placement fields of a placement-bearing command, or NULL when
// the command kind does not carry placement data. `command` may be NULL.
static inline ImgnekoPlacement *
imgneko_command_get_placement(ImgnekoCommand *command) {
    return (ImgnekoPlacement *)imgneko_command_get_placement_const(command);
}

//===----------------------------------------------------------------------===//
// Validation
//===----------------------------------------------------------------------===//

// Optional behavior for validating an in-memory graphics command.
typedef enum ImgnekoCommandValidationFlags {
    // Default: reject unknown values and validate all field relationships.
    IMGNEKO_COMMAND_VALIDATION_FLAGS_NONE = 0,
    // Accept unknown but representable protocol values. Internal UNSET sentinel
    // values remain invalid. If a discriminator (a, t, d) is unknown,
    // relationships that require knowing its meaning are not checked;
    // independent relationships are still validated.
    IMGNEKO_COMMAND_VALIDATION_ALLOW_UNKNOWN_VALUES = 1u << 0,
    // Skip cross-field relationship validation. Individual field values and
    // required discriminators are still validated.
    IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS = 1u << 1,
} ImgnekoCommandValidationFlags;

// Return stable text for a graphics command error, or a fallback for an
// unknown value.
const char *imgneko_command_error_string(ImgnekoCommandError error);

// Validate an in-memory command without consuming its direct payload reader.
//
// `command`
//     Command to validate. It must not be NULL.
// `flags`
//     Bitwise-ORed ImgnekoCommandValidationFlags values.
// `error_out`
//     Optional caller-owned storage receiving a detailed diagnostic.
ImgnekoCommandError
imgneko_command_validate(const ImgnekoCommand *command,
                         ImgnekoCommandValidationFlags flags,
                         ImgnekoErrorDetail *error_out);

//===----------------------------------------------------------------------===//
// Serialization
//===----------------------------------------------------------------------===//

// Serialize only the comma-separated control-data header for `command`.
//
// This function checks only whether fields can be represented in a header; it
// does not perform semantic or cross-field validation. It also does not read or
// encode the payload. Representable extension values and semantically invalid
// field combinations are serialized as provided.
//
// Redundant default-valued fields will be omitted, except for keys required by
// the command kind. Output is normalized and deterministic, but the ordering of
// control keys is not an API guarantee.
//
// The output does not include the start sequence, payload separator, payload,
// or end sequence.
//
// For example, a transmit command with image ID 7 and PNG format produces the
// header `a=t,i=7,f=100`.
//
// On success, the output contains exactly `len_out` serialized bytes and is not
// null-terminated. If the header does not fit, the function returns
// IMGNEKO_COMMAND_BUFFER_TOO_SMALL, copies the prefix that fits, and reports
// the exact required size through len_out.
//
// `command`
//     Command whose representable header fields are serialized.
// `out`
//     Caller-owned output storage. It may be NULL only when out_cap is zero.
// `out_cap`
//     Number of bytes available in out.
// `len_out`
//     Output parameter receiving the serialized or required header size. It
//     must not be NULL. It receives zero for failures other than
//     IMGNEKO_COMMAND_BUFFER_TOO_SMALL.
// `error_out`
//     Optional caller-owned storage receiving a detailed diagnostic.
ImgnekoCommandError
imgneko_command_header_to_buffer(const ImgnekoCommand *command, char *out,
                                 size_t out_cap, size_t *len_out,
                                 ImgnekoErrorDetail *error_out);

// Additional serialization options.
typedef struct ImgnekoCommandSerializationOptions {
    // Bytes written before the control-data header. NULL with a zero length
    // selects the default `<ESC>_G`. Non-NULL and zero len means empty string.
    const char *start_sequence;
    size_t start_sequence_len;
    // Bytes written after the header and optional payload. NULL with a zero
    // length selects the default `<ESC>\`. Non-NULL and zero len means empty
    // string.
    const char *end_sequence;
    size_t end_sequence_len;
    // Bytes appended verbatim to every serialized header before the payload
    // separator. Must include a leading comma if needed.
    const char *header_suffix;
    size_t header_suffix_len;
    // Repeat the selected `i` or `I` key in generated transmission continuation
    // headers. False follows the protocol's strict continuation form, which
    // contains `a=f`, `m`, and optionally `q`.
    bool repeat_frame_identifier_in_continuations;
} ImgnekoCommandSerializationOptions;

// Stateful reader that serializes a command into complete escape sequences.
//
// Commands without payloads produce one chunk. Direct transmissions may
// produce an initial command and generated continuations. Non-direct
// transmissions produce one command. Every successful read returns a complete
// command no larger than IMGNEKO_COMMAND_MAX_SIZE and never returns a partial
// sequence.
typedef struct ImgnekoCommandReader {
    // Command copy with a borrowed payload.
    ImgnekoCommand command;
    // Additional options, including start and end sequences and header suffix.
    ImgnekoCommandSerializationOptions options;
    // Detailed command-layer failure after a generic reader error.
    ImgnekoCommandError error_status;
    // Serialization progress flags.
    bool first_segment;
    bool eof;
} ImgnekoCommandReader;

// Initialize a command reader over a copy of `command`. This does not perform
// semantic validation. It still rejects invalid framing and oversized
// non-direct commands without consuming the payload.
//
// `reader`
//     Caller-owned reader state to initialize.
// `command`
//     Command to copy. Its payload remains borrowed and must outlive `reader`.
// `options`
//     Additional options, like start and end sequences and header suffix. May
//     be NULL to select defaults. The structure's byte spans must outlive
//     `reader`.
// `error_out`
//     Optional caller-owned storage receiving a detailed diagnostic.
//
// A failed initialization leaves `reader` in an inert state.
ImgnekoCommandError
imgneko_command_reader_init(ImgnekoCommandReader *reader,
                            const ImgnekoCommand *command,
                            const ImgnekoCommandSerializationOptions *options,
                            ImgnekoErrorDetail *error_out);

// Reader callback for an `ImgnekoCommandReader` context.
ImgnekoReaderStatus imgneko_command_reader_func(void *ctx, char *out,
                                                size_t out_cap,
                                                size_t *len_out);

// Return a generic reader view of `reader`. The caller must keep `reader` alive
// until the returned reader reaches EOF or is no longer used.
ImgnekoReader imgneko_command_reader_as_reader(ImgnekoCommandReader *reader);

// Serialize `command` and write every complete sequence through `writer`. The
// function consumes an internal command reader using a stack buffer of
// IMGNEKO_COMMAND_MAX_SIZE bytes. Earlier writes cannot be rolled back if a
// later operation fails.
//
// This function does not validate the command. The payload and any underlying
// readers must remain alive until it returns.
//
// `command`
//     Command to serialize. It must not be NULL.
// `options`
//     Framing and header options. It may be NULL to select defaults. Its byte
//     spans are borrowed until this function returns.
// `writer`
//     Destination accepting complete command sequences.
// `chunk_cap`
//     Maximum bytes in each complete sequence passed to writer. It must be
//     between 1 and IMGNEKO_COMMAND_MAX_SIZE, inclusive.
// `required_cap_out`
//     Optional output parameter. On IMGNEKO_COMMAND_BUFFER_TOO_SMALL, it
//     receives a required or best-effort capacity no greater than
//     IMGNEKO_COMMAND_MAX_SIZE. It receives zero otherwise. Note that if there
//     is no correct size, IMGNEKO_COMMAND_TOO_LARGE is returned.
// `error_out`
//     Optional caller-owned storage receiving a detailed diagnostic.
ImgnekoCommandError
imgneko_command_write(const ImgnekoCommand *command,
                      const ImgnekoCommandSerializationOptions *options,
                      ImgnekoWriter writer, size_t chunk_cap,
                      size_t *required_cap_out, ImgnekoErrorDetail *error_out);

//===----------------------------------------------------------------------===//
// Parsing
//===----------------------------------------------------------------------===//

typedef enum ImgnekoCommandParseError {
    IMGNEKO_COMMAND_PARSE_OK = 0,
    IMGNEKO_COMMAND_PARSE_FAILED = 1,
} ImgnekoCommandParseError;

// Parsed command data and borrowed views into its serialized source.
//
// `header` and `payload` borrow the input passed to imgneko_command_parse() and
// remain valid only while that input remains alive and unmodified.
typedef struct ImgnekoParsedCommand {
    ImgnekoCommand command;
    // Borrowed serialized control-data header bytes.
    const char *header;
    size_t header_len;
    // Encoded payload bytes after the first semicolon. When the payload
    // separator is absent, this pointer is NULL and payload_len is zero.
    // When the payload is explicitly empty, this pointer is non-NULL and
    // payload_len is zero.
    const char *payload;
    size_t payload_len;
} ImgnekoParsedCommand;

// Structural error-handling policies for graphics command parsing.
//
// Parsing accepts every representable value, including values not known by the
// current protocol implementation. Call imgneko_command_validate() separately
// when semantic value and relationship validation is required.
//
// When a flag allows parsing to continue after a pair would otherwise be
// rejected, `error_out` receives the same diagnostic as strict parsing, even
// if IMGNEKO_COMMAND_PARSE_OK is returned. The parser appends diagnostics for
// additional discarded pairs, separating them with newlines.
typedef enum ImgnekoCommandParseFlags {
    // By default:
    // - unknown keys are rejected;
    // - unrepresentable values (invalid syntax or outside the destination
    //   field type's range) are rejected;
    // - repeated single-byte keys are rejected.
    IMGNEKO_COMMAND_PARSE_FLAGS_NONE = 0,
    // Drop unknown keys instead of rejecting the parse.
    IMGNEKO_COMMAND_PARSE_DROP_UNKNOWN_KEYS = 1u << 0,
    // Drop unrepresentable values of known keys instead of failing.
    IMGNEKO_COMMAND_PARSE_DROP_UNREPRESENTABLE_VALUES = 1u << 1,
    // Drop repeated single-byte keys after their first occurrence. This also
    // applies to discriminator keys (`a`, `t`, and `d`).
    IMGNEKO_COMMAND_PARSE_DROP_DUPLICATE_KEYS = 1u << 2,
} ImgnekoCommandParseFlags;

// Parsing configuration.
typedef struct ImgnekoCommandParseOptions {
    // Expected bytes before the control-data header. NULL with a zero length
    // selects the default `<ESC>_G`. Non-NULL and zero len means empty string.
    const char *start_sequence;
    size_t start_sequence_len;
    // Expected bytes after the header and optional payload. NULL with a zero
    // length selects the default `<ESC>\`. Non-NULL and zero len means empty
    // string.
    const char *end_sequence;
    size_t end_sequence_len;
    // Structural parse-error policies.
    ImgnekoCommandParseFlags flags;
} ImgnekoCommandParseOptions;

// Parse one complete serialized graphics command.
//
// The parser splits the control-data header into key/value pairs and builds the
// corresponding ImgnekoCommand. It applies omitted discriminator defaults such
// as `a=t`, but performs no semantic validation. Representable values unknown
// to the current protocol are preserved. Fields whose meaning cannot be
// determined because a discriminator is unknown are handled as unknown keys.
//
// Every header pair must be nonempty, contain an equals sign, and have a
// nonempty key. These syntax errors are always rejected.
//
// `data`, `len`
//     Serialized command bytes. `data` may be NULL only when `len` is zero.
// `options`
//     Framing and structural error-handling options. Must not be NULL.
// `parsed_out`
//     Output parameter receiving the parsed command and borrowed input spans.
//     Must not be NULL.
// `error_out`
//     Optional caller-owned storage receiving newline-separated diagnostics.
//     May receive warning messages even if parsing succeeds. If its buffer is
//     too small, `message_len` still reports the complete combined length.
ImgnekoCommandParseError imgneko_command_parse(
    const char *data, size_t len, const ImgnekoCommandParseOptions *options,
    ImgnekoParsedCommand *parsed_out, ImgnekoErrorDetail *error_out);

#endif
