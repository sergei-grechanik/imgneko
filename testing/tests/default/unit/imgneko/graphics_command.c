// SPDX-License-Identifier: MIT-0

// Cross-API and parsing-policy tests for Kitty graphics-protocol commands.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "imgneko/graphics_command.h"
#include "test_graphics_command.h"
#include "test_main.h"
#include "test_reader.h"
#include "util/common.h"
#include "util/string.h"

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#elif defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

// Command representation and expectations shared across the three APIs.
typedef struct CommandCase {
    const char *expected_tokens;
    ImgnekoCommand command;
    ImgnekoCommandValidationFlags validation_flags;
    const char *error_part;
} CommandCase;

// Command and shared serialization/validation diagnostic for an in-memory
// value that has no possible protocol header.
typedef struct UnrepresentableCommandCase {
    ImgnekoCommand command;
    const char *error_part;
} UnrepresentableCommandCase;

// Serialized input that cannot be parsed under any supported flag combination.
typedef struct UnparseableCommandCase {
    const char *input;
    const char *error_part;
} UnparseableCommandCase;

// Serialized input accepted only under specific flags, plus the normalized
// header produced by serializing the resulting command.
typedef struct ConditionalParseCase {
    const char *input;
    const char *expected_tokens;
    ImgnekoCommandParseFlags parse_flags;
    // Stable fragment of the first diagnostic produced by strict parsing.
    const char *error_part;
    // Optional exact warning output when several input pairs are discarded.
    const char *expected_warnings;
} ConditionalParseCase;

// Serialized input containing explicit protocol defaults and its normalized
// header after parsing and serialization.
typedef struct DefaultNormalizationCase {
    const char *input;
    const char *expected_tokens;
} DefaultNormalizationCase;

//===----------------------------------------------------------------------===//
// Valid and semivalid command cases
//===----------------------------------------------------------------------===//

// Valid and semivalid cases: the serialized header (up to permutation) and the
// expected parsed command. Parsing accepts every representable case. Semivalid
// cases additionally specify flags that relax validation and the strict
// validation diagnostic.
static const CommandCase command_cases[] = {
    // Protocol defaults and minimal valid action-specific fields.
    {
        "",
        {
            .kind = IMGNEKO_COMMAND_TRANSMIT,
            .kind_implicit = true,
            .data.transmit.transmission =
                {
                    .medium = IMGNEKO_TRANSMISSION_MEDIUM_DIRECT,
                    .medium_implicit = true,
                },
        },
    },
    {
        "a=t",
        {
            .kind = IMGNEKO_COMMAND_TRANSMIT,
            .data.transmit.transmission =
                {
                    .medium = IMGNEKO_TRANSMISSION_MEDIUM_DIRECT,
                    .medium_implicit = true,
                },
        },
    },
    {
        "t=d",
        {
            .kind = IMGNEKO_COMMAND_TRANSMIT,
            .kind_implicit = true,
            .data.transmit.transmission.medium =
                IMGNEKO_TRANSMISSION_MEDIUM_DIRECT,
        },
    },
    {
        "a=t,t=d",
        {
            .kind = IMGNEKO_COMMAND_TRANSMIT,
            .data.transmit.transmission.medium =
                IMGNEKO_TRANSMISSION_MEDIUM_DIRECT,
        },
    },
    {
        "a=a,i=1,s=2",
        {
            .kind = IMGNEKO_COMMAND_ANIMATION,
            .image_id = 1,
            .data.animation =
                {
                    .state = IMGNEKO_ANIMATION_STATE_LOADING,
                },
        },
    },
    {
        "a=c,i=1,r=1,c=2,w=1,h=1",
        {
            .kind = IMGNEKO_COMMAND_COMPOSE,
            .image_id = 1,
            .data.compose =
                {
                    .src_frame_number = 1,
                    .dst_frame_number = 2,
                    .width = 1,
                    .height = 1,
                },
        },
    },
    {
        "a=d",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .data.delete_cmd =
                {
                    .target = IMGNEKO_DELETE_VISIBLE_PLACEMENTS,
                    .target_implicit = true,
                },
        },
    },
    {
        "a=f,i=1,t=d,f=32,s=1,v=1",
        {
            .kind = IMGNEKO_COMMAND_FRAME,
            .image_id = 1,
            .data.frame.transmission =
                {
                    .medium = IMGNEKO_TRANSMISSION_MEDIUM_DIRECT,
                    .format = IMGNEKO_IMAGE_FORMAT_RGBA,
                    .pixel_width = 1,
                    .pixel_height = 1,
                },
        },
    },
    {
        "a=p,i=1",
        {
            .kind = IMGNEKO_COMMAND_PUT,
            .image_id = 1,
        },
    },
    {
        "a=q,i=1,t=d,f=32,s=1,v=1",
        {
            .kind = IMGNEKO_COMMAND_QUERY,
            .image_id = 1,
            .data.query.transmission =
                {
                    .medium = IMGNEKO_TRANSMISSION_MEDIUM_DIRECT,
                    .format = IMGNEKO_IMAGE_FORMAT_RGBA,
                    .pixel_width = 1,
                    .pixel_height = 1,
                },
        },
    },
    {
        "a=q,I=1,t=d,f=32,s=1,v=1",
        {
            .kind = IMGNEKO_COMMAND_QUERY,
            .image_number = 1,
            .data.query.transmission =
                {
                    .medium = IMGNEKO_TRANSMISSION_MEDIUM_DIRECT,
                    .format = IMGNEKO_IMAGE_FORMAT_RGBA,
                    .pixel_width = 1,
                    .pixel_height = 1,
                },
        },
        IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS,
        "query command (a=q) requires an image ID (i=...)",
    },
    {
        "a=T,i=1,t=d",
        {
            .kind = IMGNEKO_COMMAND_TRANSMIT_AND_PUT,
            .image_id = 1,
            .data.transmit_and_put.transmission =
                {
                    .medium = IMGNEKO_TRANSMISSION_MEDIUM_DIRECT,
                },
        },
    },
    // Unknown but representable discriminators are preserved by parsing.
    {
        "a=?",
        {
            .kind = (ImgnekoCommandKindChar)'?',
        },
        IMGNEKO_COMMAND_VALIDATION_ALLOW_UNKNOWN_VALUES,
        "unknown command kind (a=?)",
    },
    // Only the first equals sign separates a key from its value, so a second
    // equals sign is a representable extension value.
    {
        "a==",
        {
            .kind = (ImgnekoCommandKindChar)'=',
        },
        IMGNEKO_COMMAND_VALIDATION_ALLOW_UNKNOWN_VALUES,
        "unknown command kind (a==)",
    },
    // Common fields remain meaningful even when the command kind is unknown.
    {
        "a=?,i=1,q=1",
        {
            .kind = (ImgnekoCommandKindChar)'?',
            .image_id = 1,
            .quietness = IMGNEKO_QUIETNESS_ERRORS_ONLY,
        },
        IMGNEKO_COMMAND_VALIDATION_ALLOW_UNKNOWN_VALUES,
        "unknown command kind (a=?)",
    },
    // Identifier mutual exclusion is independent of command kind.
    {
        "a=?,i=1,I=2",
        {
            .kind = (ImgnekoCommandKindChar)'?',
            .image_id = 1,
            .image_number = 2,
        },
        IMGNEKO_COMMAND_VALIDATION_ALLOW_UNKNOWN_VALUES |
            IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS,
        "unknown command kind (a=?)",
    },

    // Transmission fields and media.
    {
        "a=t,i=7,t=d,S=123,q=2,m=1,f=100,o=z,N=1",
        {
            .kind = IMGNEKO_COMMAND_TRANSMIT,
            .image_id = 7,
            .quietness = IMGNEKO_QUIETNESS_SILENT,
            .data.transmit.transmission =
                {
                    .medium = IMGNEKO_TRANSMISSION_MEDIUM_DIRECT,
                    .data_size = 123,
                    .more_data = 1,
                    .format = IMGNEKO_IMAGE_FORMAT_PNG,
                    .compression = IMGNEKO_PAYLOAD_COMPRESSION_ZLIB,
                    .usage_hints = IMGNEKO_IMAGE_USAGE_TRANSIENT,
                },
        },
    },
    // Unknown but representable scalar values are preserved by parsing.
    {
        "a=t,i=1,t=d,q=9",
        {
            .kind = IMGNEKO_COMMAND_TRANSMIT,
            .image_id = 1,
            .quietness = (ImgnekoQuietnessUInt32)9,
            .data.transmit.transmission =
                {
                    .medium = IMGNEKO_TRANSMISSION_MEDIUM_DIRECT,
                },
        },
        IMGNEKO_COMMAND_VALIDATION_ALLOW_UNKNOWN_VALUES,
        "unknown quietness value (q=9)",
    },
    {
        "a=t,i=1,t=d,m=2",
        {
            .kind = IMGNEKO_COMMAND_TRANSMIT,
            .image_id = 1,
            .data.transmit.transmission =
                {
                    .medium = IMGNEKO_TRANSMISSION_MEDIUM_DIRECT,
                    .more_data = 2,
                },
        },
        IMGNEKO_COMMAND_VALIDATION_ALLOW_UNKNOWN_VALUES,
        "unknown continuation value (m=2)",
    },
    {
        "a=t,i=1,t=d,f=99",
        {
            .kind = IMGNEKO_COMMAND_TRANSMIT,
            .image_id = 1,
            .data.transmit.transmission =
                {
                    .medium = IMGNEKO_TRANSMISSION_MEDIUM_DIRECT,
                    .format = (ImgnekoImageFormatUInt32)99,
                },
        },
        IMGNEKO_COMMAND_VALIDATION_ALLOW_UNKNOWN_VALUES,
        "unknown image format (f=99)",
    },
    {
        "a=t,i=1,t=d,o=?",
        {
            .kind = IMGNEKO_COMMAND_TRANSMIT,
            .image_id = 1,
            .data.transmit.transmission =
                {
                    .medium = IMGNEKO_TRANSMISSION_MEDIUM_DIRECT,
                    .compression = (ImgnekoPayloadCompressionChar)'?',
                },
        },
        IMGNEKO_COMMAND_VALIDATION_ALLOW_UNKNOWN_VALUES,
        "unknown payload compression (o=?)",
    },
    {
        "a=t,i=1,t=d,o==",
        {
            .kind = IMGNEKO_COMMAND_TRANSMIT,
            .image_id = 1,
            .data.transmit.transmission =
                {
                    .medium = IMGNEKO_TRANSMISSION_MEDIUM_DIRECT,
                    .compression = (ImgnekoPayloadCompressionChar)'=',
                },
        },
        IMGNEKO_COMMAND_VALIDATION_ALLOW_UNKNOWN_VALUES,
        "unknown payload compression (o==)",
    },
    {
        "a=t,i=1,t=d,N=8",
        {
            .kind = IMGNEKO_COMMAND_TRANSMIT,
            .image_id = 1,
            .data.transmit.transmission =
                {
                    .medium = IMGNEKO_TRANSMISSION_MEDIUM_DIRECT,
                    .usage_hints = 8,
                },
        },
        IMGNEKO_COMMAND_VALIDATION_ALLOW_UNKNOWN_VALUES,
        "unknown image usage hints (N=8)",
    },
    {
        "a=t,I=4294967295",
        {
            .kind = IMGNEKO_COMMAND_TRANSMIT,
            .image_number = UINT32_MAX,
            .data.transmit.transmission =
                {
                    .medium = IMGNEKO_TRANSMISSION_MEDIUM_DIRECT,
                    .medium_implicit = true,
                },
        },
    },
    // The identifier relationship can be skipped without dropping either ID.
    {
        "a=t,i=1,I=2,t=d",
        {
            .kind = IMGNEKO_COMMAND_TRANSMIT,
            .image_id = 1,
            .image_number = 2,
            .data.transmit.transmission =
                {
                    .medium = IMGNEKO_TRANSMISSION_MEDIUM_DIRECT,
                },
        },
        IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS,
        "image ID (i=...) and image number (I=...) are both set",
    },
    // Both permissions are required to preserve the unknown value and the
    // otherwise-invalid identifier combination.
    {
        "a=t,i=1,I=2,t=d,q=9",
        {
            .kind = IMGNEKO_COMMAND_TRANSMIT,
            .image_id = 1,
            .image_number = 2,
            .quietness = (ImgnekoQuietnessUInt32)9,
            .data.transmit.transmission =
                {
                    .medium = IMGNEKO_TRANSMISSION_MEDIUM_DIRECT,
                },
        },
        IMGNEKO_COMMAND_VALIDATION_ALLOW_UNKNOWN_VALUES |
            IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS,
        "unknown quietness value (q=9)",
    },
    {
        "a=t,i=8,t=d,q=1,f=24,s=20,v=10",
        {
            .kind = IMGNEKO_COMMAND_TRANSMIT,
            .image_id = 8,
            .quietness = IMGNEKO_QUIETNESS_ERRORS_ONLY,
            .data.transmit.transmission =
                {
                    .medium = IMGNEKO_TRANSMISSION_MEDIUM_DIRECT,
                    .format = IMGNEKO_IMAGE_FORMAT_RGB,
                    .pixel_width = 20,
                    .pixel_height = 10,
                },
        },
    },
    {
        "a=t,i=1,t=d,f=32,s=1",
        {
            .kind = IMGNEKO_COMMAND_TRANSMIT,
            .image_id = 1,
            .data.transmit.transmission =
                {
                    .medium = IMGNEKO_TRANSMISSION_MEDIUM_DIRECT,
                    .format = IMGNEKO_IMAGE_FORMAT_RGBA,
                    .pixel_width = 1,
                },
        },
        IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS,
        "raw image format (f=24 or f=32) requires pixel width (s=...) and "
        "height (v=...)",
    },
    {
        "a=t,i=9,t=f,S=10,O=11,f=100",
        {
            .kind = IMGNEKO_COMMAND_TRANSMIT,
            .image_id = 9,
            .data.transmit.transmission =
                {
                    .medium = IMGNEKO_TRANSMISSION_MEDIUM_FILE,
                    .data_size = 10,
                    .data_offset = 11,
                    .format = IMGNEKO_IMAGE_FORMAT_PNG,
                },
        },
    },
    {
        "a=t,i=1,t=d,O=1",
        {
            .kind = IMGNEKO_COMMAND_TRANSMIT,
            .image_id = 1,
            .data.transmit.transmission =
                {
                    .medium = IMGNEKO_TRANSMISSION_MEDIUM_DIRECT,
                    .data_offset = 1,
                },
        },
        IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS,
        "data offset (O=...) requires file (t=f), temporary-file (t=t), or "
        "shared-memory (t=s) transmission",
    },
    {
        "a=t,i=1,t=f,m=1",
        {
            .kind = IMGNEKO_COMMAND_TRANSMIT,
            .image_id = 1,
            .data.transmit.transmission =
                {
                    .medium = IMGNEKO_TRANSMISSION_MEDIUM_FILE,
                    .more_data = 1,
                },
        },
        IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS,
        "continuation (m=...) requires direct transmission (t=d)",
    },
    {
        "a=t,i=1,t=d,f=100,o=z",
        {
            .kind = IMGNEKO_COMMAND_TRANSMIT,
            .image_id = 1,
            .data.transmit.transmission =
                {
                    .medium = IMGNEKO_TRANSMISSION_MEDIUM_DIRECT,
                    .format = IMGNEKO_IMAGE_FORMAT_PNG,
                    .compression = IMGNEKO_PAYLOAD_COMPRESSION_ZLIB,
                },
        },
        IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS,
        "compressed PNG (f=100 and o=z) requires data size (S=...)",
    },
    {
        "a=t,t=t",
        {
            .kind = IMGNEKO_COMMAND_TRANSMIT,
            .data.transmit.transmission.medium =
                IMGNEKO_TRANSMISSION_MEDIUM_TEMP_FILE,
        },
    },
    {
        "a=t,t=s",
        {
            .kind = IMGNEKO_COMMAND_TRANSMIT,
            .data.transmit.transmission.medium =
                IMGNEKO_TRANSMISSION_MEDIUM_SHARED_MEMORY,
        },
    },
    // An extension transmission medium is preserved as an unknown value.
    {
        "a=t,t=?",
        {
            .kind = IMGNEKO_COMMAND_TRANSMIT,
            .data.transmit.transmission =
                {
                    .medium = (ImgnekoTransmissionMediumChar)'?',
                },
        },
        IMGNEKO_COMMAND_VALIDATION_ALLOW_UNKNOWN_VALUES,
        "unknown transmission medium (t=?)",
    },
    {
        "a=t,i=1,t==",
        {
            .kind = IMGNEKO_COMMAND_TRANSMIT,
            .image_id = 1,
            .data.transmit.transmission =
                {
                    .medium = (ImgnekoTransmissionMediumChar)'=',
                },
        },
        IMGNEKO_COMMAND_VALIDATION_ALLOW_UNKNOWN_VALUES,
        "unknown transmission medium (t==)",
    },
    // An unknown transmission medium does not make the other transmission
    // keys ambiguous, so their representable values are retained as well. Put
    // both discriminators late to verify header order does not affect mapping.
    {
        "S=2,O=3,m=4,f=99,o=!,N=8,t=?,i=1,a=t",
        {
            .kind = IMGNEKO_COMMAND_TRANSMIT,
            .image_id = 1,
            .data.transmit.transmission =
                {
                    .medium = (ImgnekoTransmissionMediumChar)'?',
                    .data_size = 2,
                    .data_offset = 3,
                    .more_data = 4,
                    .format = (ImgnekoImageFormatUInt32)99,
                    .compression = (ImgnekoPayloadCompressionChar)'!',
                    .usage_hints = 8,
                },
        },
        IMGNEKO_COMMAND_VALIDATION_ALLOW_UNKNOWN_VALUES,
        "unknown transmission medium (t=?)",
    },

    // Placement fields. Relative and virtual placements are kept separate
    // because a virtual placement cannot have a parent.
    {
        "a=T,i=7,t=d,p=14,c=2,r=3,C=1,x=4,y=5,w=6,h=7,X=8,Y=9,z=-10",
        {
            .kind = IMGNEKO_COMMAND_TRANSMIT_AND_PUT,
            .image_id = 7,
            .data.transmit_and_put =
                {
                    .transmission =
                        {
                            .medium = IMGNEKO_TRANSMISSION_MEDIUM_DIRECT,
                        },
                    .placement =
                        {
                            .placement_id = 14,
                            .cols = 2,
                            .rows = 3,
                            .do_not_move_cursor = 1,
                            .src_x = 4,
                            .src_y = 5,
                            .src_w = 6,
                            .src_h = 7,
                            .cell_x_offset = 8,
                            .cell_y_offset = 9,
                            .z_index = -10,
                        },
                },
        },
    },
    {
        "a=p,i=1,z=-2147483648",
        {
            .kind = IMGNEKO_COMMAND_PUT,
            .image_id = 1,
            .data.put.placement.z_index = INT32_MIN,
        },
    },
    {
        "a=p,i=1,z=2147483647",
        {
            .kind = IMGNEKO_COMMAND_PUT,
            .image_id = 1,
            .data.put.placement.z_index = INT32_MAX,
        },
    },
    {
        "a=p,i=7,p=8,P=15,Q=16,H=-17,V=18",
        {
            .kind = IMGNEKO_COMMAND_PUT,
            .image_id = 7,
            .data.put =
                {
                    .placement =
                        {
                            .placement_id = 8,
                            .parent_image_id = 15,
                            .parent_placement_id = 16,
                            .parent_x_offset = -17,
                            .parent_y_offset = 18,
                        },
                },
        },
    },
    {
        "a=p,I=10,q=2,p=11,U=1,c=12,r=13",
        {
            .kind = IMGNEKO_COMMAND_PUT,
            .image_number = 10,
            .quietness = IMGNEKO_QUIETNESS_SILENT,
            .data.put =
                {
                    .placement =
                        {
                            .placement_id = 11,
                            .virtual_placement = 1,
                            .cols = 12,
                            .rows = 13,
                        },
                },
        },
    },
    {
        "a=p,i=1,U=2",
        {
            .kind = IMGNEKO_COMMAND_PUT,
            .image_id = 1,
            .data.put =
                {
                    .placement.virtual_placement = 2,
                },
        },
        IMGNEKO_COMMAND_VALIDATION_ALLOW_UNKNOWN_VALUES,
        "unknown virtual placement value (U=2)",
    },
    {
        "a=p,i=1,C=2",
        {
            .kind = IMGNEKO_COMMAND_PUT,
            .image_id = 1,
            .data.put =
                {
                    .placement.do_not_move_cursor = 2,
                },
        },
        IMGNEKO_COMMAND_VALIDATION_ALLOW_UNKNOWN_VALUES,
        "unknown cursor movement value (C=2)",
    },
    {
        "a=p,i=1,x=-2,y=-3",
        {
            .kind = IMGNEKO_COMMAND_PUT,
            .image_id = 1,
            .data.put =
                {
                    .placement =
                        {
                            .src_x = -2,
                            .src_y = -3,
                        },
                },
        },
        IMGNEKO_COMMAND_VALIDATION_ALLOW_UNKNOWN_VALUES,
        "placement source coordinates (x=... and y=...) must be nonnegative",
    },
    {
        "a=p",
        {
            .kind = IMGNEKO_COMMAND_PUT,
        },
        IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS,
        "put command (a=p) requires an image identifier (i=...) or number "
        "(I=...)",
    },
    {
        "a=p,i=1,P=2",
        {
            .kind = IMGNEKO_COMMAND_PUT,
            .image_id = 1,
            .data.put =
                {
                    .placement.parent_image_id = 2,
                },
        },
    },
    {
        "a=p,i=1,Q=3",
        {
            .kind = IMGNEKO_COMMAND_PUT,
            .image_id = 1,
            .data.put =
                {
                    .placement.parent_placement_id = 3,
                },
        },
        IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS,
        "parent placement ID (Q=...) requires a parent image ID (P=...)",
    },
    {
        "a=p,i=1,H=2,V=-3",
        {
            .kind = IMGNEKO_COMMAND_PUT,
            .image_id = 1,
            .data.put =
                {
                    .placement =
                        {
                            .parent_x_offset = 2,
                            .parent_y_offset = -3,
                        },
                },
        },
        IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS,
        "parent offsets (H=... or V=...) require a parent image ID (P=...)",
    },
    {
        "a=p,i=1,U=1,P=2,Q=3",
        {
            .kind = IMGNEKO_COMMAND_PUT,
            .image_id = 1,
            .data.put =
                {
                    .placement =
                        {
                            .virtual_placement = 1,
                            .parent_image_id = 2,
                            .parent_placement_id = 3,
                        },
                },
        },
        IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS,
        "virtual placement (U=...) cannot have a parent (P=...)",
    },
    // This extension value is preserved while the conflicting parent
    // relationship is accepted only under the second permission.
    {
        "a=p,i=1,C=2,U=1,P=2,Q=3",
        {
            .kind = IMGNEKO_COMMAND_PUT,
            .image_id = 1,
            .data.put =
                {
                    .placement =
                        {
                            .do_not_move_cursor = 2,
                            .virtual_placement = 1,
                            .parent_image_id = 2,
                            .parent_placement_id = 3,
                        },
                },
        },
        IMGNEKO_COMMAND_VALIDATION_ALLOW_UNKNOWN_VALUES |
            IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS,
        "unknown cursor movement value (C=2)",
    },

    // Animation-frame transmission. Editing a frame, creating from a base,
    // and creating over a background are mutually exclusive cases.
    {
        "a=f,i=7,t=d,q=1,m=1,f=32,s=20,v=10,x=4,y=3,r=5,z=-6,X=1",
        {
            .kind = IMGNEKO_COMMAND_FRAME,
            .image_id = 7,
            .quietness = IMGNEKO_QUIETNESS_ERRORS_ONLY,
            .data.frame =
                {
                    .transmission =
                        {
                            .medium = IMGNEKO_TRANSMISSION_MEDIUM_DIRECT,
                            .more_data = 1,
                            .format = IMGNEKO_IMAGE_FORMAT_RGBA,
                            .pixel_width = 20,
                            .pixel_height = 10,
                        },
                    .frame =
                        {
                            .data_x = 4,
                            .data_y = 3,
                            .frame_number = 5,
                            .gap_ms = -6,
                            .composition = IMGNEKO_COMPOSITION_OVERWRITE,
                        },
                },
        },
    },
    {
        "a=f,I=9,t=d,f=32,s=20,v=10,c=2",
        {
            .kind = IMGNEKO_COMMAND_FRAME,
            .image_number = 9,
            .data.frame =
                {
                    .transmission =
                        {
                            .medium = IMGNEKO_TRANSMISSION_MEDIUM_DIRECT,
                            .format = IMGNEKO_IMAGE_FORMAT_RGBA,
                            .pixel_width = 20,
                            .pixel_height = 10,
                        },
                    .frame.base_frame_number = 2,
                },
        },
    },
    {
        "a=f,i=7,t=d,f=32,s=20,v=10,Y=16909060",
        {
            .kind = IMGNEKO_COMMAND_FRAME,
            .image_id = 7,
            .data.frame =
                {
                    .transmission =
                        {
                            .medium = IMGNEKO_TRANSMISSION_MEDIUM_DIRECT,
                            .format = IMGNEKO_IMAGE_FORMAT_RGBA,
                            .pixel_width = 20,
                            .pixel_height = 10,
                        },
                    .frame.background_rgba = 0x01020304u,
                },
        },
    },
    {
        "a=f,i=1,t=d,f=32,s=1,v=1,X=2",
        {
            .kind = IMGNEKO_COMMAND_FRAME,
            .image_id = 1,
            .data.frame =
                {
                    .transmission =
                        {
                            .medium = IMGNEKO_TRANSMISSION_MEDIUM_DIRECT,
                            .format = IMGNEKO_IMAGE_FORMAT_RGBA,
                            .pixel_width = 1,
                            .pixel_height = 1,
                        },
                    .frame.composition = (ImgnekoCompositionModeUInt32)2,
                },
        },
        IMGNEKO_COMMAND_VALIDATION_ALLOW_UNKNOWN_VALUES,
        "unknown composition mode (X=2)",
    },
    {
        "a=f,t=d,f=32,s=1,v=1",
        {
            .kind = IMGNEKO_COMMAND_FRAME,
            .data.frame.transmission =
                {
                    .medium = IMGNEKO_TRANSMISSION_MEDIUM_DIRECT,
                    .format = IMGNEKO_IMAGE_FORMAT_RGBA,
                    .pixel_width = 1,
                    .pixel_height = 1,
                },
        },
        IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS,
        "frame command (a=f) requires an image identifier (i=...) or number "
        "(I=...)",
    },
    {
        "a=f,i=1,t=d,f=32,s=1,v=1,c=2,Y=1",
        {
            .kind = IMGNEKO_COMMAND_FRAME,
            .image_id = 1,
            .data.frame =
                {
                    .transmission =
                        {
                            .medium = IMGNEKO_TRANSMISSION_MEDIUM_DIRECT,
                            .format = IMGNEKO_IMAGE_FORMAT_RGBA,
                            .pixel_width = 1,
                            .pixel_height = 1,
                        },
                    .frame =
                        {
                            .base_frame_number = 2,
                            .background_rgba = 1,
                        },
                },
        },
        IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS,
        "base frame (c=...) and background (Y=...) cannot be combined",
    },
    {
        "a=f,i=1,t=d,f=32,s=1,v=1,c=2,r=3",
        {
            .kind = IMGNEKO_COMMAND_FRAME,
            .image_id = 1,
            .data.frame =
                {
                    .transmission =
                        {
                            .medium = IMGNEKO_TRANSMISSION_MEDIUM_DIRECT,
                            .format = IMGNEKO_IMAGE_FORMAT_RGBA,
                            .pixel_width = 1,
                            .pixel_height = 1,
                        },
                    .frame =
                        {
                            .base_frame_number = 2,
                            .frame_number = 3,
                        },
                },
        },
        IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS,
        "existing frame (r=...) cannot use a base frame (c=...)",
    },
    {
        "a=f,i=1,t=d,f=32,s=1,v=1,r=3,Y=1",
        {
            .kind = IMGNEKO_COMMAND_FRAME,
            .image_id = 1,
            .data.frame =
                {
                    .transmission =
                        {
                            .medium = IMGNEKO_TRANSMISSION_MEDIUM_DIRECT,
                            .format = IMGNEKO_IMAGE_FORMAT_RGBA,
                            .pixel_width = 1,
                            .pixel_height = 1,
                        },
                    .frame =
                        {
                            .frame_number = 3,
                            .background_rgba = 1,
                        },
                },
        },
        IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS,
        "existing frame (r=...) cannot use a background color (Y=...)",
    },

    // Animation control and frame composition.
    {
        "a=a,I=4294967295,q=2,s=3,r=4,z=-5,c=6,v=4294967295",
        {
            .kind = IMGNEKO_COMMAND_ANIMATION,
            .image_number = UINT32_MAX,
            .quietness = IMGNEKO_QUIETNESS_SILENT,
            .data.animation =
                {
                    .state = IMGNEKO_ANIMATION_STATE_RUNNING,
                    .frame_number = 4,
                    .gap_ms = -5,
                    .current_frame_number = 6,
                    .loop_count = UINT32_MAX,
                },
        },
    },
    {
        "a=a,i=1,s=4",
        {
            .kind = IMGNEKO_COMMAND_ANIMATION,
            .image_id = 1,
            .data.animation =
                {
                    .state = (ImgnekoAnimationStateUInt32)4,
                },
        },
        IMGNEKO_COMMAND_VALIDATION_ALLOW_UNKNOWN_VALUES,
        "unknown animation state (s=4)",
    },
    {
        "a=a,s=1",
        {
            .kind = IMGNEKO_COMMAND_ANIMATION,
            .data.animation.state = IMGNEKO_ANIMATION_STATE_STOPPED,
        },
        IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS,
        "animation command (a=a) requires an image identifier (i=...) or "
        "number (I=...)",
    },
    {
        "a=a,i=1,z=5",
        {
            .kind = IMGNEKO_COMMAND_ANIMATION,
            .image_id = 1,
            .data.animation =
                {
                    .gap_ms = 5,
                },
        },
        IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS,
        "animation gap (z=...) requires a frame number (r=...)",
    },
    // Accepting the extension state does not by itself waive the missing
    // frame-number relationship for the replacement gap.
    {
        "a=a,i=1,s=4,z=5",
        {
            .kind = IMGNEKO_COMMAND_ANIMATION,
            .image_id = 1,
            .data.animation =
                {
                    .state = (ImgnekoAnimationStateUInt32)4,
                    .gap_ms = 5,
                },
        },
        IMGNEKO_COMMAND_VALIDATION_ALLOW_UNKNOWN_VALUES |
            IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS,
        "unknown animation state (s=4)",
    },
    {
        "a=c,i=7,q=1,r=2,c=3,X=4,Y=5,x=6,y=7,w=8,h=9,C=1",
        {
            .kind = IMGNEKO_COMMAND_COMPOSE,
            .image_id = 7,
            .quietness = IMGNEKO_QUIETNESS_ERRORS_ONLY,
            .data.compose =
                {
                    .src_frame_number = 2,
                    .dst_frame_number = 3,
                    .src_x = 4,
                    .src_y = 5,
                    .dst_x = 6,
                    .dst_y = 7,
                    .width = 8,
                    .height = 9,
                    .composition = IMGNEKO_COMPOSITION_OVERWRITE,
                },
        },
    },
    {
        "a=c,i=1,r=2,c=3,w=1,h=1,C=2",
        {
            .kind = IMGNEKO_COMMAND_COMPOSE,
            .image_id = 1,
            .data.compose =
                {
                    .src_frame_number = 2,
                    .dst_frame_number = 3,
                    .width = 1,
                    .height = 1,
                    .composition = (ImgnekoCompositionModeUInt32)2,
                },
        },
        IMGNEKO_COMMAND_VALIDATION_ALLOW_UNKNOWN_VALUES,
        "unknown composition mode (C=2)",
    },
    {
        "a=c,r=2,c=3,w=1,h=1",
        {
            .kind = IMGNEKO_COMMAND_COMPOSE,
            .data.compose =
                {
                    .src_frame_number = 2,
                    .dst_frame_number = 3,
                    .width = 1,
                    .height = 1,
                },
        },
        IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS,
        "compose command (a=c) requires an image identifier (i=...) or number "
        "(I=...)",
    },
    {
        "a=c,i=1,c=3,w=1,h=1",
        {
            .kind = IMGNEKO_COMMAND_COMPOSE,
            .image_id = 1,
            .data.compose =
                {
                    .dst_frame_number = 3,
                    .width = 1,
                    .height = 1,
                },
        },
        IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS,
        "compose command (a=c) requires source (r=...) and destination "
        "(c=...) frames",
    },

    // Delete targets. Some headers put x/y before the action and target to
    // exercise the parser's postponed-pair scans.
    {
        "a=d,d=a",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .data.delete_cmd.target = IMGNEKO_DELETE_VISIBLE_PLACEMENTS,
        },
    },
    {
        "a=d,d=A",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .data.delete_cmd =
                {
                    .target = IMGNEKO_DELETE_VISIBLE_PLACEMENTS,
                    .delete_data = true,
                },
        },
    },
    // Semantically inactive fields remain serializable and parseable, but
    // strict relationship validation must reject them for the selected target.
    {
        "a=d,d=a,p=7",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .data.delete_cmd =
                {
                    .placement_id = 7,
                    .target = IMGNEKO_DELETE_VISIBLE_PLACEMENTS,
                },
        },
        IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS,
        "placement ID (p=...) requires delete-by-ID (d=i) or "
        "delete-by-number (d=n)",
    },
    {
        "a=d,d=a,i=1",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .image_id = 1,
            .data.delete_cmd.target = IMGNEKO_DELETE_VISIBLE_PLACEMENTS,
        },
        IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS,
        "image ID (i=...) is not used by delete target (d=a)",
    },
    {
        "a=d,d=a,I=1",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .image_number = 1,
            .data.delete_cmd.target = IMGNEKO_DELETE_VISIBLE_PLACEMENTS,
        },
        IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS,
        "image number (I=...) is not used by delete target (d=a)",
    },
    {
        "a=d,d=a,x=1",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .data.delete_cmd =
                {
                    .target = IMGNEKO_DELETE_VISIBLE_PLACEMENTS,
                    .x = 1,
                },
        },
        IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS,
        "x coordinate (x=...) is not used by delete target (d=a)",
    },
    {
        "a=d,d=a,y=1",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .data.delete_cmd =
                {
                    .target = IMGNEKO_DELETE_VISIBLE_PLACEMENTS,
                    .y = 1,
                },
        },
        IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS,
        "y coordinate (y=...) is not used by delete target (d=a)",
    },
    {
        "a=d,d=a,z=-1",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .data.delete_cmd =
                {
                    .target = IMGNEKO_DELETE_VISIBLE_PLACEMENTS,
                    .z_index = -1,
                },
        },
        IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS,
        "z-index (z=...) is not used by delete target (d=a)",
    },
    {
        "a=d,d=?",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .data.delete_cmd.target = (ImgnekoDeleteTargetChar)'?',
        },
        IMGNEKO_COMMAND_VALIDATION_ALLOW_UNKNOWN_VALUES,
        "unknown delete target (d=?)",
    },
    // Generic selector fields remain available to extension targets.
    {
        "a=d,i=1,d=?,p=2,x=3,y=4,z=5",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .image_id = 1,
            .data.delete_cmd =
                {
                    .placement_id = 2,
                    .target = (ImgnekoDeleteTargetChar)'?',
                    .x = 3,
                    .y = 4,
                    .z_index = 5,
                },
        },
        IMGNEKO_COMMAND_VALIDATION_ALLOW_UNKNOWN_VALUES,
        "unknown delete target (d=?)",
    },
    {
        "a=d,d==",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .data.delete_cmd.target = (ImgnekoDeleteTargetChar)'=',
        },
        IMGNEKO_COMMAND_VALIDATION_ALLOW_UNKNOWN_VALUES,
        "unknown delete target (d==)",
    },
    {
        "a=d,i=10,p=3,q=1,d=i",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .image_id = 10,
            .quietness = IMGNEKO_QUIETNESS_ERRORS_ONLY,
            .data.delete_cmd =
                {
                    .placement_id = 3,
                    .target = IMGNEKO_DELETE_IMAGE_OR_PLACEMENT_BY_ID,
                },
        },
    },
    {
        "a=d,i=10,p=3,d=I",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .image_id = 10,
            .data.delete_cmd =
                {
                    .placement_id = 3,
                    .target = IMGNEKO_DELETE_IMAGE_OR_PLACEMENT_BY_ID,
                    .delete_data = true,
                },
        },
    },
    {
        "a=d,d=i",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .data.delete_cmd.target = IMGNEKO_DELETE_IMAGE_OR_PLACEMENT_BY_ID,
        },
        IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS,
        "delete-by-ID target (d=i) requires an image ID (i=...)",
    },
    {
        "a=d,I=1,d=i",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .image_number = 1,
            .data.delete_cmd.target = IMGNEKO_DELETE_IMAGE_OR_PLACEMENT_BY_ID,
        },
        IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS,
        "delete-by-ID target (d=i) requires an image ID (i=...)",
    },
    {
        "a=d,i=1,I=2,d=i",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .image_id = 1,
            .image_number = 2,
            .data.delete_cmd =
                {
                    .target = IMGNEKO_DELETE_IMAGE_OR_PLACEMENT_BY_ID,
                },
        },
        IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS,
        "image ID (i=...) and image number (I=...) are both set",
    },
    {
        "a=d,I=11,p=4,d=n",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .image_number = 11,
            .data.delete_cmd =
                {
                    .placement_id = 4,
                    .target = IMGNEKO_DELETE_IMAGE_OR_PLACEMENT_BY_NUMBER,
                },
        },
    },
    {
        "a=d,I=11,d=N",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .image_number = 11,
            .data.delete_cmd =
                {
                    .target = IMGNEKO_DELETE_IMAGE_OR_PLACEMENT_BY_NUMBER,
                    .delete_data = true,
                },
        },
    },
    {
        "a=d,d=n",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .data.delete_cmd.target =
                IMGNEKO_DELETE_IMAGE_OR_PLACEMENT_BY_NUMBER,
        },
        IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS,
        "delete-by-number target (d=n) requires an image number (I=...)",
    },
    {
        "a=d,i=1,d=n",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .image_id = 1,
            .data.delete_cmd.target =
                IMGNEKO_DELETE_IMAGE_OR_PLACEMENT_BY_NUMBER,
        },
        IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS,
        "delete-by-number target (d=n) requires an image number (I=...)",
    },
    {
        "a=d,d=c",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .data.delete_cmd.target = IMGNEKO_DELETE_PLACEMENTS_AT_CURSOR,
        },
    },
    {
        "a=d,d=C",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .data.delete_cmd =
                {
                    .target = IMGNEKO_DELETE_PLACEMENTS_AT_CURSOR,
                    .delete_data = true,
                },
        },
    },
    {
        "a=d,i=12,d=f",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .image_id = 12,
            .data.delete_cmd =
                {
                    .target = IMGNEKO_DELETE_ANIMATION_FRAMES,
                },
        },
    },
    {
        "a=d,i=12,d=F",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .image_id = 12,
            .data.delete_cmd =
                {
                    .target = IMGNEKO_DELETE_ANIMATION_FRAMES,
                    .delete_data = true,
                },
        },
    },
    {
        "a=d,I=12,d=f",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .image_number = 12,
            .data.delete_cmd =
                {
                    .target = IMGNEKO_DELETE_ANIMATION_FRAMES,
                },
        },
    },
    {
        "a=d,d=f",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .data.delete_cmd.target = IMGNEKO_DELETE_ANIMATION_FRAMES,
        },
        IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS,
        "animation-frame deletion (d=f) requires an image identifier (i=...) "
        "or number (I=...)",
    },
    {
        "y=3,d=p,x=2,a=d",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .data.delete_cmd =
                {
                    .target = IMGNEKO_DELETE_PLACEMENTS_AT_POSITION,
                    .x = 2,
                    .y = 3,
                },
        },
    },
    {
        "a=d,d=P,x=2,y=3",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .data.delete_cmd =
                {
                    .target = IMGNEKO_DELETE_PLACEMENTS_AT_POSITION,
                    .delete_data = true,
                    .x = 2,
                    .y = 3,
                },
        },
    },
    // Zero leaves x unset, violating the position target's required-field
    // relationship.
    {
        "a=d,d=p,y=2",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .data.delete_cmd =
                {
                    .target = IMGNEKO_DELETE_PLACEMENTS_AT_POSITION,
                    .y = 2,
                },
        },
        IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS,
        "delete position target (d=p or d=q) requires positive x (x=...) and "
        "y (y=...)",
    },
    // Negative coordinates are representable, but accepting them requires both
    // allowing unknown values and skipping the positive-coordinate relationship
    {
        "a=d,d=p,x=-1,y=1",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .data.delete_cmd =
                {
                    .target = IMGNEKO_DELETE_PLACEMENTS_AT_POSITION,
                    .x = -1,
                    .y = 1,
                },
        },
        IMGNEKO_COMMAND_VALIDATION_ALLOW_UNKNOWN_VALUES |
            IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS,
        "delete position coordinates (x=... and y=...) must be positive",
    },
    {
        "z=0,y=3,d=q,x=2,a=d",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .data.delete_cmd =
                {
                    .target = IMGNEKO_DELETE_PLACEMENTS_AT_POSITION_AND_Z_INDEX,
                    .x = 2,
                    .y = 3,
                },
        },
    },
    // Each missing coordinate makes the z-index position target invalid, but
    // skipping relationship checks must still preserve the command.
    {
        "a=d,d=q,y=2,z=0",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .data.delete_cmd =
                {
                    .target = IMGNEKO_DELETE_PLACEMENTS_AT_POSITION_AND_Z_INDEX,
                    .y = 2,
                },
        },
        IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS,
        "delete position target (d=p or d=q) requires positive x (x=...) and "
        "y (y=...)",
    },
    {
        "a=d,d=q,x=2,z=0",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .data.delete_cmd =
                {
                    .target = IMGNEKO_DELETE_PLACEMENTS_AT_POSITION_AND_Z_INDEX,
                    .x = 2,
                },
        },
        IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS,
        "delete position target (d=p or d=q) requires positive x (x=...) and "
        "y (y=...)",
    },
    // Check each negative coordinate separately so either invalid axis rejects
    // the position-and-z-index target even when unknown values are allowed.
    {
        "a=d,d=q,x=-1,y=1,z=0",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .data.delete_cmd =
                {
                    .target = IMGNEKO_DELETE_PLACEMENTS_AT_POSITION_AND_Z_INDEX,
                    .x = -1,
                    .y = 1,
                },
        },
        IMGNEKO_COMMAND_VALIDATION_ALLOW_UNKNOWN_VALUES |
            IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS,
        "delete position coordinates (x=... and y=...) must be positive",
    },
    {
        "a=d,d=q,x=1,y=-1,z=0",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .data.delete_cmd =
                {
                    .target = IMGNEKO_DELETE_PLACEMENTS_AT_POSITION_AND_Z_INDEX,
                    .x = 1,
                    .y = -1,
                },
        },
        IMGNEKO_COMMAND_VALIDATION_ALLOW_UNKNOWN_VALUES |
            IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS,
        "delete position coordinates (x=... and y=...) must be positive",
    },
    {
        "a=d,d=Q,x=2,y=3,z=-4",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .data.delete_cmd =
                {
                    .target = IMGNEKO_DELETE_PLACEMENTS_AT_POSITION_AND_Z_INDEX,
                    .delete_data = true,
                    .x = 2,
                    .y = 3,
                    .z_index = -4,
                },
        },
    },
    {
        "x=20,y=30,d=r,a=d",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .data.delete_cmd =
                {
                    .target = IMGNEKO_DELETE_IMAGES_BY_ID_RANGE,
                    .first_image_id = 20,
                    .last_image_id = 30,
                },
        },
    },
    {
        "a=d,d=R,x=20,y=30",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .data.delete_cmd =
                {
                    .target = IMGNEKO_DELETE_IMAGES_BY_ID_RANGE,
                    .delete_data = true,
                    .first_image_id = 20,
                    .last_image_id = 30,
                },
        },
    },
    // A single-image inclusive range is valid.
    {
        "a=d,d=r,x=20,y=20",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .data.delete_cmd =
                {
                    .target = IMGNEKO_DELETE_IMAGES_BY_ID_RANGE,
                    .first_image_id = 20,
                    .last_image_id = 20,
                },
        },
    },
    {
        "a=d,d=r,y=1",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .data.delete_cmd =
                {
                    .target = IMGNEKO_DELETE_IMAGES_BY_ID_RANGE,
                    .last_image_id = 1,
                },
        },
        IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS,
        "delete image ID range target (d=r) requires positive endpoints "
        "(x=... and y=...)",
    },
    {
        "a=d,d=r,x=1",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .data.delete_cmd =
                {
                    .target = IMGNEKO_DELETE_IMAGES_BY_ID_RANGE,
                    .first_image_id = 1,
                },
        },
        IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS,
        "delete image ID range target (d=r) requires positive endpoints "
        "(x=... and y=...)",
    },
    {
        "a=d,d=r,x=30,y=20",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .data.delete_cmd =
                {
                    .target = IMGNEKO_DELETE_IMAGES_BY_ID_RANGE,
                    .first_image_id = 30,
                    .last_image_id = 20,
                },
        },
        IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS,
        "delete image ID range is reversed (x=... > y=...)",
    },
    {
        "a=d,d=x,x=4",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .data.delete_cmd =
                {
                    .target = IMGNEKO_DELETE_PLACEMENTS_AT_COLUMN,
                    .x = 4,
                },
        },
    },
    {
        "a=d,d=X,x=4",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .data.delete_cmd =
                {
                    .target = IMGNEKO_DELETE_PLACEMENTS_AT_COLUMN,
                    .delete_data = true,
                    .x = 4,
                },
        },
    },
    {
        "a=d,d=x",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .data.delete_cmd.target = IMGNEKO_DELETE_PLACEMENTS_AT_COLUMN,
        },
        IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS,
        "delete-column target (d=x) requires a positive column (x=...)",
    },
    // A negative column must also fail the target's relationship check.
    {
        "a=d,d=x,x=-1",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .data.delete_cmd =
                {
                    .target = IMGNEKO_DELETE_PLACEMENTS_AT_COLUMN,
                    .x = -1,
                },
        },
        IMGNEKO_COMMAND_VALIDATION_ALLOW_UNKNOWN_VALUES |
            IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS,
        "delete position coordinates (x=... and y=...) must be positive",
    },
    {
        "a=d,d=y,y=5",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .data.delete_cmd =
                {
                    .target = IMGNEKO_DELETE_PLACEMENTS_AT_ROW,
                    .y = 5,
                },
        },
    },
    {
        "a=d,d=Y,y=5",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .data.delete_cmd =
                {
                    .target = IMGNEKO_DELETE_PLACEMENTS_AT_ROW,
                    .delete_data = true,
                    .y = 5,
                },
        },
    },
    {
        "a=d,d=y",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .data.delete_cmd.target = IMGNEKO_DELETE_PLACEMENTS_AT_ROW,
        },
        IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS,
        "delete-row target (d=y) requires a positive row (y=...)",
    },
    // A negative row must also fail the target's relationship check.
    {
        "a=d,d=y,y=-1",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .data.delete_cmd =
                {
                    .target = IMGNEKO_DELETE_PLACEMENTS_AT_ROW,
                    .y = -1,
                },
        },
        IMGNEKO_COMMAND_VALIDATION_ALLOW_UNKNOWN_VALUES |
            IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS,
        "delete position coordinates (x=... and y=...) must be positive",
    },
    {
        "a=d,d=z,z=0",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .data.delete_cmd =
                {
                    .target = IMGNEKO_DELETE_PLACEMENTS_AT_Z_INDEX,
                },
        },
    },
    {
        "a=d,d=Z,z=-6",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .data.delete_cmd =
                {
                    .target = IMGNEKO_DELETE_PLACEMENTS_AT_Z_INDEX,
                    .delete_data = true,
                    .z_index = -6,
                },
        },
    },

    // Coverage-oriented boundary cases exercise the right-hand operands of
    // relationship checks whose left-hand operands are covered above.
    {
        "a=t,t=t,O=1",
        {
            .kind = IMGNEKO_COMMAND_TRANSMIT,
            .data.transmit.transmission =
                {
                    .medium = IMGNEKO_TRANSMISSION_MEDIUM_TEMP_FILE,
                    .data_offset = 1,
                },
        },
    },
    {
        "a=t,t=s,O=1",
        {
            .kind = IMGNEKO_COMMAND_TRANSMIT,
            .data.transmit.transmission =
                {
                    .medium = IMGNEKO_TRANSMISSION_MEDIUM_SHARED_MEMORY,
                    .data_offset = 1,
                },
        },
    },
    {
        "a=t,t=d,f=32,v=1",
        {
            .kind = IMGNEKO_COMMAND_TRANSMIT,
            .data.transmit.transmission =
                {
                    .medium = IMGNEKO_TRANSMISSION_MEDIUM_DIRECT,
                    .format = IMGNEKO_IMAGE_FORMAT_RGBA,
                    .pixel_height = 1,
                },
        },
        IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS,
        "raw image format (f=24 or f=32) requires pixel width (s=...) and "
        "height (v=...)",
    },
    {
        "a=p,i=1,y=-3",
        {
            .kind = IMGNEKO_COMMAND_PUT,
            .image_id = 1,
            .data.put.placement.src_y = -3,
        },
        IMGNEKO_COMMAND_VALIDATION_ALLOW_UNKNOWN_VALUES,
        "placement source coordinates (x=... and y=...) must be nonnegative",
    },
    {
        "a=p,i=1,V=-3",
        {
            .kind = IMGNEKO_COMMAND_PUT,
            .image_id = 1,
            .data.put.placement.parent_y_offset = -3,
        },
        IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS,
        "parent offsets (H=... or V=...) require a parent image ID (P=...)",
    },
    {
        "a=c,i=1,r=1",
        {
            .kind = IMGNEKO_COMMAND_COMPOSE,
            .image_id = 1,
            .data.compose.src_frame_number = 1,
        },
        IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS,
        "compose command (a=c) requires source (r=...) and destination "
        "(c=...) frames",
    },
    {
        "a=d,d=p,x=2",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .data.delete_cmd =
                {
                    .target = IMGNEKO_DELETE_PLACEMENTS_AT_POSITION,
                    .x = 2,
                },
        },
        IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS,
        "delete position target (d=p or d=q) requires positive x (x=...) and "
        "y (y=...)",
    },
    {
        "a=d,d=p,x=1,y=-1",
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .data.delete_cmd =
                {
                    .target = IMGNEKO_DELETE_PLACEMENTS_AT_POSITION,
                    .x = 1,
                    .y = -1,
                },
        },
        IMGNEKO_COMMAND_VALIDATION_ALLOW_UNKNOWN_VALUES |
            IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS,
        "delete position coordinates (x=... and y=...) must be positive",
    },
};

//===----------------------------------------------------------------------===//
// Unrepresentable command cases
//===----------------------------------------------------------------------===//

// Commands whose fields cannot produce a protocol header. Required UNSET
// sentinels cannot be replaced by implicit defaults. Discriminator fields use
// byte-sized storage, but commas and payload-separator semicolons still cannot
// be encoded as single-value protocol tokens.
static const UnrepresentableCommandCase unrepresentable_command_cases[] = {
    {
        {
            .kind = IMGNEKO_COMMAND_UNSET,
            .kind_implicit = true,
        },
        "command kind is unset (a=0)",
    },
    {
        {
            .kind = IMGNEKO_COMMAND_TRANSMIT,
            .data.transmit.transmission =
                {
                    .medium = IMGNEKO_TRANSMISSION_MEDIUM_UNSET,
                    .medium_implicit = true,
                },
        },
        "transmission medium is unset (t=0)",
    },
    {
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .data.delete_cmd =
                {
                    .target = IMGNEKO_DELETE_UNSET,
                    .target_implicit = true,
                },
        },
        "delete target is unset (d=0)",
    },
    // The protocol reuses x/y for position coordinates and image ID range
    // endpoints. Values stored in the wrong discriminator-selected fields
    // cannot be serialized without changing the command parsed back. Separate
    // cases exercise both operands of each representability check.
    {
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .data.delete_cmd =
                {
                    .target = IMGNEKO_DELETE_VISIBLE_PLACEMENTS,
                    .first_image_id = 1,
                    .last_image_id = 2,
                },
        },
        "image ID range endpoints require delete-range target (d=r)",
    },
    {
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .data.delete_cmd =
                {
                    .target = IMGNEKO_DELETE_VISIBLE_PLACEMENTS,
                    .last_image_id = 2,
                },
        },
        "image ID range endpoints require delete-range target (d=r)",
    },
    {
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .data.delete_cmd =
                {
                    .target = IMGNEKO_DELETE_IMAGES_BY_ID_RANGE,
                    .x = 1,
                    .y = 2,
                },
        },
        "delete position coordinates cannot be represented for delete-range "
        "target (d=r)",
    },
    {
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .data.delete_cmd =
                {
                    .target = IMGNEKO_DELETE_IMAGES_BY_ID_RANGE,
                    .y = 2,
                },
        },
        "delete position coordinates cannot be represented for delete-range "
        "target (d=r)",
    },
    {
        {
            .kind = (ImgnekoCommandKindChar)',',
        },
        "command kind is not representable (a=,)",
    },
    {
        {
            .kind = (ImgnekoCommandKindChar)';',
        },
        "command kind is not representable (a=;)",
    },
    {
        {
            .kind = IMGNEKO_COMMAND_TRANSMIT,
            .data.transmit.transmission.medium =
                (ImgnekoTransmissionMediumChar)',',
        },
        "transmission medium is not representable (t=,)",
    },
    {
        {
            .kind = IMGNEKO_COMMAND_TRANSMIT,
            .data.transmit.transmission.medium =
                (ImgnekoTransmissionMediumChar)';',
        },
        "transmission medium is not representable (t=;)",
    },
    {
        {
            .kind = IMGNEKO_COMMAND_TRANSMIT,
            .data.transmit.transmission =
                {
                    .medium = IMGNEKO_TRANSMISSION_MEDIUM_DIRECT,
                    .compression = (ImgnekoPayloadCompressionChar)',',
                },
        },
        "payload compression is not representable (o=,)",
    },
    {
        {
            .kind = IMGNEKO_COMMAND_TRANSMIT,
            .data.transmit.transmission =
                {
                    .medium = IMGNEKO_TRANSMISSION_MEDIUM_DIRECT,
                    .compression = (ImgnekoPayloadCompressionChar)';',
                },
        },
        "payload compression is not representable (o=;)",
    },
    {
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .data.delete_cmd.target = (ImgnekoDeleteTargetChar)',',
        },
        "delete target is not representable (d=,)",
    },
    {
        {
            .kind = IMGNEKO_COMMAND_DELETE,
            .data.delete_cmd.target = (ImgnekoDeleteTargetChar)';',
        },
        "delete target is not representable (d=;)",
    },
};

//===----------------------------------------------------------------------===//
// Unparseable serialized command cases
//===----------------------------------------------------------------------===//

// Header tokenization errors cannot be relaxed by parse policy flags.
static const UnparseableCommandCase unparseable_command_cases[] = {
    // Syntax errors are rejected before any fields are interpreted.
    {
        "a",
        "header pair is missing an equals sign",
    },
    {
        ",a=t",
        "header contains an empty pair",
    },
    {
        "a=t,",
        "header contains an empty pair",
    },
    {
        "a=t,,t=d",
        "header contains an empty pair",
    },
    // A malformed pair remains a scanner error alongside otherwise valid
    // pairs, regardless of where it occurs in the header.
    {
        "broken,a=t",
        "header pair is missing an equals sign",
    },
    {
        "a=t,broken,t=d",
        "header pair is missing an equals sign",
    },
    // The complete syntax pass must reject a later malformed pair before the
    // duplicate policy considers an earlier repeated key.
    {
        "a=t,a=p,broken",
        "header pair is missing an equals sign",
    },
    // The payload remains opaque, but does not make a malformed header valid.
    {
        "a=t,broken;opaque=payload",
        "header pair is missing an equals sign",
    },
    {
        "a=p,i=1,=2",
        "header pair has an empty key",
    },
};

//===----------------------------------------------------------------------===//
// Conditionally parseable serialized command cases
//===----------------------------------------------------------------------===//

// These inputs require selected parsing permissions. Their expected headers
// describe the normalized result, which may differ when pairs are discarded.
static const ConditionalParseCase conditional_parse_cases[] = {
    {
        "a=p,i=1,unknown=2",
        "a=p,i=1",
        IMGNEKO_COMMAND_PARSE_DROP_UNKNOWN_KEYS,
        "unknown graphics command key 'unknown'",
    },
    // The transmit-only `f` key is placed before `a=p` to exercise a postponed
    // pair that becomes contextually unknown once the action is resolved.
    {
        "f=100,a=p,i=1",
        "a=p,i=1",
        IMGNEKO_COMMAND_PARSE_DROP_UNKNOWN_KEYS,
        "unknown graphics command key 'f'",
    },
    // With no `a` pair, `p` remains postponed until the parser selects the
    // implicit transmit action, for which `p` is contextually unknown.
    {
        "p=2,i=1",
        "i=1",
        IMGNEKO_COMMAND_PARSE_DROP_UNKNOWN_KEYS,
        "unknown graphics command key 'p'",
    },

    // Representable unknown discriminators are always preserved. Keys whose
    // storage cannot be selected without understanding them remain unknown.
    {
        "a=?,i=1,q=1,f=100",
        "a=?,i=1,q=1",
        IMGNEKO_COMMAND_PARSE_DROP_UNKNOWN_KEYS,
        "unknown graphics command key 'f'",
    },

    // Unrepresentable discriminators use the same drop policy as other fields
    // and fall back to the corresponding protocol default.
    {
        "a=",
        "",
        IMGNEKO_COMMAND_PARSE_DROP_UNREPRESENTABLE_VALUES,
        "command kind is not representable",
    },
    {
        "a=t,t=",
        "a=t",
        IMGNEKO_COMMAND_PARSE_DROP_UNREPRESENTABLE_VALUES,
        "transmission medium is not representable",
    },
    {
        "a=d,d=",
        "a=d",
        IMGNEKO_COMMAND_PARSE_DROP_UNREPRESENTABLE_VALUES,
        "delete target is not representable",
    },

    // Exercise several forms of unrepresentable known values: alphabetic and
    // empty numeric input, signed and unsigned range errors, and an overlong
    // character value.
    {
        "a=t,i=1,t=d,q=loud",
        "a=t,i=1,t=d",
        IMGNEKO_COMMAND_PARSE_DROP_UNREPRESENTABLE_VALUES,
        "quietness value is not representable",
    },
    {
        "a=t,i=1,t=d,q=x",
        "a=t,i=1,t=d",
        IMGNEKO_COMMAND_PARSE_DROP_UNREPRESENTABLE_VALUES,
        "quietness value is not representable",
    },
    // A leading plus sign reaches the decimal parser's below-'0' rejection;
    // graphics command integers accept no explicit positive sign.
    {
        "a=t,i=1,t=d,q=+1",
        "a=t,i=1,t=d",
        IMGNEKO_COMMAND_PARSE_DROP_UNREPRESENTABLE_VALUES,
        "quietness value is not representable",
    },
    {
        "a=t,i=1,t=d,q=",
        "a=t,i=1,t=d",
        IMGNEKO_COMMAND_PARSE_DROP_UNREPRESENTABLE_VALUES,
        "quietness value is not representable",
    },
    {
        "a=t,i=1,t=d,q=4294967296",
        "a=t,i=1,t=d",
        IMGNEKO_COMMAND_PARSE_DROP_UNREPRESENTABLE_VALUES,
        "quietness value is not representable",
    },
    {
        "a=t,t=d,i=-1",
        "a=t,t=d",
        IMGNEKO_COMMAND_PARSE_DROP_UNREPRESENTABLE_VALUES,
        "image ID is not representable",
    },
    {
        "a=t,t=d,i=-0",
        "a=t,t=d",
        IMGNEKO_COMMAND_PARSE_DROP_UNREPRESENTABLE_VALUES,
        "image ID is not representable",
    },
    {
        "a=t,t=d,i=4294967296",
        "a=t,t=d",
        IMGNEKO_COMMAND_PARSE_DROP_UNREPRESENTABLE_VALUES,
        "image ID is not representable",
    },
    // Exercise the shared decimal parser at and beyond its signed 64-bit
    // limits before the narrower destination checks reject the values.
    {
        "a=t,t=d,i=9223372036854775808",
        "a=t,t=d",
        IMGNEKO_COMMAND_PARSE_DROP_UNREPRESENTABLE_VALUES,
        "image ID is not representable",
    },
    {
        "a=p,i=1,z=-2147483649",
        "a=p,i=1",
        IMGNEKO_COMMAND_PARSE_DROP_UNREPRESENTABLE_VALUES,
        "placement z-index is not representable",
    },
    {
        "a=p,i=1,z=-9223372036854775808",
        "a=p,i=1",
        IMGNEKO_COMMAND_PARSE_DROP_UNREPRESENTABLE_VALUES,
        "placement z-index is not representable",
    },
    {
        "a=p,i=1,z=-9223372036854775809",
        "a=p,i=1",
        IMGNEKO_COMMAND_PARSE_DROP_UNREPRESENTABLE_VALUES,
        "placement z-index is not representable",
    },
    {
        "a=p,i=1,z=2147483648",
        "a=p,i=1",
        IMGNEKO_COMMAND_PARSE_DROP_UNREPRESENTABLE_VALUES,
        "placement z-index is not representable",
    },
    {
        "a=p,i=1,z=",
        "a=p,i=1",
        IMGNEKO_COMMAND_PARSE_DROP_UNREPRESENTABLE_VALUES,
        "placement z-index is not representable",
    },
    {
        "a=p,i=1,z=-",
        "a=p,i=1",
        IMGNEKO_COMMAND_PARSE_DROP_UNREPRESENTABLE_VALUES,
        "placement z-index is not representable",
    },
    {
        "a=p,i=1,z=x",
        "a=p,i=1",
        IMGNEKO_COMMAND_PARSE_DROP_UNREPRESENTABLE_VALUES,
        "placement z-index is not representable",
    },
    {
        "a=t,t=d,o=",
        "a=t,t=d",
        IMGNEKO_COMMAND_PARSE_DROP_UNREPRESENTABLE_VALUES,
        "payload compression is not representable",
    },
    // Unknown keys in each command-specific switch exercise the mapping's
    // fallthrough paths before the generic unknown-key policy handles them.
    {
        "a=f,i=1,t=d,f=32,s=1,v=1,k=1",
        "a=f,i=1,t=d,f=32,s=1,v=1",
        IMGNEKO_COMMAND_PARSE_DROP_UNKNOWN_KEYS,
        "unknown graphics command key 'k'",
    },
    {
        "a=a,i=1,k=1",
        "a=a,i=1",
        IMGNEKO_COMMAND_PARSE_DROP_UNKNOWN_KEYS,
        "unknown graphics command key 'k'",
    },
    {
        "a=c,i=1,r=1,c=2,k=1",
        "a=c,i=1,r=1,c=2",
        IMGNEKO_COMMAND_PARSE_DROP_UNKNOWN_KEYS,
        "unknown graphics command key 'k'",
    },
    {
        "a=d,k=1",
        "a=d",
        IMGNEKO_COMMAND_PARSE_DROP_UNKNOWN_KEYS,
        "unknown graphics command key 'k'",
    },
    // The range target has a preliminary x/y switch before the general delete
    // fields, so an unknown key must fall through both mappings.
    {
        "a=d,d=r,k=1",
        "a=d,d=r",
        IMGNEKO_COMMAND_PARSE_DROP_UNKNOWN_KEYS,
        "unknown graphics command key 'k'",
    },
    // The delete-target key is contextual and remains unknown for non-delete
    // commands instead of being mistaken for a discriminator.
    {
        "a=p,i=1,d=i",
        "a=p,i=1",
        IMGNEKO_COMMAND_PARSE_DROP_UNKNOWN_KEYS,
        "unknown graphics command key 'd'",
    },
    // Duplicate handling keeps the first occurrence, including discriminators
    // and malformed later occurrences that must not be interpreted.
    {
        "a=p,i=1,i=2",
        "a=p,i=1",
        IMGNEKO_COMMAND_PARSE_DROP_DUPLICATE_KEYS,
        "duplicate key",
    },
    {
        "i=1,i=2,a=p",
        "a=p,i=1",
        IMGNEKO_COMMAND_PARSE_DROP_DUPLICATE_KEYS,
        "duplicate key",
    },
    {
        "a=p,i=1,i=not-a-number",
        "a=p,i=1",
        IMGNEKO_COMMAND_PARSE_DROP_DUPLICATE_KEYS,
        "duplicate key",
    },
    {
        "a=p,a=t,i=1",
        "a=p,i=1",
        IMGNEKO_COMMAND_PARSE_DROP_DUPLICATE_KEYS,
        "duplicate key",
    },
    {
        "a=t,t=d,t=f",
        "a=t,t=d",
        IMGNEKO_COMMAND_PARSE_DROP_DUPLICATE_KEYS,
        "duplicate key",
    },
    {
        "d=i,d=n,a=d,i=1",
        "a=d,i=1,d=i",
        IMGNEKO_COMMAND_PARSE_DROP_DUPLICATE_KEYS,
        "duplicate key",
    },
    // Multi-policy cases ensure each requested structural flag is independently
    // necessary.
    {
        "a=t,t=d,q=9,unknown=2",
        "a=t,t=d,q=9",
        IMGNEKO_COMMAND_PARSE_DROP_UNKNOWN_KEYS,
        "unknown graphics command key 'unknown'",
    },
    {
        "a=p,q=loud",
        "a=p",
        IMGNEKO_COMMAND_PARSE_DROP_UNREPRESENTABLE_VALUES,
        "quietness value is not representable",
    },
    {
        "a=p,i=1,q=9,q=1",
        "a=p,i=1,q=9",
        IMGNEKO_COMMAND_PARSE_DROP_DUPLICATE_KEYS,
        "duplicate key",
    },
    {
        "a=p,i=1,i=2,q=9,p=wide,unknown=2",
        "a=p,i=1,q=9",
        IMGNEKO_COMMAND_PARSE_DROP_UNKNOWN_KEYS |
            IMGNEKO_COMMAND_PARSE_DROP_UNREPRESENTABLE_VALUES |
            IMGNEKO_COMMAND_PARSE_DROP_DUPLICATE_KEYS,
        "duplicate key",
        "duplicate key 'i'\n"
        "placement ID is not representable (p)\n"
        "unknown graphics command key 'unknown'",
    },
    // Duplicate policy is applied during the parsing scan. Therefore an
    // earlier unknown key is reported first unless both conditions are
    // explicitly dropped.
    {
        "unknown=2,i=1,i=2,a=p",
        "a=p,i=1",
        IMGNEKO_COMMAND_PARSE_DROP_UNKNOWN_KEYS |
            IMGNEKO_COMMAND_PARSE_DROP_DUPLICATE_KEYS,
        "unknown graphics command key 'unknown'",
        "unknown graphics command key 'unknown'\n"
        "duplicate key 'i'",
    },
};

// Protocol defaults parse successfully and serialize to their command-specific
// canonical form. Most zero values are omitted, while delete z-index selectors
// are made explicit.
static const DefaultNormalizationCase default_normalization_cases[] = {
    {
        "a=t,t=d,q=0",
        "a=t,t=d",
    },
    {
        "a=t,t=d,m=0",
        "a=t,t=d",
    },
    {
        "a=p,i=1,C=0",
        "a=p,i=1",
    },
    {
        "a=p,i=1,U=0",
        "a=p,i=1",
    },
    {
        "a=f,i=1,t=d,f=32,s=1,v=1,X=0",
        "a=f,i=1,t=d,f=32,s=1,v=1",
    },
    {
        "a=c,i=1,r=1,c=2,w=1,h=1,C=0",
        "a=c,i=1,r=1,c=2,w=1,h=1",
    },
    // Both z-index delete targets default a missing z key to zero, then make
    // that selector explicit in their canonical serialized headers.
    {
        "a=d,d=q,x=2,y=3",
        "a=d,d=q,x=2,y=3,z=0",
    },
    {
        "a=d,d=z",
        "a=d,d=z,z=0",
    },
};

//===----------------------------------------------------------------------===//
// Test case runners and helpers
//===----------------------------------------------------------------------===//

// All public parse-policy flags, used to exercise every structural policy
// combination.
static const unsigned all_parse_flags =
    IMGNEKO_COMMAND_PARSE_DROP_UNKNOWN_KEYS |
    IMGNEKO_COMMAND_PARSE_DROP_UNREPRESENTABLE_VALUES |
    IMGNEKO_COMMAND_PARSE_DROP_DUPLICATE_KEYS;
static const unsigned all_validation_flags =
    IMGNEKO_COMMAND_VALIDATION_ALLOW_UNKNOWN_VALUES |
    IMGNEKO_COMMAND_VALIDATION_SKIP_RELATIONSHIPS;

// Check that a diagnostic contains its stable expected fragment.
//
// `ctx`
//     Test context used for diagnostics.
// `test_case`
//     Command case supplying the diagnostic fragment and case identifier.
// `operation`
//     Name of the operation that produced the diagnostic.
// `message`
//     Null-terminated diagnostic produced by the operation.
static int expect_error_part(TestContext *ctx, const CommandCase *test_case,
                             const char *operation, const char *message) {
    if (test_case->error_part == NULL) {
        fprintf(stderr, "%s: command \"%s\": missing expected %s error part\n",
                ctx->test_name, test_case->expected_tokens, operation);
        return 1;
    }
    if (strstr(message, test_case->error_part) == NULL) {
        fprintf(stderr,
                "%s: command \"%s\": %s error does not contain %s: %s\n",
                ctx->test_name, test_case->expected_tokens, operation,
                test_case->error_part, message);
        return 1;
    }

    return 0;
}

// Check that an unrepresentable-command diagnostic contains its expected
// fragment.
//
// `ctx`
//     Test context used for diagnostics.
// `test_case`
//     Unrepresentable command and its expected diagnostic fragment.
// `operation`
//     Name of the operation that produced the diagnostic.
// `message`
//     Null-terminated diagnostic produced by the operation.
static int
expect_unrepresentable_error_part(TestContext *ctx,
                                  const UnrepresentableCommandCase *test_case,
                                  const char *operation, const char *message) {
    if (test_case->error_part == NULL) {
        fprintf(stderr,
                "%s: unrepresentable command has no expected %s error part\n",
                ctx->test_name, operation);
        return 1;
    }
    if (strstr(message, test_case->error_part) == NULL) {
        fprintf(stderr,
                "%s: unrepresentable command \"%s\": %s error does not "
                "contain its expected part: %s\n",
                ctx->test_name, test_case->error_part, operation, message);
        return 1;
    }

    return 0;
}

// Serialize a command and compare its header without assuming token order.
static int serialize_case(TestContext *ctx, const CommandCase *test_case) {
    char serialized[IMGNEKO_COMMAND_MAX_SIZE];
    char message[256];
    size_t serialized_len = SIZE_MAX;
    ImgnekoErrorDetail detail = {
        .message = message,
        .message_cap = sizeof(message),
        .message_len = SIZE_MAX,
    };

    memset(serialized, 'x', sizeof(serialized));
    memset(message, 'x', sizeof(message));
    ImgnekoCommandError status = imgneko_command_header_to_buffer(
        &test_case->command, serialized, sizeof(serialized), &serialized_len,
        &detail);
    if (status != IMGNEKO_COMMAND_OK) {
        fprintf(stderr,
                "%s: command \"%s\": serialization returned %d (%.*s)\n",
                ctx->test_name, test_case->expected_tokens, status,
                (int)sizeof(message), message);
        return 1;
    }
    if (detail.message_len != 0 || message[0] != '\0')
        return test_fail_message(ctx, "serialization left a diagnostic");

    return test_expect_graphics_command_header_tokens(
        ctx, str_span(serialized, serialized_len),
        str_span(test_case->expected_tokens,
                 strlen(test_case->expected_tokens)),
        test_case->expected_tokens);
}

// Require validation to fail under a flag set missing a necessary permission.
static int expect_validation_failure(TestContext *ctx,
                                     const CommandCase *test_case,
                                     ImgnekoCommandValidationFlags flags) {
    char message[256];
    ImgnekoErrorDetail detail = {
        .message = message,
        .message_cap = sizeof(message),
        .message_len = SIZE_MAX,
    };

    memset(message, 'x', sizeof(message));
    ImgnekoCommandError status =
        imgneko_command_validate(&test_case->command, flags, &detail);
    if (status != IMGNEKO_COMMAND_INVALID_FIELD) {
        fprintf(stderr,
                "%s: command \"%s\": validation with flags %#x returned %d, "
                "expected failure\n",
                ctx->test_name, test_case->expected_tokens, (unsigned)flags,
                status);
        return 1;
    }
    if (flags == IMGNEKO_COMMAND_VALIDATION_FLAGS_NONE)
        return expect_error_part(ctx, test_case, "validation", message);

    return 0;
}

// Require validation to succeed and leave no diagnostic.
static int expect_validation_success(TestContext *ctx,
                                     const CommandCase *test_case,
                                     ImgnekoCommandValidationFlags flags) {
    char message[256];
    ImgnekoErrorDetail detail = {
        .message = message,
        .message_cap = sizeof(message),
        .message_len = SIZE_MAX,
    };

    memset(message, 'x', sizeof(message));
    ImgnekoCommandError status =
        imgneko_command_validate(&test_case->command, flags, &detail);
    if (status != IMGNEKO_COMMAND_OK) {
        fprintf(stderr,
                "%s: command \"%s\": validation with flags %#x returned %d "
                "(%.*s)\n",
                ctx->test_name, test_case->expected_tokens, (unsigned)flags,
                status, (int)sizeof(message), message);
        return 1;
    }
    if (detail.message_len != 0 || message[0] != '\0')
        return test_fail_message(ctx, "validation left a diagnostic");

    return 0;
}

// Check that every required validation flag is necessary and every superset of
// those flags still succeeds.
static int validate_case(TestContext *ctx, const CommandCase *test_case) {
    unsigned required_flags = (unsigned)test_case->validation_flags;
    if (required_flags == 0 && test_case->error_part != NULL)
        return test_fail_message(ctx, "valid case has a validation error part");

    if (required_flags != 0) {
        if (expect_validation_failure(ctx, test_case,
                                      IMGNEKO_COMMAND_VALIDATION_FLAGS_NONE))
            return 1;

        // Removing each required bit in turn must leave an invalid command.
        for (unsigned bit = 1; bit != 0; bit <<= 1) {
            if ((required_flags & bit) == 0)
                continue;

            unsigned partial_flags = required_flags & ~bit;
            if (partial_flags != 0 &&
                expect_validation_failure(
                    ctx, test_case,
                    (ImgnekoCommandValidationFlags)partial_flags))
                return 1;
        }
    }

    // Enumerate every subset of optional flags and combine it with the
    // required flags, including the empty and complete optional subsets.
    unsigned optional_flags = all_validation_flags & ~required_flags;
    for (unsigned added_flags = optional_flags;;
         added_flags = (added_flags - 1) & optional_flags) {
        ImgnekoCommandValidationFlags flags =
            (ImgnekoCommandValidationFlags)(required_flags | added_flags);
        if (expect_validation_success(ctx, test_case, flags))
            return 1;
        if (added_flags == 0)
            break;
    }

    return 0;
}

// Require parsing to succeed, preserve exact input spans and command fields,
// and leave no diagnostic.
static int expect_parse_success(TestContext *ctx, const CommandCase *test_case,
                                ImgnekoCommandParseFlags flags) {
    char message[256];
    ImgnekoErrorDetail detail = {
        .message = message,
        .message_cap = sizeof(message),
        .message_len = SIZE_MAX,
    };
    ImgnekoCommandParseOptions options = {
        .start_sequence = "",
        .end_sequence = "",
        .flags = flags,
    };
    ImgnekoParsedCommand parsed;

    memset(&parsed, 0xa5, sizeof(parsed));
    memset(message, 'x', sizeof(message));
    ImgnekoCommandParseError status = imgneko_command_parse(
        test_case->expected_tokens, strlen(test_case->expected_tokens),
        &options, &parsed, &detail);
    if (status != IMGNEKO_COMMAND_PARSE_OK) {
        fprintf(stderr,
                "%s: command \"%s\": parsing with flags %#x returned %d "
                "(%.*s)\n",
                ctx->test_name, test_case->expected_tokens, (unsigned)flags,
                status, (int)sizeof(message), message);
        return 1;
    }
    if (detail.message_len != 0 || message[0] != '\0')
        return test_fail_message(ctx, "parsing left a diagnostic");
    if (parsed.header_len != strlen(test_case->expected_tokens) ||
        (parsed.header_len != 0 &&
         memcmp(parsed.header, test_case->expected_tokens, parsed.header_len) !=
             0) ||
        parsed.payload != NULL || parsed.payload_len != 0)
        return test_fail_message(ctx, "parser returned wrong input spans");
    if (memcmp(&parsed.command, &test_case->command,
               sizeof(test_case->command)) != 0) {
        fprintf(stderr, "%s: command \"%s\": parsed command differs\n",
                ctx->test_name, test_case->expected_tokens);
        return 1;
    }

    return 0;
}

// Every representable command must parse without semantic relaxation flags.
static int parse_case(TestContext *ctx, const CommandCase *test_case) {
    return expect_parse_success(ctx, test_case,
                                IMGNEKO_COMMAND_PARSE_FLAGS_NONE);
}

// Require header serialization to reject a command that has no protocol
// representation and report its expected diagnostic.
static int
serialize_unrepresentable_case(TestContext *ctx,
                               const UnrepresentableCommandCase *test_case) {
    char serialized[IMGNEKO_COMMAND_MAX_SIZE];
    char message[256];
    size_t serialized_len = SIZE_MAX;
    ImgnekoErrorDetail detail = {
        .message = message,
        .message_cap = sizeof(message),
        .message_len = SIZE_MAX,
    };

    memset(serialized, 'x', sizeof(serialized));
    memset(message, 'x', sizeof(message));
    ImgnekoCommandError status = imgneko_command_header_to_buffer(
        &test_case->command, serialized, sizeof(serialized), &serialized_len,
        &detail);
    if (status != IMGNEKO_COMMAND_INVALID_FIELD) {
        fprintf(stderr,
                "%s: unrepresentable command \"%s\": serialization "
                "returned %d\n",
                ctx->test_name, test_case->error_part, status);
        return 1;
    }
    if (serialized_len != 0) {
        fprintf(stderr,
                "%s: unrepresentable command \"%s\": failed serialization "
                "returned length %zu\n",
                ctx->test_name, test_case->error_part, serialized_len);
        return 1;
    }

    return expect_unrepresentable_error_part(ctx, test_case, "serialization",
                                             message);
}

// Require validation to reject an unrepresentable command under every
// supported validation-flag combination.
static int
validate_unrepresentable_case(TestContext *ctx,
                              const UnrepresentableCommandCase *test_case) {
    char message[256];
    ImgnekoErrorDetail detail = {
        .message = message,
        .message_cap = sizeof(message),
        .message_len = SIZE_MAX,
    };

    for (unsigned raw_flags = all_validation_flags;;
         raw_flags = (raw_flags - 1) & all_validation_flags) {
        ImgnekoCommandValidationFlags flags =
            (ImgnekoCommandValidationFlags)raw_flags;
        memset(message, 'x', sizeof(message));
        detail.message_len = SIZE_MAX;
        ImgnekoCommandError status =
            imgneko_command_validate(&test_case->command, flags, &detail);
        if (status != IMGNEKO_COMMAND_INVALID_FIELD) {
            fprintf(stderr,
                    "%s: unrepresentable command \"%s\": validation with "
                    "flags %#x returned %d\n",
                    ctx->test_name, test_case->error_part, raw_flags, status);
            return 1;
        }
        if (expect_unrepresentable_error_part(ctx, test_case, "validation",
                                              message))
            return 1;
        if (raw_flags == 0)
            break;
    }

    return 0;
}

// Require parsing to fail under every supported parse-policy combination.
static int check_unparseable_case(TestContext *ctx,
                                  const UnparseableCommandCase *test_case) {
    char message[256];
    ImgnekoErrorDetail detail = {
        .message = message,
        .message_cap = sizeof(message),
        .message_len = SIZE_MAX,
    };
    ImgnekoParsedCommand parsed;

    for (unsigned raw_flags = all_parse_flags;;
         raw_flags = (raw_flags - 1) & all_parse_flags) {
        ImgnekoCommandParseFlags flags = (ImgnekoCommandParseFlags)raw_flags;
        ImgnekoCommandParseOptions options = {
            .start_sequence = "",
            .end_sequence = "",
            .flags = flags,
        };

        memset(&parsed, 0xa5, sizeof(parsed));
        memset(message, 'x', sizeof(message));
        detail.message_len = SIZE_MAX;
        ImgnekoCommandParseError status =
            imgneko_command_parse(test_case->input, strlen(test_case->input),
                                  &options, &parsed, &detail);
        if (status != IMGNEKO_COMMAND_PARSE_FAILED) {
            fprintf(stderr,
                    "%s: unparseable input \"%s\" succeeded with flags "
                    "%#x\n",
                    ctx->test_name, test_case->input, raw_flags);
            return 1;
        }
        if (test_case->error_part == NULL ||
            strstr(message, test_case->error_part) == NULL) {
            fprintf(stderr,
                    "%s: unparseable input \"%s\": error with flags %#x "
                    "does not contain %s: %s\n",
                    ctx->test_name, test_case->input, raw_flags,
                    test_case->error_part != NULL ? test_case->error_part
                                                  : "<missing error part>",
                    message);
            return 1;
        }
        if (raw_flags == 0)
            break;
    }

    return 0;
}

// Require a conditional input to fail when a necessary parse flag is absent.
static int
expect_conditional_parse_failure(TestContext *ctx,
                                 const ConditionalParseCase *test_case,
                                 ImgnekoCommandParseFlags flags) {
    char message[256];
    ImgnekoErrorDetail detail = {
        .message = message,
        .message_cap = sizeof(message),
        .message_len = SIZE_MAX,
    };
    ImgnekoCommandParseOptions options = {
        .start_sequence = "",
        .end_sequence = "",
        .flags = flags,
    };
    ImgnekoParsedCommand parsed;

    memset(&parsed, 0xa5, sizeof(parsed));
    memset(message, 'x', sizeof(message));
    ImgnekoCommandParseError status = imgneko_command_parse(
        test_case->input, strlen(test_case->input), &options, &parsed, &detail);
    if (status != IMGNEKO_COMMAND_PARSE_FAILED) {
        fprintf(stderr,
                "%s: conditional input \"%s\" parsed with insufficient "
                "flags %#x\n",
                ctx->test_name, test_case->input, (unsigned)flags);
        return 1;
    }
    if (flags == IMGNEKO_COMMAND_PARSE_FLAGS_NONE &&
        (test_case->error_part == NULL ||
         strstr(message, test_case->error_part) == NULL)) {
        fprintf(stderr,
                "%s: conditional input \"%s\": strict error does not "
                "contain %s: %s\n",
                ctx->test_name, test_case->input,
                test_case->error_part != NULL ? test_case->error_part
                                              : "<missing error part>",
                message);
        return 1;
    }

    return 0;
}

// Exercise the complete lifecycle of a conditionally accepted input: establish
// its strict diagnostic, parse it under every applicable relaxed policy, check
// all warnings and borrowed spans, then serialize and reparse its normalized
// command without further diagnostics.
static int
expect_conditional_parse_success(TestContext *ctx,
                                 const ConditionalParseCase *test_case,
                                 ImgnekoCommandParseFlags flags) {
    char message[256];
    char strict_message[256];
    ImgnekoErrorDetail detail = {
        .message = message,
        .message_cap = sizeof(message),
        .message_len = SIZE_MAX,
    };
    ImgnekoCommandParseOptions options = {
        .start_sequence = "",
        .end_sequence = "",
        .flags = flags,
    };
    ImgnekoParsedCommand parsed;

    // Strict parsing establishes the baseline diagnostic for cases that need
    // a relaxation flag. Clean normalization cases skip this failure check.
    ImgnekoErrorDetail strict_detail = {
        .message = strict_message,
        .message_cap = sizeof(strict_message),
        .message_len = SIZE_MAX,
    };
    if (test_case->parse_flags != IMGNEKO_COMMAND_PARSE_FLAGS_NONE) {
        ImgnekoCommandParseOptions strict_options = {
            .start_sequence = "",
            .end_sequence = "",
        };
        ImgnekoParsedCommand strict_parsed;

        memset(&strict_parsed, 0xa5, sizeof(strict_parsed));
        memset(strict_message, 'x', sizeof(strict_message));
        ImgnekoCommandParseError strict_status = imgneko_command_parse(
            test_case->input, strlen(test_case->input), &strict_options,
            &strict_parsed, &strict_detail);
        if (strict_status != IMGNEKO_COMMAND_PARSE_FAILED) {
            fprintf(stderr,
                    "%s: conditional input \"%s\": strict parsing "
                    "returned %d\n",
                    ctx->test_name, test_case->input, strict_status);
            return 1;
        }
    }

    // With every required policy enabled, parsing must succeed and report all
    // discarded pairs. Single-warning cases reuse the exact strict message;
    // multi-warning cases provide their complete newline-separated output.
    memset(&parsed, 0xa5, sizeof(parsed));
    memset(message, 'x', sizeof(message));
    ImgnekoCommandParseError parse_status = imgneko_command_parse(
        test_case->input, strlen(test_case->input), &options, &parsed, &detail);
    if (parse_status != IMGNEKO_COMMAND_PARSE_OK) {
        fprintf(stderr,
                "%s: conditional input \"%s\": parsing with flags %#x "
                "returned %d (%.*s)\n",
                ctx->test_name, test_case->input, (unsigned)flags, parse_status,
                (int)sizeof(message), message);
        return 1;
    }
    if (test_case->parse_flags != IMGNEKO_COMMAND_PARSE_FLAGS_NONE) {
        const char *expected_warnings = test_case->expected_warnings != NULL
                                            ? test_case->expected_warnings
                                            : strict_message;
        if (detail.message_len != strlen(expected_warnings) ||
            strcmp(message, expected_warnings) != 0) {
            fprintf(stderr,
                    "%s: conditional input \"%s\": relaxed warning "
                    "output is wrong: %s != %s\n",
                    ctx->test_name, test_case->input, message,
                    expected_warnings);
            return 1;
        }
    } else if (detail.message_len != 0 || message[0] != '\0') {
        return test_fail_message(ctx, "conditional parse left a diagnostic");
    }
    if (parsed.header_len != strlen(test_case->input) ||
        (parsed.header_len != 0 &&
         memcmp(parsed.header, test_case->input, parsed.header_len) != 0) ||
        parsed.payload != NULL || parsed.payload_len != 0)
        return test_fail_message(ctx,
                                 "conditional parser returned wrong spans");

    // Serialization discards the ignored source pairs and produces the
    // canonical header without carrying parser warnings into another API.
    char serialized[IMGNEKO_COMMAND_MAX_SIZE];
    size_t serialized_len = SIZE_MAX;
    memset(serialized, 'x', sizeof(serialized));
    memset(message, 'x', sizeof(message));
    detail.message_len = SIZE_MAX;
    ImgnekoCommandError serialize_status = imgneko_command_header_to_buffer(
        &parsed.command, serialized, sizeof(serialized), &serialized_len,
        &detail);
    if (serialize_status != IMGNEKO_COMMAND_OK) {
        fprintf(stderr,
                "%s: conditional input \"%s\": serialization returned %d "
                "(%.*s)\n",
                ctx->test_name, test_case->input, serialize_status,
                (int)sizeof(message), message);
        return 1;
    }
    if (detail.message_len != 0 || message[0] != '\0')
        return test_fail_message(ctx,
                                 "conditional serialization left a diagnostic");

    if (test_expect_graphics_command_header_tokens(
            ctx, str_span(serialized, serialized_len),
            str_span(test_case->expected_tokens,
                     strlen(test_case->expected_tokens)),
            test_case->input))
        return 1;

    // The normalized header contains no discarded pairs, so reparsing must be
    // clean and preserve the complete command structure.
    ImgnekoParsedCommand reparsed;
    memset(&reparsed, 0xa5, sizeof(reparsed));
    memset(message, 'x', sizeof(message));
    detail.message_len = SIZE_MAX;
    parse_status = imgneko_command_parse(serialized, serialized_len, &options,
                                         &reparsed, &detail);
    if (parse_status != IMGNEKO_COMMAND_PARSE_OK) {
        fprintf(stderr,
                "%s: normalized conditional input \"%s\": reparsing with "
                "flags %#x returned %d (%.*s)\n",
                ctx->test_name, test_case->input, (unsigned)flags, parse_status,
                (int)sizeof(message), message);
        return 1;
    }
    if (detail.message_len != 0 || message[0] != '\0')
        return test_fail_message(ctx, "normalized reparse left a diagnostic");
    if (memcmp(&parsed.command, &reparsed.command, sizeof(parsed.command)) !=
        0) {
        fprintf(stderr,
                "%s: normalized conditional input \"%s\": reparsing with "
                "flags %#x changed the command structure\n",
                ctx->test_name, test_case->input, (unsigned)flags);
        return 1;
    }

    return 0;
}

// Check necessary parse flags and every superset for a conditional
// input, then compare its normalized serialization with the expected header.
static int check_conditional_parse_case(TestContext *ctx,
                                        const ConditionalParseCase *test_case) {
    unsigned required_flags = (unsigned)test_case->parse_flags;
    if (required_flags == 0)
        return test_fail_message(ctx,
                                 "conditional parse case requires no flags");
    if (expect_conditional_parse_failure(ctx, test_case,
                                         IMGNEKO_COMMAND_PARSE_FLAGS_NONE))
        return 1;

    // Removing each required bit in turn must prevent the expected parse.
    for (unsigned bit = 1; bit != 0; bit <<= 1) {
        if ((required_flags & bit) == 0)
            continue;

        unsigned partial_flags = required_flags & ~bit;
        if (partial_flags != 0 &&
            expect_conditional_parse_failure(
                ctx, test_case, (ImgnekoCommandParseFlags)partial_flags))
            return 1;
    }

    unsigned optional_flags = all_parse_flags & ~required_flags;
    for (unsigned added_flags = optional_flags;;
         added_flags = (added_flags - 1) & optional_flags) {
        ImgnekoCommandParseFlags flags =
            (ImgnekoCommandParseFlags)(required_flags | added_flags);
        if (expect_conditional_parse_success(ctx, test_case, flags))
            return 1;
        if (added_flags == 0)
            break;
    }

    return 0;
}

// Verify stable error strings, validation argument and diagnostic-buffer
// checks, NULL accessors, and representability checks for extension targets.
static int test_public_api_edges(TestContext *ctx) {
    static const struct {
        ImgnekoCommandError error;
        const char *message;
    } error_cases[] = {
        {IMGNEKO_COMMAND_OK, "success"},
        {IMGNEKO_COMMAND_INVALID_ARGUMENT, "invalid argument"},
        {IMGNEKO_COMMAND_INVALID_FIELD, "invalid command field"},
        {IMGNEKO_COMMAND_OVERFLOW, "size overflow"},
        {IMGNEKO_COMMAND_TOO_LARGE, "command too large"},
        {IMGNEKO_COMMAND_READ_FAILED, "payload read failed"},
        {IMGNEKO_COMMAND_BUFFER_TOO_SMALL, "buffer too small"},
        {IMGNEKO_COMMAND_WORKSPACE_TOO_SMALL, "workspace too small"},
        {IMGNEKO_COMMAND_WRITE_FAILED, "write failed"},
        {(ImgnekoCommandError)99, "unknown graphics command error"},
    };
    for (size_t i = 0; i < ARRAY_SIZE(error_cases); ++i) {
        const char *actual = imgneko_command_error_string(error_cases[i].error);
        if (strcmp(actual, error_cases[i].message) != 0)
            return test_fail_message(ctx, "wrong graphics-command error text");
    }

    if (imgneko_command_get_transmission(NULL) != NULL ||
        imgneko_command_get_transmission_const(NULL) != NULL ||
        imgneko_command_get_placement(NULL) != NULL ||
        imgneko_command_get_placement_const(NULL) != NULL)
        return test_fail_message(ctx, "NULL command accessor returned a field");

    ImgnekoErrorDetail invalid_detail = {
        .message = NULL,
        .message_cap = 1,
    };
    ImgnekoCommand command = {
        .kind = IMGNEKO_COMMAND_TRANSMIT,
        .data.transmit.transmission =
            {
                .medium = IMGNEKO_TRANSMISSION_MEDIUM_DIRECT,
            },
    };
    if (test_expect_status(ctx,
                           imgneko_command_validate(
                               &command, IMGNEKO_COMMAND_VALIDATION_FLAGS_NONE,
                               &invalid_detail),
                           IMGNEKO_COMMAND_INVALID_ARGUMENT,
                           "invalid validation detail"))
        return 1;

    char message[128];
    ImgnekoErrorDetail detail = {
        .message = message,
        .message_cap = sizeof(message),
    };
    if (test_expect_status(
            ctx,
            imgneko_command_validate(
                NULL, IMGNEKO_COMMAND_VALIDATION_FLAGS_NONE, &detail),
            IMGNEKO_COMMAND_INVALID_ARGUMENT, "NULL validation command") ||
        strstr(message, "must not be NULL") == NULL)
        return test_fail_message(ctx, "NULL validation diagnostic is wrong");

    // A capacity-one buffer cannot store diagnostic text, but it must still be
    // null-terminated and report the complete required length.
    char tiny_message[1] = {'x'};
    ImgnekoErrorDetail tiny_detail = {
        .message = tiny_message,
        .message_cap = sizeof(tiny_message),
        .message_len = SIZE_MAX,
    };
    if (test_expect_status(
            ctx,
            imgneko_command_validate(
                NULL, IMGNEKO_COMMAND_VALIDATION_FLAGS_NONE, &tiny_detail),
            IMGNEKO_COMMAND_INVALID_ARGUMENT,
            "truncated NULL validation diagnostic") ||
        tiny_detail.message_len != detail.message_len ||
        tiny_message[0] != '\0')
        return test_fail_message(ctx,
                                 "capacity-one validation diagnostic is wrong");

    // Printable unknown character values use their protocol spelling, while
    // control bytes use hexadecimal notation.
    static const struct {
        ImgnekoCommandKindChar kind;
        const char *expected_message;
    } unknown_character_cases[] = {
        {(ImgnekoCommandKindChar)'?', "unknown command kind (a=?)"},
        {(ImgnekoCommandKindChar)0x01, "unknown command kind (a=0x01)"},
        {(ImgnekoCommandKindChar)0x7f, "unknown command kind (a=0x7f)"},
    };
    for (size_t i = 0; i < ARRAY_SIZE(unknown_character_cases); ++i) {
        command = (ImgnekoCommand){
            .kind = unknown_character_cases[i].kind,
        };
        if (test_expect_status(
                ctx,
                imgneko_command_validate(
                    &command, IMGNEKO_COMMAND_VALIDATION_FLAGS_NONE, &detail),
                IMGNEKO_COMMAND_INVALID_FIELD,
                "unknown character diagnostic") ||
            strcmp(message, unknown_character_cases[i].expected_message) != 0)
            return test_fail_message(ctx,
                                     "unknown character diagnostic is wrong");
    }

    command = (ImgnekoCommand){
        .kind = IMGNEKO_COMMAND_DELETE,
        .data.delete_cmd =
            {
                .target = (ImgnekoDeleteTargetChar)'?',
                .delete_data = true,
                .z_index = 17,
            },
    };
    char header[32];
    size_t header_len = 0;
    if (test_expect_status(
            ctx,
            imgneko_command_header_to_buffer(&command, header, sizeof(header),
                                             &header_len, &detail),
            IMGNEKO_COMMAND_INVALID_FIELD,
            "non-uppercaseable delete-data target") ||
        strstr(message, "d=0x3f") == NULL)
        return test_fail_message(
            ctx, "non-uppercaseable delete target diagnostic is wrong");

    command.data.delete_cmd.delete_data = false;
    if (test_expect_status(
            ctx,
            imgneko_command_header_to_buffer(&command, header, sizeof(header),
                                             &header_len, NULL),
            IMGNEKO_COMMAND_OK, "extension delete serialization") ||
        test_expect_graphics_command_header_tokens(
            ctx, str_span(header, header_len),
            str_span_from_cstr("a=d,d=?,z=17"),
            "extension delete serialization"))
        return 1;

    command.data.delete_cmd.target = (ImgnekoDeleteTargetChar)'{';
    command.data.delete_cmd.delete_data = true;
    if (test_expect_status(
            ctx,
            imgneko_command_header_to_buffer(&command, header, sizeof(header),
                                             &header_len, &detail),
            IMGNEKO_COMMAND_INVALID_FIELD,
            "high non-uppercaseable delete-data target") ||
        strstr(message, "d=0x7b") == NULL)
        return test_fail_message(
            ctx, "high non-uppercaseable delete target diagnostic is wrong");

    command.data.delete_cmd.delete_data = false;
    if (test_expect_status(
            ctx,
            imgneko_command_header_to_buffer(&command, header, sizeof(header),
                                             &header_len, NULL),
            IMGNEKO_COMMAND_OK, "high extension delete serialization") ||
        test_expect_graphics_command_header_tokens(
            ctx, str_span(header, header_len),
            str_span_from_cstr("a=d,d={,z=17"),
            "high extension delete serialization"))
        return 1;

    return 0;
}

// Verify every representable command through serialization, parsing, and
// validation.
static int test_representable_commands(TestContext *ctx) {
    for (size_t i = 0; i < ARRAY_SIZE(command_cases); ++i) {
        const CommandCase *test_case = &command_cases[i];

        if (serialize_case(ctx, test_case) || parse_case(ctx, test_case) ||
            validate_case(ctx, test_case))
            return 1;
    }

    return 0;
}

// Verify commands with no protocol representation through serialization and
// validation only.
static int test_unrepresentable_commands(TestContext *ctx) {
    for (size_t i = 0; i < ARRAY_SIZE(unrepresentable_command_cases); ++i) {
        const UnrepresentableCommandCase *test_case =
            &unrepresentable_command_cases[i];

        if (serialize_unrepresentable_case(ctx, test_case) ||
            validate_unrepresentable_case(ctx, test_case))
            return 1;
    }

    return 0;
}

// Verify malformed inputs and unrepresentable discriminators cannot be
// accepted by any parse-flag combination.
static int test_unparseable_commands(TestContext *ctx) {
    for (size_t i = 0; i < ARRAY_SIZE(unparseable_command_cases); ++i) {
        if (check_unparseable_case(ctx, &unparseable_command_cases[i]))
            return 1;
    }

    return 0;
}

// Verify flag-dependent inputs produce their expected normalized headers.
static int test_conditionally_parseable_commands(TestContext *ctx) {
    for (size_t i = 0; i < ARRAY_SIZE(conditional_parse_cases); ++i) {
        if (check_conditional_parse_case(ctx, &conditional_parse_cases[i]))
            return 1;
    }

    return 0;
}

// Verify protocol defaults are accepted and serialized in canonical form
// without requiring a parsing relaxation flag.
static int test_default_value_normalization(TestContext *ctx) {
    for (size_t i = 0; i < ARRAY_SIZE(default_normalization_cases); ++i) {
        const DefaultNormalizationCase *normalization_case =
            &default_normalization_cases[i];
        ConditionalParseCase test_case = {
            .input = normalization_case->input,
            .expected_tokens = normalization_case->expected_tokens,
        };

        if (expect_conditional_parse_success(ctx, &test_case,
                                             IMGNEKO_COMMAND_PARSE_FLAGS_NONE))
            return 1;
    }

    return 0;
}

int main(int argc, char **argv) {
    const Subtest subtests[] = {
        PREFIXED_TEST(test_representable_commands),
        PREFIXED_TEST(test_unrepresentable_commands),
        PREFIXED_TEST(test_unparseable_commands),
        PREFIXED_TEST(test_conditionally_parseable_commands),
        PREFIXED_TEST(test_default_value_normalization),
        PREFIXED_TEST(test_public_api_edges),
    };

    return run_subtests(argc, argv, subtests, ARRAY_SIZE(subtests));
}
