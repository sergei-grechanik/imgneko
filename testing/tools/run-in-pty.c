// Expose the POSIX/XSI PTY interfaces used below: posix_openpt(), grantpt(),
// unlockpt(), and ptsname().
#define _XOPEN_SOURCE 600

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "util/error.h"
#include "util/options.h"

// Run a command with stdin/stdout/stderr attached to a PTY whose window size
// is configured up front. This lets tests exercise TTY-sensitive behavior
// without depending on external tools such as `script` or `stty`.

#define RUN_IN_PTY_STRINGIFY_IMPL(value) #value
#define RUN_IN_PTY_STRINGIFY(value) RUN_IN_PTY_STRINGIFY_IMPL(value)

// CLI schema for the PTY helper. The child command and its arguments are only
// accepted after a bare `--`, which keeps option-like child argv entries out
// of the helper's own option space.
#define RUN_IN_PTY_OPTIONS(X, S)                                               \
    X(S, rows, OptInt,                                                         \
      OPT_CUSTOM(.parse = opt_parse_positive_int_option, .cli = "--rows ROWS", \
                 .descr = "PTY row count.", .dflt = "24"))                     \
    X(S, cols, OptInt,                                                         \
      OPT_CUSTOM(.parse = opt_parse_positive_int_option, .cli = "--cols COLS", \
                 .descr = "PTY column count.", .dflt = "80"))                  \
    X(S, opost, OptBool,                                                       \
      OPT_BOOL_NEGATABLE(.cli = "--opost", .cli_negate = "--no-opost",         \
                         .descr = "Enable PTY output post-processing.",        \
                         .dflt = "false"))                                     \
    X(S, write_chunk_size, OptInt,                                             \
      OPT_CUSTOM(.parse = opt_parse_positive_int_option,                       \
                 .cli = "--write-chunk-size BYTES",                            \
                 .descr = "Maximum stdout forwarding write size.",             \
                 .dflt = RUN_IN_PTY_STRINGIFY(PIPE_BUF)))                      \
    X(S, command, OptString,                                                   \
      OPT_STRING(.cli = "COMMAND", .descr = "Command to run under the PTY.",   \
                 .positional = true, .double_dash_only = true))                \
    X(S, args, OptStringList,                                                  \
      OPT_STRING_LIST(.cli = "ARG",                                            \
                      .descr = "Arguments forwarded to COMMAND.",              \
                      .positional = true, .double_dash_only = true))

OPT_DEFINE_STRUCT(RunInPtyOptions, RUN_IN_PTY_OPTIONS)

OPT_DEFINE_PROGRAM_PARSER_NO_COMMANDS(
    RunInPty,
    OPT_PROGRAM(.program_name = "run-in-pty",
                .descr = "Run a child command under a PTY with a requested "
                         "window size."),
    RunInPtyOptions);

// Validate the fixed positional prefix that this tool requires.
static int validate_required_arguments(const RunInPtyOptions *options) {
    if (!options->command.is_set) {
        fprintf(stderr, "error: missing required argument: COMMAND\n");
        return 2;
    }

    return 0;
}

// Open the PTY subsidiary corresponding to an already-opened manager FD.
static int open_pty_subsidiary(int manager_fd) {
    char *subsidiary_path = ptsname(manager_fd);
    int subsidiary_fd = -1;

    require(subsidiary_path != NULL,
            "failed to resolve PTY subsidiary path: %errno");
    subsidiary_fd = open(subsidiary_path, O_RDWR | O_NOCTTY);
    require(subsidiary_fd >= 0, "failed to open PTY subsidiary: %errno");
    return subsidiary_fd;
}

// Apply the requested window size and disable newline translation so forwarded
// output matches the child process's writes byte-for-byte.
static void configure_pty_subsidiary(int subsidiary_fd,
                                     const RunInPtyOptions *options) {
    struct winsize size = {
        .ws_row = (unsigned short)options->rows.value,
        .ws_col = (unsigned short)options->cols.value,
    };
    struct termios attrs;

    require(ioctl(subsidiary_fd, TIOCSWINSZ, &size) == 0,
            "failed to set PTY window size: %errno");
    require(tcgetattr(subsidiary_fd, &attrs) == 0,
            "failed to read PTY terminal attributes: %errno");
    if (options->opost.value)
        attrs.c_oflag |= OPOST;
    else
        attrs.c_oflag &= (tcflag_t)~OPOST;
    require(tcsetattr(subsidiary_fd, TCSANOW, &attrs) == 0,
            "failed to update PTY terminal attributes: %errno");
}

// Like require(), but report the current errno and exit from the child.
// IMGNEKO_UNCOVERED_OK_START
static void child_require(bool condition, const char *message) {
    if (condition)
        return;

    int saved_errno = errno;
    fprintf(stderr, "error: %s: %s\n", message, strerror(saved_errno));
    _exit(127);
}
// IMGNEKO_UNCOVERED_OK_END

// Fork a child whose standard streams all point at the PTY subsidiary. This
// code keeps the already-open subsidiary FD across the fork instead of
// reopening the subsidiary path in the child, because some devpts/container
// setups remove that pathname once the setup-side descriptor is closed.
static pid_t spawn_pty_child(int manager_fd, int subsidiary_fd, char **argv) {
    pid_t pid = fork();

    require(pid >= 0, "failed to fork PTY child: %errno");
    // IMGNEKO_UNCOVERED_OK_START: Reporting coverage for children is tricky
    if (pid != 0)
        return pid;

    child_require(close(manager_fd) == 0,
                  "failed to close PTY manager in child");
    child_require(setsid() >= 0, "failed to start a new session");
    child_require(ioctl(subsidiary_fd, TIOCSCTTY, 0) >= 0,
                  "failed to make PTY the controlling terminal");
    child_require(dup2(subsidiary_fd, STDIN_FILENO) >= 0,
                  "failed to connect PTY to stdin");
    child_require(dup2(subsidiary_fd, STDOUT_FILENO) >= 0,
                  "failed to connect PTY to stdout");
    child_require(dup2(subsidiary_fd, STDERR_FILENO) >= 0,
                  "failed to connect PTY to stderr");
    if (subsidiary_fd > STDERR_FILENO)
        close(subsidiary_fd);

    execvp(argv[0], argv);
    child_require(false, "failed to exec child command");
    return -1;
    // IMGNEKO_UNCOVERED_OK_END
}

// Build a NULL-terminated child argv array whose entries point at the parsed
// command/argument strings owned by `options`. The caller frees the array
// itself but not the pointed-at strings.
static char **build_child_argv(const RunInPtyOptions *options) {
    size_t argc = options->args.value.size + 2;
    char **argv = calloc(argc, sizeof(*argv));
    require(argv != NULL, "failed to allocate PTY child argv: %errno");

    argv[0] = options->command.value.cstr;
    for (size_t i = 0; i < options->args.value.size; ++i)
        argv[i + 1] = options->args.value.data[i].cstr;

    return argv;
}

// Write a complete byte span to stdout, retrying short writes and EINTR. Limit
// each write() request to the configured maximum chunk size so tests can force
// shorter writes when needed.
static void write_all_stdout(const char *data, size_t len,
                             size_t write_chunk_size) {
    size_t offset = 0;

    while (offset < len) {
        size_t chunk_len = len - offset;
        ssize_t written;

        if (chunk_len > write_chunk_size)
            chunk_len = write_chunk_size;
        written = write(STDOUT_FILENO, data + offset, chunk_len);

        if (written > 0) {
            offset += (size_t)written;
            continue;
        }

        // IMGNEKO_UNCOVERED_OK[2 lines]
        require(written < 0 && errno == EINTR,
                "failed to write PTY output to stdout: %errno");
    }
}

// Forward all PTY manager output to stdout until the child closes the PTY.
static void forward_pty_output(int manager_fd, size_t write_chunk_size) {
    char buffer[4096];

    while (true) {
        ssize_t read_size = read(manager_fd, buffer, sizeof(buffer));
        if (read_size > 0) {
            write_all_stdout(buffer, (size_t)read_size, write_chunk_size);
            continue;
        }

        // IMGNEKO_UNCOVERED_OK_START: Hard to trigger
        if (read_size == 0)
            return;
        if (errno == EINTR)
            continue;
        if (errno != EIO)
            die("failed to read PTY output: %errno");
        // IMGNEKO_UNCOVERED_OK_END

        return;
    }
}

// Translate a waited child status into this process's exit code.
static int child_status_exit_code(int status) {
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    if (WIFSIGNALED(status))
        return 128 + WTERMSIG(status);

    // IMGNEKO_UNCOVERED_OK[2 lines]
    die("child exited without an exit code or terminating signal");
    return 1;
}

// Run the parsed child command under a configured PTY and return the child's
// eventual exit status.
static int run_command_in_pty(const RunInPtyOptions *options) {
    int manager_fd = -1;
    int subsidiary_fd = -1;
    size_t write_chunk_size = (size_t)options->write_chunk_size.value;

    // Open the PTY pair and configure the subsidiary side before the child
    // is executed so the child inherits the requested terminal size and output
    // translation settings.
    manager_fd = posix_openpt(O_RDWR | O_NOCTTY);
    require(manager_fd >= 0, "failed to open PTY manager: %errno");
    require(grantpt(manager_fd) == 0, "failed to grant PTY access: %errno");
    require(unlockpt(manager_fd) == 0, "failed to unlock PTY: %errno");
    subsidiary_fd = open_pty_subsidiary(manager_fd);
    configure_pty_subsidiary(subsidiary_fd, options);

    // Build the child argv from parsed strings and spawn the child with all
    // three standard streams attached to the PTY subsidiary.
    char **child_argv = build_child_argv(options);
    pid_t child_pid = spawn_pty_child(manager_fd, subsidiary_fd, child_argv);
    free(child_argv);
    close(subsidiary_fd);
    subsidiary_fd = -1;

    // Relay PTY output to stdout until the child closes the terminal, then
    // reap the child and translate its wait status into this helper's exit
    // code.
    forward_pty_output(manager_fd, write_chunk_size);
    close(manager_fd);
    manager_fd = -1;

    int wait_status = 0;
    // IMGNEKO_UNCOVERED_OK_START: Interrupts and failures are hard to trigger
    while (waitpid(child_pid, &wait_status, 0) < 0) {
        if (errno == EINTR)
            continue;
        die("failed to wait for PTY child: %errno");
    }
    // IMGNEKO_UNCOVERED_OK_END
    return child_status_exit_code(wait_status);
}

int main(int argc, char **argv) {
    ParsedRunInPty opts = {0};
    int rc = opt_run_program_parser(&RunInPty_parser, argc, argv, &opts);

    if (rc != 0 || opts.must_exit)
        return rc;

    // The generic parser enforces syntax and defaults. The tool itself still
    // requires an explicit COMMAND payload after the `--` delimiter.
    rc = validate_required_arguments(&opts.top_level);
    if (rc == 0)
        rc = run_command_in_pty(&opts.top_level);

    RunInPtyOptions_deinit(&opts.top_level);
    return rc;
}
