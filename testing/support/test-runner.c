// Enable POSIX APIs used in this file (getline, strdup/strndup, setenv, etc.).
#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "util/array.h"
#include "util/file.h"
#include "util/path.h"
#include "util/string.h"

#ifndef TEST_RUNNER_ROOT_DIR
#error "TEST_RUNNER_ROOT_DIR must be defined at compile time"
#endif

#ifndef TEST_RUNNER_BUILD_DIR
#error "TEST_RUNNER_BUILD_DIR must be defined at compile time"
#endif

typedef enum TestKind {
    TEST_KIND_C,
    TEST_KIND_EXECUTABLE,
} TestKind;

typedef enum TestMarker {
    TEST_MARKER_NONE,
    TEST_MARKER_XFAIL,
    TEST_MARKER_DISABLED,
} TestMarker;

// One discovered runnable file under `testing/tests/`.
//
// rel_path is the path relative to that test root, for example:
// `integration/unit/util/string.c`
//
// abs_path is the corresponding absolute path in the working tree, for
// example:
// `/repo/testing/tests/integration/unit/util/string.c`
typedef struct TestFile {
    TestKind kind;
    TestMarker marker;
    String rel_path;
    String abs_path;
} TestFile;

// One subtest reported by a compiled C test binary.
typedef struct CSubtest {
    String name;
    TestMarker marker;
} CSubtest;

// One runnable test case expanded from a discovered file.
//
// Example for a C test with subtests:
// id:            `integration/unit/util/string.c/empty_and_reserve`
// file_id:       `integration/unit/util/string.c`
// file_abs_path: `/repo/testing/tests/integration/unit/util/string.c`
// c_exe_path:    `/repo/build/release/obj/test-bin/.../util/string.c.bin`
// c_subtest:     `empty_and_reserve`
//
// For an executable test file, id and file_id both equal the relative path, and
// c_exe_path/c_subtest are empty.
typedef struct TestCase {
    TestKind kind;
    TestMarker marker;
    String id;
    String file_id;
    String file_abs_path;
    String c_exe_path;
    String c_subtest;
} TestCase;

// Result of running one test case in a child process.
typedef struct TestRunResult {
    int exit_code;
    bool timed_out;
    double elapsed_seconds;
} TestRunResult;

DEFINE_ARRAY_TYPE(CSubtestArray, CSubtest)
DEFINE_ARRAY_TYPE(TestFileArray, TestFile)
DEFINE_ARRAY_TYPE(TestCaseArray, TestCase)

// Absolute path to the repository root (/path/to/imgneko).
static const char *const root_dir = TEST_RUNNER_ROOT_DIR;
// Absolute path to the selected build directory (for example:
// /path/to/imgneko/build/default).
static const char *const build_dir = TEST_RUNNER_BUILD_DIR;

// Repository-relative directory containing runnable tests.
static const char *const tests_root_rel = "testing/tests";
// Default directory under the build tree for per-test captured output.
static const char *const default_test_output_dir_rel = "test-outputs";
// Default per-test timeout in seconds.
static const double default_test_timeout_seconds = 180.0;

// Per-test environment variable that points tests at their own output
// directory.
static const char *const test_output_dir_env = "IMGNEKO_TEST_OUTPUT_DIR";

static void die_errno(const char *message) {
    fprintf(stderr, "error: %s: %s\n", message, strerror(errno));
    exit(1);
}

static void string_array_push_copy(StringArray *array, const char *item) {
    arr_push(*array, str_from_cstr(item));
}

static void c_subtest_array_free(CSubtestArray *array) {
    for (size_t i = 0; i < array->size; ++i)
        str_free(array->data[i].name);
    arr_free(*array);
}

static void test_file_array_free(TestFileArray *array) {
    for (size_t i = 0; i < array->size; ++i) {
        str_free(array->data[i].rel_path);
        str_free(array->data[i].abs_path);
    }
    arr_free(*array);
}

// Return the user-facing name for a test marker, or NULL for an unmarked test.
static const char *test_marker_name(TestMarker marker) {
    switch (marker) {
    case TEST_MARKER_NONE:
        return NULL;
    case TEST_MARKER_XFAIL:
        return "XFAIL";
    case TEST_MARKER_DISABLED:
        return "DISABLED";
    }

    return NULL;
}

static void test_case_array_free(TestCaseArray *array) {
    for (size_t i = 0; i < array->size; ++i) {
        str_free(array->data[i].id);
        str_free(array->data[i].file_id);
        str_free(array->data[i].file_abs_path);
        str_free(array->data[i].c_exe_path);
        str_free(array->data[i].c_subtest);
    }
    arr_free(*array);
}

// Trim leading and trailing ASCII whitespace from a string in place.
static void trim_in_place(char *line) {
    size_t len;
    size_t start = 0;
    size_t end;

    while (str_char_is_ascii_space(line[start])) {
        start++;
    }

    len = strlen(line);
    end = len;
    while (end > start && str_char_is_ascii_space(line[end - 1])) {
        end--;
    }

    if (start > 0) {
        memmove(line, line + start, end - start);
    }
    line[end - start] = '\0';
}

// Return whether a character can be part of a marker word.
static bool is_marker_word_char(char ch) {
    return str_char_is_ascii_alnum(ch) || ch == '_';
}

// Return whether `text` contains `word` delimited by non-word characters.
static bool text_contains_word(const char *text, const char *word) {
    size_t word_len = strlen(word);

    for (const char *cursor = text; (cursor = strstr(cursor, word)) != NULL;
         ++cursor) {
        bool start_ok = cursor == text || !is_marker_word_char(cursor[-1]);
        bool end_ok = !is_marker_word_char(cursor[word_len]);

        if (start_ok && end_ok)
            return true;
    }

    return false;
}

// Scan a free-form text line for supported executable-test markers.
static TestMarker test_marker_from_text_line(const char *line) {
    bool has_xfail = text_contains_word(line, "XFAIL");
    bool has_disabled = text_contains_word(line, "DISABLED");

    if (has_xfail && has_disabled) {
        fprintf(stderr, "error: ambiguous test markers in line: %s\n", line);
        exit(1);
    }
    if (has_xfail)
        return TEST_MARKER_XFAIL;
    if (has_disabled)
        return TEST_MARKER_DISABLED;
    return TEST_MARKER_NONE;
}

// Remove a trailing ` MARKER` suffix from a listed C subtest line and return
// the parsed marker.
static TestMarker strip_trailing_test_marker(char *line) {
    size_t len = strlen(line);
    const char *xfail_suffix = " XFAIL";
    const char *disabled_suffix = " DISABLED";
    size_t xfail_len = strlen(xfail_suffix);
    size_t disabled_len = strlen(disabled_suffix);

    if (len >= xfail_len && strcmp(line + len - xfail_len, xfail_suffix) == 0) {
        line[len - xfail_len] = '\0';
        trim_in_place(line);
        return TEST_MARKER_XFAIL;
    }

    if (len >= disabled_len &&
        strcmp(line + len - disabled_len, disabled_suffix) == 0) {
        line[len - disabled_len] = '\0';
        trim_in_place(line);
        return TEST_MARKER_DISABLED;
    }

    return TEST_MARKER_NONE;
}

// Read the first five lines of an executable test file and return its marker.
static TestMarker executable_test_marker(const char *path) {
    FILE *stream = fopen(path, "r");
    char *line = NULL;
    size_t line_capacity = 0;
    TestMarker marker = TEST_MARKER_NONE;
    size_t line_number = 0;

    if (stream == NULL)
        die_errno("failed to open an executable test file");

    while (line_number < 5 && getline(&line, &line_capacity, stream) >= 0) {
        TestMarker line_marker;

        line_number++;
        line_marker = test_marker_from_text_line(line);
        if (line_marker == TEST_MARKER_NONE)
            continue;

        if (marker != TEST_MARKER_NONE) {
            fprintf(stderr, "error: multiple test markers found in %s\n", path);
            free(line);
            fclose(stream);
            exit(1);
        }

        marker = line_marker;
    }

    if (ferror(stream)) {
        free(line);
        fclose(stream);
        die_errno("failed to read an executable test file");
    }

    free(line);
    fclose(stream);
    return marker;
}

// Return whether `prefix` names the same path as `path`, or a parent directory
// of it, with a component boundary at the match point.
static bool path_is_prefix(const char *prefix, const char *path) {
    size_t prefix_len = strlen(prefix);

    if (strncmp(prefix, path, prefix_len) != 0)
        return false;

    return path[prefix_len] == '\0' || path[prefix_len] == '/';
}

// Comparator for deterministic sorting of discovered files by relative path.
static int compare_test_files(const void *lhs, const void *rhs) {
    const TestFile *a = lhs;
    const TestFile *b = rhs;
    return strcmp(a->rel_path.cstr, b->rel_path.cstr);
}

// Comparator for deterministic sorting of expanded test cases by test id.
static int compare_test_cases(const void *lhs, const void *rhs) {
    const TestCase *a = lhs;
    const TestCase *b = rhs;
    return strcmp(a->id.cstr, b->id.cstr);
}

// Resolve the compiled build-dir string against the repository root when it is
// not already absolute. The caller owns the returned string and must free it
// with str_free.
static String absolute_build_dir(void) {
    String resolved = str_empty;

    if (path_is_absolute(build_dir))
        resolved = str_from_cstr(build_dir);
    else
        resolved = path_join(root_dir, build_dir);

    path_trim_trailing_slashes(&resolved);
    return resolved;
}

// Recursively discover supported test files tests_root_abs/rel_dir/** and
// append entries to `files`. `rel_dir` should be "" when called from the top
// level.
static void discover_test_files_rec(const char *tests_root_abs,
                                    const char *rel_dir, TestFileArray *files) {
    // Resolve the directory represented by rel_dir relative to tests_root_abs.
    String dir_path = rel_dir[0] == '\0' ? str_from_cstr(tests_root_abs)
                                         : path_join(tests_root_abs, rel_dir);
    struct dirent *entry;
    DIR *dir;

    dir = opendir(dir_path.cstr);
    if (dir == NULL) {
        str_free(dir_path);
        die_errno("failed to open tests directory");
    }

    while ((entry = readdir(dir)) != NULL) {
        struct stat st;
        String rel_path;
        String abs_path;
        TestKind kind;
        TestMarker marker = TEST_MARKER_NONE;

        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        rel_path = rel_dir[0] == '\0' ? str_from_cstr(entry->d_name)
                                      : path_join(rel_dir, entry->d_name);
        abs_path = path_join(tests_root_abs, rel_path.cstr);

        if (stat(abs_path.cstr, &st) != 0) {
            str_free(rel_path);
            str_free(abs_path);
            closedir(dir);
            str_free(dir_path);
            die_errno("failed to stat a test path");
        }

        if (S_ISDIR(st.st_mode)) {
            discover_test_files_rec(tests_root_abs, rel_path.cstr, files);
            str_free(rel_path);
            str_free(abs_path);
            continue;
        }

        if (!S_ISREG(st.st_mode)) {
            str_free(rel_path);
            str_free(abs_path);
            continue;
        }

        // Keep only supported test file types.
        if (ends_with_cstr(rel_path.cstr, ".c")) {
            kind = TEST_KIND_C;
        } else if (access(abs_path.cstr, X_OK) == 0) {
            kind = TEST_KIND_EXECUTABLE;
            marker = executable_test_marker(abs_path.cstr);
        } else {
            str_free(rel_path);
            str_free(abs_path);
            continue;
        }

        arr_push(*files, ((TestFile){
                             .kind = kind,
                             .marker = marker,
                             .rel_path = rel_path,
                             .abs_path = abs_path,
                         }));
    }

    closedir(dir);
    str_free(dir_path);
}

// Discover and sort test files for deterministic output and execution order.
static void discover_test_files(const char *tests_root_abs,
                                TestFileArray *files) {
    discover_test_files_rec(tests_root_abs, "", files);
    qsort(files->data, files->size, sizeof(files->data[0]), compare_test_files);
}

// Compute the default absolute directory that stores per-test output
// directories. The caller owns the returned string and must free it with
// str_free.
static String default_test_output_dir(void) {
    String build_dir_abs = absolute_build_dir();
    String output_dir =
        path_join(build_dir_abs.cstr, default_test_output_dir_rel);

    path_trim_trailing_slashes(&output_dir);
    str_free(build_dir_abs);
    return output_dir;
}

// Reject output roots that would let the runner delete the repository root,
// the whole build directory, or the filesystem root.
static void validate_output_dir(const char *output_dir) {
    String build_dir_abs = absolute_build_dir();
    bool unsafe = false;

    if (!path_is_absolute(output_dir))
        unsafe = true;

    if (strcmp(output_dir, "/") == 0 || path_is_prefix(output_dir, root_dir) ||
        path_is_prefix(output_dir, build_dir_abs.cstr)) {
        unsafe = true;
    }

    if (unsafe) {
        str_free(build_dir_abs);
        fprintf(stderr, "error: unsafe output directory: %s\n", output_dir);
        exit(1);
    }

    str_free(build_dir_abs);
}

// Parse a probability in the inclusive range [0, 1].
static bool parse_probability(const char *text, double *probability_out) {
    char *end = NULL;
    double probability;

    errno = 0;
    probability = strtod(text, &end);
    if (errno != 0 || end == text || *end != '\0' || probability < 0.0 ||
        probability > 1.0) {
        return false;
    }

    *probability_out = probability;
    return true;
}

// Parse a timeout in seconds. Zero disables the timeout.
static bool parse_timeout_seconds(const char *text, double *timeout_out) {
    char *end = NULL;
    double timeout_seconds;

    errno = 0;
    timeout_seconds = strtod(text, &end);
    if (errno != 0 || end == text || *end != '\0' || timeout_seconds < 0.0)
        return false;

    *timeout_out = timeout_seconds;
    return true;
}

// Build the per-test output directory path under output_root. Executable tests
// map to `<root>/<test-id>/`; C subtests map to
// `<root>/<file-id>/<subtest>/`. The caller owns the returned string and must
// free it with str_free.
static String test_output_dir_path(const TestCase *test_case,
                                   const char *output_root) {
    return path_join(output_root, test_case->id.cstr);
}

// Build the per-test captured output file path (`.../output`) inside a
// per-test output directory. The caller owns the returned string and must free
// it with str_free.
static String test_output_file_path(const char *test_output_dir) {
    return path_join(test_output_dir, "output");
}

// Return the current monotonic time in seconds.
static double monotonic_seconds(void) {
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        die_errno("clock_gettime failed");

    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

// pselect() only wakes for signals that are actually caught, so install a
// no-op SIGCHLD handler purely to interrupt the sleep when the child changes
// state. The main loop still does the real reap via waitpid().
static void run_argv_sigchld_handler(int signum) { (void)signum; }

// Return the relative timeout until the next monotonic deadline. Zero means
// the deadline already expired.
static struct timespec timeout_until_next_deadline(double deadline_seconds) {
    struct timespec timeout = {0};
    double remaining_seconds = deadline_seconds - monotonic_seconds();

    if (remaining_seconds <= 0.0)
        return timeout;

    timeout.tv_sec = (time_t)remaining_seconds;
    timeout.tv_nsec =
        (long)((remaining_seconds - (double)timeout.tv_sec) * 1000000000.0);
    if (timeout.tv_nsec >= 1000000000L) {
        timeout.tv_sec += timeout.tv_nsec / 1000000000L;
        timeout.tv_nsec %= 1000000000L;
    }
    if (timeout.tv_sec == 0 && timeout.tv_nsec == 0)
        timeout.tv_nsec = 1;

    return timeout;
}

// Write the full byte range to fd, retrying short writes and EINTR. Fatal on
// failure because the runner cannot recover from losing captured output.
static void write_all_or_die(int fd, const char *data, size_t len,
                             const char *message) {
    while (len != 0) {
        ssize_t written = write(fd, data, len);

        if (written < 0) {
            if (errno == EINTR)
                continue;
            die_errno(message);
        }

        data += written;
        len -= (size_t)written;
    }
}

// Read one chunk from the merged child-output pipe, append it to the captured
// output file, and optionally mirror it to the user's terminal.
static void pump_child_output(int output_pipe_fd, int output_fd,
                              bool output_passthrough,
                              bool *output_pipe_closed) {
    char buffer[4096];
    ssize_t count = read(output_pipe_fd, buffer, sizeof(buffer));

    if (count < 0) {
        if (errno == EINTR)
            return;
        die_errno("failed to read child output");
    }

    if (count == 0) {
        close(output_pipe_fd);
        *output_pipe_closed = true;
        return;
    }

    write_all_or_die(output_fd, buffer, (size_t)count,
                     "failed to write captured output");
    if (output_passthrough) {
        write_all_or_die(STDOUT_FILENO, buffer, (size_t)count,
                         "failed to pass test output through");
    }
}

// Drain any remaining child output after the child has already been terminated
// and reaped, so the output file still contains the full captured log.
static void drain_child_output_pipe(int output_pipe_fd, int output_fd,
                                    bool output_passthrough) {
    bool output_pipe_closed = false;

    while (!output_pipe_closed) {
        pump_child_output(output_pipe_fd, output_fd, output_passthrough,
                          &output_pipe_closed);
    }
}

// Run argv in a child process with stdout/stderr captured into output_path.
// The child also receives its per-test output directory and runs from it.
//
// When output_passthrough is enabled, mirror the merged child output to the
// user while still writing the same bytes into output_path.
//
// When a timeout is enabled, place the child in its own process group so a
// timeout can terminate the whole test subtree, not just the direct exec'd
// process.
static TestRunResult run_argv(char *const *argv, const char *test_output_dir,
                              const char *output_path, double timeout_seconds,
                              bool output_passthrough) {
    TestRunResult result = {.exit_code = 1, .timed_out = false};
    bool timeout_enabled = timeout_seconds > 0.0;
    bool deadline_expired = false;
    bool child_reaped = false;
    bool output_pipe_closed = false;
    struct sigaction old_sigchld_action = {0};
    sigset_t old_sigchld_mask;
    sigset_t wait_mask;
    int output_fd = -1;
    int output_pipe_fds[2] = {-1, -1};
    pid_t pid;
    double start_time;
    double deadline_seconds = 0.0;
    int status = 0;

    if (timeout_enabled) {
        struct sigaction sigchld_action = {0};
        sigset_t sigchld_mask;

        // Install a temporary SIGCHLD handler so pselect() can wake when this
        // child exits. Save the old action so the runner does not leak its
        // internal signal setup into later code.
        sigemptyset(&sigchld_action.sa_mask);
        sigchld_action.sa_handler = run_argv_sigchld_handler;
        if (sigaction(SIGCHLD, &sigchld_action, &old_sigchld_action) != 0)
            die_errno("sigaction failed");

        // Block SIGCHLD in normal execution so a child exit cannot land in the
        // tiny window between waitpid(WNOHANG) and pselect(). If that happens,
        // the signal stays pending until pselect() temporarily unblocks it.
        sigemptyset(&sigchld_mask);
        sigaddset(&sigchld_mask, SIGCHLD);
        if (sigprocmask(SIG_BLOCK, &sigchld_mask, &old_sigchld_mask) != 0)
            die_errno("sigprocmask failed");

        // pselect() takes a full replacement mask, not a "signals to unblock"
        // set, so start from the caller's original mask and only make SIGCHLD
        // unblocked while sleeping.
        wait_mask = old_sigchld_mask;
        sigdelset(&wait_mask, SIGCHLD);
    }

    if (!mkdir_p(test_output_dir))
        die_errno("failed to create a directory");
    output_fd = open(output_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    if (output_fd < 0)
        die_errno("failed to open a test output file");

    // Route both stdout and stderr through one pipe so the parent can always
    // capture the full merged output stream into output_path, and optionally
    // mirror the same bytes to the user's terminal in passthrough mode.
    if (pipe(output_pipe_fds) != 0) {
        close(output_fd);
        die_errno("pipe failed");
    }

    pid = fork();

    if (pid < 0) {
        close(output_pipe_fds[0]);
        close(output_pipe_fds[1]);
        close(output_fd);
        die_errno("fork failed");
    }

    if (pid == 0) {
        // The parent blocks SIGCHLD around its wait loop. Undo that in the
        // child before running the test so the test process inherits the
        // caller's original signal mask, not the runner's internal one.
        if (timeout_enabled &&
            sigprocmask(SIG_SETMASK, &old_sigchld_mask, NULL) != 0) {
            fprintf(stderr, "error: sigprocmask failed: %s\n", strerror(errno));
            _exit(127);
        }
        // Make the child the leader of its own process group so timeout
        // cleanup can signal the whole test subtree with kill(-pid, ...).
        // The first 0 means current pid, the second 0 means new group leader is
        // the same as the pid.
        if (setpgid(0, 0) != 0) {
            fprintf(stderr, "error: setpgid failed: %s\n", strerror(errno));
            _exit(127);
        }
        // Send both output streams into the same pipe.
        if (dup2(output_pipe_fds[1], STDOUT_FILENO) < 0 ||
            dup2(output_pipe_fds[1], STDERR_FILENO) < 0) {
            fprintf(stderr, "error: dup2 failed: %s\n", strerror(errno));
            _exit(127);
        }
        close(output_pipe_fds[0]);
        close(output_pipe_fds[1]);
        close(output_fd);
        if (setenv(test_output_dir_env, test_output_dir, 1) != 0) {
            fprintf(stderr, "error: failed to update test output env: %s\n",
                    strerror(errno));
            _exit(127);
        }
        if (chdir(test_output_dir) != 0) {
            fprintf(stderr, "error: failed to chdir to %s: %s\n",
                    test_output_dir, strerror(errno));
            _exit(127);
        }
        execvp(argv[0], argv);
        fprintf(stderr, "error: failed to exec %s: %s\n", argv[0],
                strerror(errno));
        _exit(127);
    }

    close(output_pipe_fds[1]);

    // Repeat setpgid in the parent to close the small race where timeout
    // cleanup might need to signal the process group before the child runs its
    // own setpgid call.
    // EACCES means the child already exec'd after its own setpgid(), so the
    // parent lost the race but the process-group setup step is already done.
    // ESRCH means the child exited before the parent got here, so there is no
    // remaining process to move into a group.
    if (setpgid(pid, pid) != 0 && errno != EACCES && errno != ESRCH)
        die_errno("setpgid failed");

    start_time = monotonic_seconds();
    if (timeout_enabled)
        deadline_seconds = start_time + timeout_seconds;

    // Loop until the child is reaped and the output pipe reaches EOF, or the
    // deadline expires. The child can exit before the parent consumes the last
    // buffered output bytes, so both conditions matter.
    for (;;) {
        if (!child_reaped) {
            // WNOHANG turns waitpid() into a non-blocking probe: pid means the
            // child already changed state, 0 means it is still running.
            pid_t waited = waitpid(pid, &status, WNOHANG);

            if (waited < 0) {
                // A caught signal can interrupt the probe; just retry.
                if (errno == EINTR)
                    continue;
                die_errno("waitpid failed");
            }

            if (waited == pid)
                child_reaped = true;
        }

        if (child_reaped && output_pipe_closed)
            break;

        fd_set read_fds;
        struct timespec wait_timeout = {0};
        struct timespec *wait_timeout_ptr = NULL;
        int nfds = 0;
        int rc;

        FD_ZERO(&read_fds);
        if (!output_pipe_closed) {
            FD_SET(output_pipe_fds[0], &read_fds);
            nfds = output_pipe_fds[0] + 1;
        }

        if (timeout_enabled && !child_reaped) {
            wait_timeout = timeout_until_next_deadline(deadline_seconds);

            if (wait_timeout.tv_sec == 0 && wait_timeout.tv_nsec == 0) {
                deadline_expired = true;
                break;
            }

            wait_timeout_ptr = &wait_timeout;
        }

        // Sleep until either output becomes readable, the deadline expires, or
        // SIGCHLD arrives. When nfds is 0, this intentionally degenerates into
        // an interruptible timed wait after the child has closed the pipe but
        // before waitpid() has reported the exit yet.
        rc = pselect(nfds, nfds == 0 ? NULL : &read_fds, NULL, NULL,
                     wait_timeout_ptr, timeout_enabled ? &wait_mask : NULL);

        if (rc == 0) {
            // The timeout expired before any output readiness or SIGCHLD wakeup
            deadline_expired = true;
            break;
        }
        if (rc < 0) {
            // EINTR is the normal wakeup path after SIGCHLD (or another caught
            // signal); loop back and probe waitpid() again.
            if (errno == EINTR)
                continue;
            die_errno("pselect failed");
        }

        if (!output_pipe_closed && FD_ISSET(output_pipe_fds[0], &read_fds)) {
            // Consume one available chunk, append it to the per-test output
            // file, and optionally pass it through to the user immediately.
            pump_child_output(output_pipe_fds[0], output_fd, output_passthrough,
                              &output_pipe_closed);
        }

        // Keep SIGCHLD blocked between the WNOHANG probe and pselect(). That
        // way a child exit in that window becomes a pending SIGCHLD, and
        // pselect() wakes immediately when it temporarily unblocks SIGCHLD via
        // wait_mask.
    }

    // Restore the caller's signal state now that this child is no longer being
    // supervised by the pselect()/SIGCHLD timeout machinery.
    if (timeout_enabled &&
        sigprocmask(SIG_SETMASK, &old_sigchld_mask, NULL) != 0) {
        die_errno("sigprocmask restore failed");
    }
    if (timeout_enabled && sigaction(SIGCHLD, &old_sigchld_action, NULL) != 0) {
        die_errno("sigaction restore failed");
    }

    result.elapsed_seconds = monotonic_seconds() - start_time;
    if (deadline_expired) {
        result.timed_out = true;
        // Use 124 as the synthetic timeout status. This matches the common
        // shell convention used by tools like `timeout`.
        result.exit_code = 124;
        // Kill the whole process group in case the test spawned children that
        // would otherwise outlive the direct runner child.
        if (kill(-pid, SIGTERM) != 0 && errno != ESRCH)
            die_errno("kill failed");
        // Give the test subtree a brief chance to exit cleanly on SIGTERM
        // before forcing it down with SIGKILL.
        struct timespec grace = {.tv_sec = 0, .tv_nsec = 100000000};
        nanosleep(&grace, NULL);
        if (kill(-pid, SIGKILL) != 0 && errno != ESRCH)
            die_errno("kill failed");
        // Reap the direct child so we do not leave a zombie behind after the
        // timeout path finishes. ECHILD means it was already reaped elsewhere
        // in the timeout race, which is fine.
        if (!child_reaped && waitpid(pid, &status, 0) < 0 && errno != ECHILD)
            die_errno("waitpid failed after timeout");
        // A timeout can kill the child before the parent has drained all bytes
        // already buffered in the pipe, so finish draining them before closing
        // the output file.
        if (!output_pipe_closed) {
            // REVIEW: What if the child launches a process that just produces
            // an output but cannot be killed via killing that child? We will
            // get stuck here forever, maybe we need to add a timeout or
            // something to this drainage procedure itself?
            drain_child_output_pipe(output_pipe_fds[0], output_fd,
                                    output_passthrough);
        }
        close(output_fd);
        result.elapsed_seconds = monotonic_seconds() - start_time;
        return result;
    }

    close(output_fd);

    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
        return result;
    }

    if (WIFSIGNALED(status)) {
        result.exit_code = 128 + WTERMSIG(status);
        return result;
    }

    return result;
}

// Run argv, capture the child's stdout lines, and return an exit-like status.
// On success, this frees the previous contents of `*stdout_lines`.
static int run_argv_capture_stdout_lines(char *const *argv,
                                         StringArray *stdout_lines) {
    int pipe_fds[2];
    FILE *stream = NULL;
    pid_t pid;
    int status;

    if (pipe(pipe_fds) != 0)
        die_errno("pipe failed");

    pid = fork();
    if (pid < 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        die_errno("fork failed");
    }

    if (pid == 0) {
        // Redirect stdout to the pipe and run the command.
        if (dup2(pipe_fds[1], STDOUT_FILENO) < 0) {
            fprintf(stderr, "error: dup2 failed: %s\n", strerror(errno));
            _exit(127);
        }
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        execvp(argv[0], argv);
        fprintf(stderr, "error: failed to exec %s: %s\n", argv[0],
                strerror(errno));
        _exit(127);
    }

    close(pipe_fds[1]);
    stream = fdopen(pipe_fds[0], "r");
    if (stream == NULL) {
        close(pipe_fds[0]);
        die_errno("fdopen failed");
    }

    if (!file_read_stream_lines(stdout_lines, stream, -1)) {
        int read_errno = errno;

        fclose(stream);
        if (waitpid(pid, &status, 0) < 0)
            die_errno("waitpid failed");
        errno = read_errno;
        die_errno("failed to read child stdout");
    }
    fclose(stream);

    if (waitpid(pid, &status, 0) < 0) {
        die_errno("waitpid failed");
    }

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }

    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }

    return 1;
}

// Print the failing output path and the last max_lines lines so users do not
// need to open the full file just to see the failure context.
static void print_output_tail(const char *output_path, size_t max_lines) {
    StringArray lines = arr_empty;

    if (!file_read_lines(&lines, output_path, (ptrdiff_t)max_lines)) {
        fprintf(stderr, "error: failed to read captured output %s: %s\n\n",
                output_path, strerror(errno));
        return;
    }

    if (lines.size == 0) {
        fprintf(stderr, "output is empty: %s\n", output_path);
        str_array_free(&lines);
        return;
    }

    fprintf(stderr, "===== LAST %zu LINES OF TEST OUTPUT %s {{{ =====\n",
            max_lines, output_path);
    for (size_t i = 0; i < lines.size; ++i) {
        str_trim_trailing_chars(&lines.data[i], "\r\n");
        String escaped =
            str_from_escaped_bytes(lines.data[i].cstr, lines.data[i].len);
        fprintf(stderr, "%s\n", escaped.cstr);
        str_free(escaped);
    }
    fprintf(stderr, "===== }}} END TEST OUTPUT =====\n\n");

    str_array_free(&lines);
}

// Check whether a test case matches a pattern.
//
// C tests with discovered subtests have two stable identifiers:
// - id:      `foo.c/subtest_name`
// - file_id: `foo.c`
//
// Matching succeeds if at least one of these two matches the pattern.
static bool test_matches_pattern(const TestCase *test_case,
                                 const char *pattern) {
    if (fnmatch(pattern, test_case->id.cstr, 0) == 0) {
        return true;
    }

    // For subtests, also allow matching the parent file id.
    if (strcmp(test_case->file_id.cstr, test_case->id.cstr) != 0 &&
        fnmatch(pattern, test_case->file_id.cstr, 0) == 0) {
        return true;
    }

    return false;
}

// Check whether a test matches any filter pattern (supporting '|' alternation).
static bool test_matches_filters(const TestCase *test_case,
                                 const StringArray *filters) {
    size_t i;

    if (filters->size == 0)
        return true;

    for (i = 0; i < filters->size; ++i) {
        const char *pattern = filters->data[i].cstr;
        const char *cursor = pattern;

        while (*cursor != '\0') {
            const char *bar = strchr(cursor, '|');
            size_t len = bar != NULL ? (size_t)(bar - cursor) : strlen(cursor);
            String part = str_from_data(cursor, len);

            if (part.len != 0 && test_matches_pattern(test_case, part.cstr)) {
                str_free(part);
                return true;
            }

            str_free(part);
            if (bar == NULL)
                break;
            cursor = bar + 1;
        }
    }

    return false;
}

// Build the expected on-disk path for a compiled C test binary.
// The caller owns the returned string and must free it with str_free.
static String c_test_output_path(const char *rel_path) {
    String build_dir_abs = absolute_build_dir();
    String base = path_join(build_dir_abs.cstr, "obj/test-bin");
    String final_path = path_join(base.cstr, rel_path);

    str_append_cstr(final_path, ".bin");
    str_free(base);
    str_free(build_dir_abs);
    return final_path;
}

// Build a user-facing command hint for building C test binaries.
// The caller owns the returned string and must free it with str_free.
static String c_test_build_hint(void) {
    String build_dir_abs = absolute_build_dir();
    String hint = str_from_cstr("make -C ");

    str_append_cstr(hint, build_dir_abs.cstr);
    str_append_cstr(hint, " test-programs");
    str_free(build_dir_abs);
    return hint;
}

// Ensure the compiled C test binary exists and is executable.
static void require_c_test_binary(const TestFile *file, const char *exe_path) {
    if (access(exe_path, X_OK) != 0) {
        String hint = c_test_build_hint();

        fprintf(stderr,
                "error: missing built C test binary for %s at %s\n"
                "       build C tests first with `%s` or `make -C %s test`\n",
                file->rel_path.cstr, exe_path, hint.cstr, build_dir);
        str_free(hint);
        exit(1);
    }
}

// Collect subtests from a compiled C test binary by executing it with `--list`.
// Writes the executable path to `*exe_path_out`; caller owns it and frees it
// with str_free.
static void c_test_subtests(const TestFile *file, CSubtestArray *subtests,
                            String *exe_path_out) {
    StringArray stdout_lines = arr_empty;
    String exe_path = c_test_output_path(file->rel_path.cstr);
    int status;
    char *argv[] = {exe_path.cstr, "--list", NULL};

    require_c_test_binary(file, exe_path.cstr);

    status = run_argv_capture_stdout_lines(argv, &stdout_lines);
    if (status != 0) {
        fprintf(stderr, "error: %s --list failed with status %d\n",
                file->rel_path.cstr, status);
        str_array_free(&stdout_lines);
        str_free(exe_path);
        exit(status);
    }

    for (size_t i = 0; i < stdout_lines.size; ++i) {
        str_trim_trailing_chars(&stdout_lines.data[i], "\r\n");
        char *line = stdout_lines.data[i].cstr;

        trim_in_place(line);
        if (line[0] != '\0') {
            TestMarker marker = strip_trailing_test_marker(line);

            if (line[0] == '\0') {
                fprintf(stderr, "error: invalid empty subtest name in %s\n",
                        file->rel_path.cstr);
                str_array_free(&stdout_lines);
                str_free(exe_path);
                c_subtest_array_free(subtests);
                exit(1);
            }

            arr_push(*subtests, ((CSubtest){
                                    .name = str_from_cstr(line),
                                    .marker = marker,
                                }));
        }
    }

    str_array_free(&stdout_lines);
    *exe_path_out = exe_path;
}

// Expand discovered files into runnable test cases.
static void discover_test_cases(const TestFileArray *files,
                                TestCaseArray *cases) {
    size_t i;

    for (i = 0; i < files->size; ++i) {
        const TestFile *file = &files->data[i];

        if (file->kind == TEST_KIND_C) {
            CSubtestArray subtests = arr_empty;
            String exe_path = str_empty;
            size_t j;

            // A C test binary can expose multiple subtests via --list.
            c_test_subtests(file, &subtests, &exe_path);
            if (subtests.size == 0) {
                arr_push(*cases, ((TestCase){
                                     .kind = TEST_KIND_C,
                                     .marker = file->marker,
                                     .id = copy_str(file->rel_path),
                                     .file_id = copy_str(file->rel_path),
                                     .file_abs_path = copy_str(file->abs_path),
                                     .c_exe_path = copy_str(exe_path),
                                     .c_subtest = str_empty,
                                 }));
            } else {
                for (j = 0; j < subtests.size; ++j) {
                    String id = copy_str(file->rel_path);

                    str_push(id, '/');
                    str_append_str(id, subtests.data[j].name);
                    arr_push(*cases,
                             ((TestCase){
                                 .kind = TEST_KIND_C,
                                 .marker = subtests.data[j].marker,
                                 .id = id,
                                 .file_id = copy_str(file->rel_path),
                                 .file_abs_path = copy_str(file->abs_path),
                                 .c_exe_path = copy_str(exe_path),
                                 .c_subtest = copy_str(subtests.data[j].name),
                             }));
                }
            }

            str_free(exe_path);
            c_subtest_array_free(&subtests);
            continue;
        }

        arr_push(*cases, ((TestCase){
                             .kind = file->kind,
                             .marker = file->marker,
                             .id = copy_str(file->rel_path),
                             .file_id = copy_str(file->rel_path),
                             .file_abs_path = copy_str(file->abs_path),
                             .c_exe_path = str_empty,
                             .c_subtest = str_empty,
                         }));
    }

    qsort(cases->data, cases->size, sizeof(cases->data[0]), compare_test_cases);
}

// Run one executable test file directly.
static TestRunResult run_executable_test(const TestCase *test_case,
                                         const char *test_output_dir,
                                         const char *output_path,
                                         double timeout_seconds,
                                         bool output_passthrough) {
    char *argv[] = {test_case->file_abs_path.cstr, NULL};

    if (access(test_case->file_abs_path.cstr, X_OK) != 0) {
        fprintf(stderr, "error: test file is not executable: %s\n",
                test_case->file_abs_path.cstr);
        return (TestRunResult){.exit_code = 1};
    }

    return run_argv(argv, test_output_dir, output_path, timeout_seconds,
                    output_passthrough);
}

// Run one compiled C test, either a selected subtest or all subtests.
static TestRunResult run_c_test(const TestCase *test_case,
                                const char *test_output_dir,
                                const char *output_path, double timeout_seconds,
                                bool output_passthrough) {
    char *argv[] = {
        test_case->c_exe_path.cstr,
        test_case->c_subtest.len != 0 ? test_case->c_subtest.cstr : "--all",
        NULL,
    };

    return run_argv(argv, test_output_dir, output_path, timeout_seconds,
                    output_passthrough);
}

// Print one discovered test id, appending its marker when present.
static void print_listed_test(const TestCase *test_case) {
    const char *marker_name = test_marker_name(test_case->marker);

    if (marker_name == NULL)
        puts(test_case->id.cstr);
    else
        printf("%s %s\n", test_case->id.cstr, marker_name);
}

// Print a named list of tests with a heading, skipping the list if empty.
static void print_named_test_list(const char *heading,
                                  const StringArray *tests) {
    if (tests->size == 0)
        return;

    printf("\n%s:\n", heading);
    for (size_t i = 0; i < tests->size; ++i)
        printf("  %s\n", tests->data[i].cstr);
}

// Print one non-zero summary counter.
static void print_summary_count(const char *label, size_t count) {
    if (count != 0)
        printf("  %s: %zu\n", label, count);
}

// Seed the debug-only pseudo-random exit-code perturbation once per process.
static void seed_debug_random(void) {
    static bool seeded = false;

    if (seeded)
        return;

    srand((unsigned int)time(NULL) ^ (unsigned int)getpid());
    seeded = true;
}

// Randomly flip a test's raw exit code according to the requested probability.
static int maybe_flip_exit_code(const TestCase *test_case, int test_exit_code,
                                double flip_probability) {
    if (flip_probability <= 0.0)
        return test_exit_code;

    if (flip_probability < 1.0) {
        double sample = (double)rand() / ((double)RAND_MAX + 1.0);
        if (sample >= flip_probability)
            return test_exit_code;
    }

    int flipped_exit_code = test_exit_code == 0 ? 1 : 0;
    printf("DEBUG: flipped exit code for %s (%d -> %d)\n", test_case->id.cstr,
           test_exit_code, flipped_exit_code);
    return flipped_exit_code;
}

// Prepend build/bin to PATH and export stable test-runner environment
// variables.
static void prepare_env_vars(void) {
    const char *old_path = getenv("PATH");
    String build_dir_abs = absolute_build_dir();
    String bin_dir = path_join(build_dir_abs.cstr, "bin");
    String new_path = copy_str(bin_dir);

    if (old_path != NULL && old_path[0] != '\0') {
        str_push(new_path, ':');
        str_append_cstr(new_path, old_path);
    }

    if (setenv("PATH", new_path.cstr, 1) != 0) {
        str_free(bin_dir);
        str_free(new_path);
        die_errno("failed to update PATH");
    }
    // Remove inherited make variables to avoid affecting nested make calls from
    // tests.
    if (unsetenv("BUILD_DIR") != 0 || unsetenv("MAKEFLAGS") != 0 ||
        unsetenv("MAKEOVERRIDES") != 0 || unsetenv("MFLAGS") != 0 ||
        unsetenv("MAKELEVEL") != 0 || unsetenv(test_output_dir_env) != 0) {
        str_free(bin_dir);
        str_free(new_path);
        die_errno("failed to clear inherited make state");
    }
    if (setenv("IMGNEKO_ROOT_DIR", root_dir, 1) != 0 ||
        setenv("IMGNEKO_BUILD_DIR", build_dir_abs.cstr, 1) != 0) {
        str_free(build_dir_abs);
        str_free(bin_dir);
        str_free(new_path);
        die_errno("failed to update test environment");
    }

    str_free(build_dir_abs);
    str_free(bin_dir);
    str_free(new_path);
}

// Print CLI usage help.
static void usage(FILE *stream) {
    String default_output_dir = default_test_output_dir();

    fprintf(stream,
            "Usage: %s [--list] [--all] [--output-dir DIR] [--filter PATTERN]\n"
            "       [--timeout SECONDS] [-p|--output-passthrough]\n"
            "       [--debug-flip-exit-probability P]\n"
            "       [PATTERN ...]\n"
            "\n"
            "Discover tests under %s/ relative to %s.\n"
            "\n"
            "Use -p/--output-passthrough to mirror test stdout/stderr live.\n"
            "Patterns use shell-style wildcards and may be joined with '|'.\n"
            "Default output dir: %s\n"
            "Default timeout: %.0f seconds\n",
            "test-runner", tests_root_rel, root_dir, default_output_dir.cstr,
            default_test_timeout_seconds);

    str_free(default_output_dir);
}

int main(int argc, char **argv) {
    // User-specified filters and options.
    StringArray filters = arr_empty;
    bool list_only = false;
    bool run_all = false;
    bool output_passthrough = false;
    String output_dir = str_empty;
    double timeout_seconds = default_test_timeout_seconds;
    double debug_flip_exit_probability = 0.0;

    // Collected files and test cases.
    TestFileArray files = arr_empty;
    TestCaseArray cases = arr_empty;

    // The directory where we look for test files.
    String tests_dir = str_empty;

    // Results
    double run_start_seconds = monotonic_seconds();
    int exit_code = 0;
    size_t discovered = 0;
    size_t passed = 0;
    size_t xfailed = 0;
    size_t disabled = 0;
    size_t xpassed = 0;
    size_t timed_out = 0;
    size_t failed = 0;
    StringArray failed_tests = arr_empty;
    StringArray xpassed_tests = arr_empty;
    StringArray timed_out_tests = arr_empty;

    // Parse CLI arguments.

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--list") == 0) {
            list_only = true;
            continue;
        }
        if (strcmp(argv[i], "--all") == 0) {
            run_all = true;
            continue;
        }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(stdout);
            exit_code = 0;
            goto cleanup;
        }
        if (strcmp(argv[i], "-p") == 0 ||
            strcmp(argv[i], "--output-passthrough") == 0) {
            output_passthrough = true;
            continue;
        }
        if (strcmp(argv[i], "--output-dir") == 0) {
            if (i + 1 >= argc) {
                usage(stderr);
                exit_code = 1;
                goto cleanup;
            }
            if (!path_resolve_absolute(&output_dir, argv[++i]))
                die_errno("failed to resolve output directory");
            continue;
        }
        if (strncmp(argv[i], "--output-dir=", 13) == 0) {
            if (!path_resolve_absolute(&output_dir, argv[i] + 13))
                die_errno("failed to resolve output directory");
            continue;
        }
        if (strcmp(argv[i], "--filter") == 0) {
            if (i + 1 >= argc) {
                usage(stderr);
                exit_code = 1;
                goto cleanup;
            }
            string_array_push_copy(&filters, argv[++i]);
            continue;
        }
        if (strncmp(argv[i], "--filter=", 9) == 0) {
            string_array_push_copy(&filters, argv[i] + 9);
            continue;
        }
        if (strcmp(argv[i], "--timeout") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --timeout requires a value\n");
                usage(stderr);
                exit_code = 1;
                goto cleanup;
            }
            if (!parse_timeout_seconds(argv[++i], &timeout_seconds)) {
                fprintf(stderr, "error: invalid --timeout value: %s\n",
                        argv[i]);
                usage(stderr);
                exit_code = 1;
                goto cleanup;
            }
            continue;
        }
        if (strncmp(argv[i], "--timeout=", 10) == 0) {
            if (!parse_timeout_seconds(argv[i] + 10, &timeout_seconds)) {
                fprintf(stderr, "error: invalid --timeout value: %s\n",
                        argv[i] + 10);
                usage(stderr);
                exit_code = 1;
                goto cleanup;
            }
            continue;
        }
        if (strcmp(argv[i], "--debug-flip-exit-probability") == 0) {
            if (i + 1 >= argc) {
                fprintf(
                    stderr,
                    "error: --debug-flip-exit-probability requires a value\n");
                exit_code = 1;
                goto cleanup;
            }
            if (!parse_probability(argv[++i], &debug_flip_exit_probability)) {
                fprintf(stderr,
                        "error: invalid --debug-flip-exit-probability value: "
                        "%s\n",
                        argv[i]);
                usage(stderr);
                exit_code = 1;
                goto cleanup;
            }
            continue;
        }
        if (strncmp(argv[i], "--debug-flip-exit-probability=", 30) == 0) {
            if (!parse_probability(argv[i] + 30,
                                   &debug_flip_exit_probability)) {
                fprintf(stderr,
                        "error: invalid --debug-flip-exit-probability value: "
                        "%s\n",
                        argv[i] + 30);
                usage(stderr);
                exit_code = 1;
                goto cleanup;
            }
            continue;
        }
        if (argv[i][0] == '-') {
            fprintf(stderr, "error: unknown option: %s\n", argv[i]);
            usage(stderr);
            exit_code = 1;
            goto cleanup;
        }
        string_array_push_copy(&filters, argv[i]);
    }

    if (run_all && filters.size != 0) {
        fprintf(stderr, "error: --all cannot be combined with --filter or "
                        "positional patterns\n");
        usage(stderr);
        exit_code = 1;
        goto cleanup;
    }

    if (output_dir.len == 0)
        output_dir = default_test_output_dir();
    validate_output_dir(output_dir.cstr);
    if (debug_flip_exit_probability > 0.0)
        seed_debug_random();

    // Discover tests.

    prepare_env_vars();
    tests_dir = path_join(root_dir, tests_root_rel);

    if (chdir(root_dir) != 0) {
        str_free(tests_dir);
        die_errno("failed to chdir to repository root");
    }

    discover_test_files(tests_dir.cstr, &files);
    if (files.size == 0) {
        fprintf(stderr, "error: no tests found under %s\n", tests_dir.cstr);
        exit_code = 1;
        goto cleanup;
    }

    discover_test_cases(&files, &cases);
    if (cases.size == 0) {
        fprintf(stderr, "error: no runnable tests found under %s\n",
                tests_dir.cstr);
        exit_code = 1;
        goto cleanup;
    }

    for (size_t i = 0; i < cases.size; ++i) {
        TestCase *test_case = &cases.data[i];
        if (!test_matches_filters(test_case, &filters))
            continue;

        discovered++;
        if (list_only) {
            print_listed_test(test_case);
            continue;
        }

        if (test_case->marker == TEST_MARKER_DISABLED) {
            disabled++;
            printf("DISABLED: %s\n", test_case->id.cstr);
            fflush(stdout);
            continue;
        }

        printf("RUN: %s\n", test_case->id.cstr);
        fflush(stdout);

        TestRunResult run_result = {.exit_code = 1};
        String test_output_dir =
            test_output_dir_path(test_case, output_dir.cstr);
        String output_path = test_output_file_path(test_output_dir.cstr);
        if (test_case->kind == TEST_KIND_C) {
            run_result =
                run_c_test(test_case, test_output_dir.cstr, output_path.cstr,
                           timeout_seconds, output_passthrough);
        } else if (test_case->kind == TEST_KIND_EXECUTABLE) {
            run_result = run_executable_test(test_case, test_output_dir.cstr,
                                             output_path.cstr, timeout_seconds,
                                             output_passthrough);
        } else {
            run_result.exit_code = 1;
        }
        if (!run_result.timed_out) {
            run_result.exit_code = maybe_flip_exit_code(
                test_case, run_result.exit_code, debug_flip_exit_probability);
        }

        if (run_result.timed_out) {
            timed_out++;
            string_array_push_copy(&timed_out_tests, test_case->id.cstr);
            printf("\nTIMEOUT: %s\n", test_case->id.cstr);
            if (!output_passthrough)
                print_output_tail(output_path.cstr, 20);
        } else if (run_result.exit_code == 0 &&
                   test_case->marker == TEST_MARKER_XFAIL) {
            xpassed++;
            string_array_push_copy(&xpassed_tests, test_case->id.cstr);
            printf("XPASS: %s\n", test_case->id.cstr);
        } else if (run_result.exit_code == 0) {
            passed++;
            printf("PASS: %s\n", test_case->id.cstr);
        } else if (test_case->marker == TEST_MARKER_XFAIL) {
            xfailed++;
            printf("XFAIL: %s\n", test_case->id.cstr);
        } else {
            failed++;
            string_array_push_copy(&failed_tests, test_case->id.cstr);
            printf("\nFAIL: %s\n", test_case->id.cstr);
            if (!output_passthrough)
                print_output_tail(output_path.cstr, 20);
        }
        str_free(test_output_dir);
        str_free(output_path);
        fflush(stdout);
    }

    if (discovered == 0) {
        if (!list_only) {
            fprintf(stderr, "error: no tests matched the requested filters\n");
            exit_code = 1;
        }
        goto cleanup;
    }

    if (!list_only) {
        double total_run_seconds = monotonic_seconds() - run_start_seconds;

        print_named_test_list("timed out tests", &timed_out_tests);
        print_named_test_list("failed tests", &failed_tests);
        print_named_test_list("xpassed tests", &xpassed_tests);
        printf("\nSummary:\n");
        print_summary_count("discovered", discovered);
        print_summary_count("passed", passed);
        print_summary_count("xfailed", xfailed);
        print_summary_count("disabled", disabled);
        print_summary_count("xpassed", xpassed);
        print_summary_count("timeout", timed_out);
        print_summary_count("failed", failed);
        if (failed != 0 || xpassed != 0 || timed_out != 0)
            exit_code = 1;
        printf("\n");
        printf("Time: %.3f s\n", total_run_seconds);
        printf("Result: %s\n", exit_code == 0 ? "SUCCESS" : "FAILURE");
    }

cleanup:
    str_array_free(&timed_out_tests);
    str_array_free(&xpassed_tests);
    str_array_free(&failed_tests);
    str_free(output_dir);
    str_free(tests_dir);
    str_array_free(&filters);
    test_case_array_free(&cases);
    test_file_array_free(&files);
    return exit_code;
}
