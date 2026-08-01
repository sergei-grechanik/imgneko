// SPDX-License-Identifier: MIT-0

// Stream data through imgneko reader transformers for benchmark workloads.
//
// This program deliberately does not measure time. The benchmark driver owns
// timing, input generation, correctness checks, and reporting so this helper
// can focus on making each reader workload behave like a normal stdin/stdout
// filter. It also provides the deterministic synthetic-data generator used by
// the driver, keeping generated inputs independent of platform shell tools.

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "imgneko/base64.h"
#include "imgneko/reader.h"
#include "imgneko/zlib.h"
#include "util/options.h"
#include "util/print_build_info.h"

#define DEFAULT_BUFFER_SIZE_TEXT "16384"
#define DEFAULT_GENERATOR_SEED_TEXT "7640891576956012809"

// Workloads that can be drained from standard input or generated on demand.
typedef enum BenchmarkMode {
    BENCHMARK_MODE_INVALID = 0,
    BENCHMARK_MODE_BASE64_ENCODE,
    BENCHMARK_MODE_BASE64_DECODE,
    BENCHMARK_MODE_ZLIB_COMPRESS,
    BENCHMARK_MODE_ZLIB_DECOMPRESS,
    BENCHMARK_MODE_GENERATE,
} BenchmarkMode;

// Synthetic data-set shapes used by the deterministic generator.
typedef enum GeneratorDataSet {
    GENERATOR_DATA_SET_RANDOM = 0,
    GENERATOR_DATA_SET_REPETITIVE,
} GeneratorDataSet;

// Generic-parser wrappers for the benchmark-specific scalar option types.
OPT_DEFINE_WRAPPER_STRUCT(OptByteCount, size_t);
OPT_DEFINE_WRAPPER_STRUCT(OptGeneratorSeed, uint64_t);
OPT_DEFINE_WRAPPER_STRUCT(OptGeneratorDataSet, GeneratorDataSet);

// Validated settings for a reader workload or generator invocation.
typedef struct BenchmarkOptions {
    BenchmarkMode mode;
    size_t input_buffer_size;
    size_t output_buffer_size;
    size_t generate_bytes;
    uint64_t generator_seed;
    GeneratorDataSet generator_data_set;
} BenchmarkOptions;

static const OptNamedEnumOption generator_data_set_options[] = {
    {"random", GENERATOR_DATA_SET_RANDOM},
    {"repetitive", GENERATOR_DATA_SET_REPETITIVE},
};

// Parse a nonzero unsigned 64-bit generator seed.
static bool parse_generator_seed_option(void *value, const char *text,
                                        size_t text_len, String *error_out) {
    uint64_t *seed = value;

    if (!opt_parse_uint64_hex_or_decimal_span(text, text_len, seed) ||
        *seed == 0) {
        return opt_parse_error(error_out,
                               "expected a nonzero unsigned 64-bit integer");
    }

    return true;
}

// Parse the data-set selector used by the deterministic generator.
static bool parse_generator_data_set_option(void *value, const char *text,
                                            size_t text_len,
                                            String *error_out) {
    int parsed = 0;

    if (!opt_parse_named_enum_option(generator_data_set_options,
                                     ARRAY_SIZE(generator_data_set_options),
                                     text, text_len, &parsed, error_out)) {
        return false;
    }

    *(GeneratorDataSet *)value = (GeneratorDataSet)parsed;
    return true;
}

// Options shared by every reader workload.
#define BENCHMARK_READER_OPTIONS(X, S)                                         \
    X(S, input_buffer_size, OptByteCount,                                      \
      OPT_CUSTOM(.cli = "--input-buffer-size BYTES",                           \
                 .descr = "Source-reader workspace size.",                     \
                 .dflt = DEFAULT_BUFFER_SIZE_TEXT,                             \
                 .parse = opt_parse_byte_count_option))                        \
    X(S, output_buffer_size, OptByteCount,                                     \
      OPT_CUSTOM(.cli = "--output-buffer-size BYTES",                          \
                 .descr = "Drain buffer size.",                                \
                 .dflt = DEFAULT_BUFFER_SIZE_TEXT,                             \
                 .parse = opt_parse_byte_count_option))

// Program-wide options that exit before a reader workload is selected.
#define BENCHMARK_PROGRAM_OPTIONS(X, S)                                        \
    X(S, version, OptBool,                                                     \
      OPT_BOOL_FLAG(.cli = "--version",                                        \
                    .descr = "Show helper and generator versions."))

// Options accepted only by the data-generation workload.
#define BENCHMARK_GENERATE_OPTIONS(X, S)                                       \
    X(S, bytes, OptByteCount,                                                  \
      OPT_CUSTOM(.cli = "--bytes BYTES", .descr = "Bytes to write.",           \
                 .parse = opt_parse_byte_count_option))                        \
    X(S, data_set, OptGeneratorDataSet,                                        \
      OPT_CUSTOM(.cli = "--data-set DATA_SET",                                 \
                 .descr = "Synthetic data set: random or repetitive.",         \
                 .dflt = "random", .parse = parse_generator_data_set_option))  \
    X(S, seed, OptGeneratorSeed,                                               \
      OPT_CUSTOM(.cli = "--seed UINT64", .descr = "xorshift64star-v1 seed.",   \
                 .dflt = DEFAULT_GENERATOR_SEED_TEXT,                          \
                 .parse = parse_generator_seed_option))

OPT_DEFINE_STRUCT(BenchmarkReaderOptions, BENCHMARK_READER_OPTIONS)
OPT_DEFINE_STRUCT(BenchmarkProgramOptions, BENCHMARK_PROGRAM_OPTIONS)
OPT_DEFINE_STRUCT(BenchmarkGenerateOptions, BENCHMARK_GENERATE_OPTIONS)

#define BENCHMARK_READER_COMMANDS(Name, X)                                     \
    X(Name, base64_encode, BenchmarkReaderOptions,                             \
      OPT_COMMAND(.name = "base64-encode",                                     \
                  .descr = "Encode standard input as base64."))                \
    X(Name, base64_decode, BenchmarkReaderOptions,                             \
      OPT_COMMAND(.name = "base64-decode",                                     \
                  .descr = "Decode base64 from standard input."))              \
    X(Name, zlib_compress, BenchmarkReaderOptions,                             \
      OPT_COMMAND(.name = "zlib-compress",                                     \
                  .descr = "Compress standard input as a zlib stream."))       \
    X(Name, zlib_decompress, BenchmarkReaderOptions,                           \
      OPT_COMMAND(.name = "zlib-decompress",                                   \
                  .descr = "Decompress a zlib stream from standard input."))   \
    X(Name, generate, BenchmarkGenerateOptions,                                \
      OPT_COMMAND(.descr = "Generate deterministic benchmark input data."))

OPT_DEFINE_PROGRAM_PARSER_WITH_TOP_LEVEL_OPTIONS(
    BenchmarkReaders,
    OPT_PROGRAM(.program_name = "benchmark-readers",
                .descr = "Benchmark stdin/stdout reader workloads and "
                         "generate deterministic benchmark inputs."),
    BenchmarkProgramOptions, BENCHMARK_READER_COMMANDS);

// Print the imgneko build and linked-dependency details used for benchmarks.
static void print_version(void) { build_info_print(); }

// Validate reader-specific workspace requirements before any allocation or
// stdin consumption, so a malformed benchmark command fails predictably.
static int validate_reader_options(const BenchmarkOptions *options) {
    size_t minimum_input = 0;
    size_t minimum_output = 0;

    switch (options->mode) {
    case BENCHMARK_MODE_BASE64_ENCODE:
        minimum_input = 3;
        minimum_output = 4;
        break;
    case BENCHMARK_MODE_BASE64_DECODE:
        minimum_input = 4;
        minimum_output = 3;
        break;
    case BENCHMARK_MODE_ZLIB_COMPRESS:
    case BENCHMARK_MODE_ZLIB_DECOMPRESS:
        minimum_input = 1;
        minimum_output = 1;
        break;
    case BENCHMARK_MODE_INVALID:
    case BENCHMARK_MODE_GENERATE:
        return -1;
    }

    if (options->input_buffer_size < minimum_input) {
        fprintf(stderr,
                "error: --input-buffer-size must be at least %zu bytes for "
                "this workload\n",
                minimum_input);
        return -1;
    }
    if (options->output_buffer_size < minimum_output) {
        fprintf(stderr,
                "error: --output-buffer-size must be at least %zu bytes for "
                "this workload\n",
                minimum_output);
        return -1;
    }
    if ((options->mode == BENCHMARK_MODE_ZLIB_COMPRESS ||
         options->mode == BENCHMARK_MODE_ZLIB_DECOMPRESS) &&
        options->input_buffer_size > UINT_MAX) {
        fprintf(stderr,
                "error: zlib reader input workspaces cannot exceed %u "
                "bytes\n",
                UINT_MAX);
        return -1;
    }

    return 0;
}

// Translate shared reader options into a validated reader workload request.
// options_out receives the request on success.
static int prepare_reader_options(BenchmarkMode mode,
                                  const BenchmarkReaderOptions *cli_options,
                                  BenchmarkOptions *options_out) {
    BenchmarkOptions options = {
        .mode = mode,
        .input_buffer_size = cli_options->input_buffer_size.value,
        .output_buffer_size = cli_options->output_buffer_size.value,
    };

    if (validate_reader_options(&options) != 0)
        return 2;

    *options_out = options;
    return 0;
}

// Translate generator-only options into a generator request.
// options_out receives the request on success.
static int
prepare_generate_options(const BenchmarkGenerateOptions *generate_options,
                         BenchmarkOptions *options_out) {
    BenchmarkOptions options = {
        .mode = BENCHMARK_MODE_GENERATE,
        .generate_bytes = generate_options->bytes.value,
        .generator_seed = generate_options->seed.value,
        .generator_data_set = generate_options->data_set.value,
    };

    if (!generate_options->bytes.is_set) {
        fprintf(stderr, "error: generate requires --bytes=BYTES\n");
        return 2;
    }

    *options_out = options;
    return 0;
}

// Write all bytes in data to standard output, handling interrupted and short
// writes. Return zero on success and -1 after a write failure.
static int write_all_stdout(const char *data, size_t len) {
    size_t offset = 0;

    while (offset < len) {
        ssize_t written = write(STDOUT_FILENO, data + offset, len - offset);

        if (written > 0) {
            offset += (size_t)written;
            continue;
        }
        if (written < 0 && errno == EINTR)
            continue;

        if (written == 0)
            errno = EIO;
        return -1;
    }

    return 0;
}

// Drain a reader into caller-owned output storage and forward every produced
// span to stdout.
//
// reader
//     Reader transformer to drain until EOF or failure.
// output_buffer
//     Caller-owned drain buffer.
// output_buffer_size
//     Number of bytes available in output_buffer.
// failure_status_out
//     Output parameter receiving the reader status that ended an unsuccessful
//     drain, or IMGNEKO_READER_EOF after success.
static int drain_reader(ImgnekoReader reader, char *output_buffer,
                        size_t output_buffer_size,
                        ImgnekoReaderStatus *failure_status_out) {
    *failure_status_out = IMGNEKO_READER_EOF;

    while (true) {
        size_t output_len = 0;
        ImgnekoReaderStatus status = imgneko_reader_read(
            reader, output_buffer, output_buffer_size, &output_len);

        if (status == IMGNEKO_READER_OK) {
            if (write_all_stdout(output_buffer, output_len) != 0) {
                fprintf(stderr, "error: failed to write standard output: %s\n",
                        strerror(errno));
                *failure_status_out = IMGNEKO_READER_ERROR;
                return -1;
            }
            continue;
        }

        if (status == IMGNEKO_READER_EOF)
            return 0;

        *failure_status_out = status;
        return -1;
    }
}

// Report a reader failure with its transformer-specific diagnostic when one is
// available. The caller has already stopped draining and can safely inspect
// the transformer's sticky error state.
//
// mode
//     Reader operation that failed.
// status
//     Generic reader status reported by the operation.
// decoder
//     Base64 decoder state, when the operation is base64 decoding.
// compressor
//     zlib compressor state, when the operation is zlib compression.
// decompressor
//     zlib decompressor state, when the operation is zlib decompression.
static void
report_transform_failure(BenchmarkMode mode, ImgnekoReaderStatus status,
                         const ImgnekoBase64DecodeReader *decoder,
                         const ImgnekoZlibCompressReader *compressor,
                         const ImgnekoZlibDecompressReader *decompressor) {
    fprintf(stderr, "error: reader workload failed: %s",
            imgneko_reader_status_string(status));

    if (mode == BENCHMARK_MODE_BASE64_DECODE && decoder != NULL &&
        decoder->error_status != IMGNEKO_BASE64_OK)
        fprintf(stderr, " (%s)",
                imgneko_base64_error_string(decoder->error_status));
    if (mode == BENCHMARK_MODE_ZLIB_COMPRESS && compressor != NULL &&
        compressor->error_status != IMGNEKO_ZLIB_OK)
        fprintf(stderr, " (%s)",
                imgneko_zlib_error_string(compressor->error_status));
    if (mode == BENCHMARK_MODE_ZLIB_DECOMPRESS && decompressor != NULL &&
        decompressor->error_status != IMGNEKO_ZLIB_OK)
        fprintf(stderr, " (%s)",
                imgneko_zlib_error_string(decompressor->error_status));

    fputc('\n', stderr);
}

// Run an imgneko reader transformer over standard input and write its complete
// output to standard output. The function owns and frees both borrowed reader
// workspaces, and always releases zlib state before it returns.
static int run_reader_workload(const BenchmarkOptions *options) {
    ImgnekoFdReader source;
    ImgnekoBase64EncodeReader base64_encoder = {0};
    ImgnekoBase64DecodeReader base64_decoder = {0};
    ImgnekoZlibCompressReader zlib_compressor = {0};
    ImgnekoZlibDecompressReader zlib_decompressor = {0};
    ImgnekoReader transformer = {0};
    ImgnekoReaderStatus failure_status = IMGNEKO_READER_ERROR;
    char *input_buffer = NULL;
    char *output_buffer = NULL;
    int result = 1;

    input_buffer = malloc(options->input_buffer_size);
    output_buffer = malloc(options->output_buffer_size);
    if (input_buffer == NULL || output_buffer == NULL) {
        fprintf(stderr, "error: failed to allocate reader buffers: %s\n",
                strerror(errno));
        goto cleanup;
    }

    imgneko_fd_reader_init(&source, STDIN_FILENO);

    switch (options->mode) {
    case BENCHMARK_MODE_BASE64_ENCODE:
        imgneko_base64_encode_reader_init(
            &base64_encoder, imgneko_fd_reader_as_reader(&source), input_buffer,
            options->input_buffer_size);
        transformer = imgneko_base64_encode_reader_as_reader(&base64_encoder);
        break;
    case BENCHMARK_MODE_BASE64_DECODE:
        imgneko_base64_decode_reader_init(
            &base64_decoder, imgneko_fd_reader_as_reader(&source), input_buffer,
            options->input_buffer_size);
        transformer = imgneko_base64_decode_reader_as_reader(&base64_decoder);
        break;
    case BENCHMARK_MODE_ZLIB_COMPRESS:
        if (imgneko_zlib_compress_reader_init(
                &zlib_compressor, imgneko_fd_reader_as_reader(&source),
                input_buffer, options->input_buffer_size) != IMGNEKO_ZLIB_OK) {
            fprintf(stderr, "error: failed to initialize zlib compressor: %s\n",
                    imgneko_zlib_error_string(zlib_compressor.error_status));
            goto cleanup;
        }
        transformer = imgneko_zlib_compress_reader_as_reader(&zlib_compressor);
        break;
    case BENCHMARK_MODE_ZLIB_DECOMPRESS:
        if (imgneko_zlib_decompress_reader_init(
                &zlib_decompressor, imgneko_fd_reader_as_reader(&source),
                input_buffer, options->input_buffer_size) != IMGNEKO_ZLIB_OK) {
            fprintf(stderr,
                    "error: failed to initialize zlib decompressor: %s\n",
                    imgneko_zlib_error_string(zlib_decompressor.error_status));
            goto cleanup;
        }
        transformer =
            imgneko_zlib_decompress_reader_as_reader(&zlib_decompressor);
        break;
    case BENCHMARK_MODE_INVALID:
    case BENCHMARK_MODE_GENERATE:
        fprintf(stderr, "error: invalid reader workload\n");
        goto cleanup;
    }

    if (drain_reader(transformer, output_buffer, options->output_buffer_size,
                     &failure_status) != 0) {
        report_transform_failure(options->mode, failure_status, &base64_decoder,
                                 &zlib_compressor, &zlib_decompressor);
        goto cleanup;
    }

    result = 0;

cleanup:
    // These deinitializers are safe for zeroed and failed-initialization state.
    if (options->mode == BENCHMARK_MODE_ZLIB_COMPRESS)
        imgneko_zlib_compress_reader_deinit(&zlib_compressor);
    if (options->mode == BENCHMARK_MODE_ZLIB_DECOMPRESS)
        imgneko_zlib_decompress_reader_deinit(&zlib_decompressor);
    free(output_buffer);
    free(input_buffer);
    return result;
}

// Advance the fixed xorshift64* generator. The output bytes are emitted in
// increasing low-to-high byte order below, which specifies the generated byte
// sequence independently of the host's native endianness.
static uint64_t next_xorshift64star(uint64_t *state) {
    uint64_t value = *state;

    value ^= value >> 12;
    value ^= value << 25;
    value ^= value >> 27;
    *state = value;
    return value * UINT64_C(2685821657736338717);
}

// Generate a requested number of deterministic random or repetitive bytes on
// standard output.
static int generate_data(const BenchmarkOptions *options) {
    static const char repetitive_pattern[] = "imgneko-reader-benchmark-v1\n";
    char buffer[4096];
    size_t remaining = options->generate_bytes;
    size_t pattern_offset = 0;
    uint64_t state = options->generator_seed;

    while (remaining != 0) {
        size_t chunk_size =
            remaining < sizeof(buffer) ? remaining : sizeof(buffer);

        if (options->generator_data_set == GENERATOR_DATA_SET_RANDOM) {
            size_t offset = 0;

            while (offset < chunk_size) {
                uint64_t value = next_xorshift64star(&state);

                for (size_t byte = 0;
                     byte < sizeof(value) && offset < chunk_size; ++byte)
                    buffer[offset++] = (char)(value >> (byte * CHAR_BIT));
            }
        } else {
            for (size_t offset = 0; offset < chunk_size; ++offset) {
                buffer[offset] = repetitive_pattern[pattern_offset];
                pattern_offset =
                    (pattern_offset + 1) % (sizeof(repetitive_pattern) - 1);
            }
        }

        if (write_all_stdout(buffer, chunk_size) != 0) {
            fprintf(stderr, "error: failed to write generated data: %s\n",
                    strerror(errno));
            return 1;
        }
        remaining -= chunk_size;
    }

    return 0;
}

int main(int argc, char **argv) {
    ParsedBenchmarkReaders parsed = {0};
    BenchmarkOptions options = {0};
    int result =
        opt_run_program_parser(&BenchmarkReaders_parser, argc, argv, &parsed);

    if (result != 0 || parsed.must_exit)
        return result;

    if (parsed.top_level.version.value) {
        print_version();
        goto cleanup;
    }

    switch (parsed.command_id) {
    case OPT_CMD_BenchmarkReaders_base64_encode:
        result =
            prepare_reader_options(BENCHMARK_MODE_BASE64_ENCODE,
                                   &parsed.command.base64_encode, &options);
        break;
    case OPT_CMD_BenchmarkReaders_base64_decode:
        result =
            prepare_reader_options(BENCHMARK_MODE_BASE64_DECODE,
                                   &parsed.command.base64_decode, &options);
        break;
    case OPT_CMD_BenchmarkReaders_zlib_compress:
        result =
            prepare_reader_options(BENCHMARK_MODE_ZLIB_COMPRESS,
                                   &parsed.command.zlib_compress, &options);
        break;
    case OPT_CMD_BenchmarkReaders_zlib_decompress:
        result =
            prepare_reader_options(BENCHMARK_MODE_ZLIB_DECOMPRESS,
                                   &parsed.command.zlib_decompress, &options);
        break;
    case OPT_CMD_BenchmarkReaders_generate:
        result = prepare_generate_options(&parsed.command.generate, &options);
        break;
    case OPT_CMD_NONE_BenchmarkReaders:
        fprintf(stderr, "error: missing workload\n");
        result = 2;
        break;
    // IMGNEKO_UNCOVERED_OK[3 lines]: Defensive against malformed parser data.
    default:
        fprintf(stderr, "error: invalid workload\n");
        result = 2;
        break;
    }

    if (result != 0)
        goto cleanup;

    if (options.mode == BENCHMARK_MODE_GENERATE)
        result = generate_data(&options);
    else
        result = run_reader_workload(&options);

cleanup:
    opt_program_result_deinit(&BenchmarkReaders_parser, &parsed);
    return result;
}
