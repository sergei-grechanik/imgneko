// Enable POSIX APIs used in this file (getline, strdup/strndup, setenv, etc.).
#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <float.h>
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
#include "util/error.h"
#include "util/file.h"
#include "util/path.h"
#include "util/string.h"
#include "util/time.h"

#ifndef TEST_RUNNER_ROOT_DIR
#error "TEST_RUNNER_ROOT_DIR must be defined at compile time"
#endif

#ifndef TEST_RUNNER_BUILD_DIR
#error "TEST_RUNNER_BUILD_DIR must be defined at compile time"
#endif

#ifndef TEST_RUNNER_DEFAULT_JOBS
#error "TEST_RUNNER_DEFAULT_JOBS must be defined at compile time"
#endif

#if TEST_RUNNER_DEFAULT_JOBS <= 0
#error "TEST_RUNNER_DEFAULT_JOBS must be a positive integer"
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

typedef enum TestOutcomeKind {
    TEST_OUTCOME_PASS,
    TEST_OUTCOME_XFAIL,
    TEST_OUTCOME_DISABLED,
    TEST_OUTCOME_XPASS,
    TEST_OUTCOME_TIMEOUT,
    TEST_OUTCOME_FAIL,
} TestOutcomeKind;

// A combination of a test case's run result and its outcome classification.
typedef struct ClassifiedTestResult {
    TestRunResult run_result;
    TestOutcomeKind outcome;
} ClassifiedTestResult;

// Per-test launch configuration shared by executable and C test child startup.
typedef struct TestRunConfig {
    const char *test_output_dir;
    const char *output_file_path;
    double timeout_seconds;
    double debug_parent_setpgid_delay_seconds;
} TestRunConfig;

// Parsed command-line options plus their derived filter/path state.
typedef struct CliOptions {
    StringArray raw_filters;
    StringArray filters;
    bool list_only;
    bool run_all;
    bool output_passthrough;
    int jobs;
    String tests_dir;
    String output_dir;
    String test_bin_dir;
    double timeout_seconds;
    double debug_flip_exit_probability;
    double debug_parent_setpgid_delay_seconds;
    double debug_parent_output_chunk_delay_seconds;
} CliOptions;

// Signal state for the main wait loop while test children exist.
//
// The parent blocks SIGCHLD between waitpid(WNOHANG) polls and then passes
// wait_mask to pselect() so SIGCHLD is temporarily unblocked during the sleep.
// That avoids missing child exits in the small window between polling and
// sleeping, while still restoring the caller's original signal state at exit.
typedef struct TestSignalState {
    // SIGCHLD action before the runner installed its wakeup handler.
    struct sigaction old_sigchld_action;
    // Signal mask before the runner blocked SIGCHLD in the parent.
    sigset_t old_sigchld_mask;
    // Mask used during pselect(): same as old_sigchld_mask, but with SIGCHLD
    // unblocked so child exits interrupt the sleep immediately.
    sigset_t wait_mask;
    // Whether signal_state_init() ran successfully and cleanup must restore
    // signal state.
    bool installed;
} TestSignalState;

// One currently running/supervised test child plus its captured-output state.
//
// Lifecycle:
// 1. start_test_process() fills this after fork/exec setup succeeds.
// 2. wait_for_running_test_events() drains output and advances timeout state.
// 3. reap_running_tests() stores the wait status once the child exits.
// 4. finalize_completed_running_tests() records the result after the child is
//    reaped and the merged output pipe reaches EOF.
typedef struct RunningTest {
    const TestCase *test_case;

    // Per-test directory exposed to the child via IMGNEKO_TEST_OUTPUT_DIR and
    // used as its working directory.
    String test_output_dir;
    // Captured merged stdout/stderr file under test_output_dir, usually
    // `<test_output_dir>/output`.
    String output_file_path;

    pid_t pid;
    // Writable file descriptor for output_file_path in the parent.
    int output_fd;
    // Read end of the merged stdout/stderr pipe from the child process.
    int output_pipe_fd;

    // waitpid() status once child_reaped becomes true.
    int status;

    double start_time_seconds;
    // Initial timeout deadline for this test, or 0 when timeouts are disabled.
    double deadline_seconds;
    // Grace deadline after SIGTERM before escalating to SIGKILL.
    double sigkill_deadline_seconds;
    // Deadline for giving up on additional pipe output after a timeout.
    double output_drain_deadline_seconds;

    bool child_reaped;
    bool output_pipe_closed;
    bool timed_out;
    bool sigkill_sent;
} RunningTest;

// Summary counters accumulated across a whole test run.
typedef struct RunSummary {
    size_t passed;
    size_t xfailed;
    size_t disabled;
    size_t xpassed;
    size_t timed_out;
    size_t failed;
    StringArray failed_tests;
    StringArray xpassed_tests;
    StringArray timed_out_tests;
} RunSummary;

DEFINE_ARRAY_TYPE(CSubtestArray, CSubtest)
typedef const TestCase *TestCasePtr;
DEFINE_ARRAY_TYPE(TestCasePtrArray, TestCasePtr)
DEFINE_ARRAY_TYPE(RunningTestArray, RunningTest)
DEFINE_ARRAY_TYPE(TestFileArray, TestFile)
DEFINE_ARRAY_TYPE(TestCaseArray, TestCase)

// Mutable process-wide state for one test-runner invocation.
typedef struct TestRunnerState {
    TestFileArray files;
    TestCaseArray cases;
    TestCasePtrArray selected_cases;
    RunningTestArray running_tests;
    RunSummary summary;
    size_t discovered;
    double run_start_seconds;
    TestSignalState signal_state;
} TestRunnerState;

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
// After a timeout sends SIGTERM, wait briefly before escalating to SIGKILL.
static const double timeout_sigkill_grace_seconds = 0.1;
// After killing a timed-out test, keep draining buffered output only briefly so
// a detached descendant that inherited the pipe cannot hang the runner forever.
static const double timeout_output_drain_grace_seconds = 0.25;

// Per-test environment variable that points tests at their own output
// directory.
static const char *const test_output_dir_env = "IMGNEKO_TEST_OUTPUT_DIR";

// Intentionally never called. The coverage-ignore regression keeps this helper
// uncovered so the repo-level ignore list can prove that it suppresses branch
// and function findings without hiding uncovered line entries.
bool coverage_ignore_probe(bool arg)
// IMGNEKO_UNCOVERED_OK_START
{
    return arg;
}
// IMGNEKO_UNCOVERED_OK_END

// Intentionally never called. Start/end suppression should hide every
// uncovered finding in this small helper.
// IMGNEKO_UNCOVERED_OK_START
bool uncovered_ok_range_probe(bool range_uncovered_branch) {
    if (range_uncovered_branch)
        return true;
    return false;
}
// IMGNEKO_UNCOVERED_OK_END

// Intentionally never called. Counted suppression should hide the marker line
// plus this whole helper body.
// IMGNEKO_UNCOVERED_OK[5 lines]
bool uncovered_ok_count_probe(bool count_uncovered_branch) {
    if (count_uncovered_branch)
        return true;
    return false;
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
    // IMGNEKO_UNCOVERED_OK
    switch (marker) {
    case TEST_MARKER_NONE:
        return NULL;
    case TEST_MARKER_XFAIL:
        return "XFAIL";
    case TEST_MARKER_DISABLED:
        return "DISABLED";
    }

    // IMGNEKO_UNCOVERED_OK
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

    require(stream != NULL, "failed to open an executable test file: %errno");

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

    // IMGNEKO_UNCOVERED_OK_START
    if (ferror(stream)) {
        free(line);
        fclose(stream);
        die_errno("failed to read an executable test file");
    }
    // IMGNEKO_UNCOVERED_OK_END

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
    String resolved = str_from_cstr(build_dir);

    require(path_is_absolute(build_dir),
            "TEST_RUNNER_BUILD_DIR must be absolute");

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

        // IMGNEKO_UNCOVERED_OK_START
        if (stat(abs_path.cstr, &st) != 0) {
            str_free(rel_path);
            str_free(abs_path);
            closedir(dir);
            str_free(dir_path);
            die_errno("failed to stat a test path");
        }
        // IMGNEKO_UNCOVERED_OK_END

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

// Compute the default absolute directory that stores discovered tests. The
// caller owns the returned string and must free it with str_free.
static String default_tests_dir(void) {
    return path_join(root_dir, tests_root_rel);
}

// Compute the default absolute directory that stores compiled C test binaries.
// The caller owns the returned string and must free it with str_free.
static String default_test_bin_dir(void) {
    String test_bin_dir = absolute_build_dir();
    path_append(&test_bin_dir, "obj/test-bin");
    return test_bin_dir;
}

// Require the selected output root to be absent or empty so a new run never
// mixes fresh results with leftover files from an earlier invocation.
static void require_empty_output_dir(const char *output_dir) {
    struct stat st;
    DIR *dir;
    bool has_entries = false;
    int readdir_errno = 0;
    String rm_command = str_from_cstr("rm -r ");

    if (stat(output_dir, &st) != 0) {
        require(errno == ENOENT, "failed to stat output directory: %errno");
        str_free(rm_command);
        return;
    }

    str_append_shell_quoted_word(&rm_command, output_dir);

    if (!S_ISDIR(st.st_mode)) {
        fprintf(stderr,
                "error: test output path exists and is not a directory: %s\n"
                "       remove it first with %s\n",
                output_dir, rm_command.cstr);
        str_free(rm_command);
        exit(1);
    }

    dir = opendir(output_dir);
    if (dir == NULL)
        die_errno("failed to open output directory");

    errno = 0;
    for (;;) {
        struct dirent *entry = readdir(dir);

        if (entry == NULL)
            break;
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        has_entries = true;
        break;
    }

    readdir_errno = errno;
    // IMGNEKO_UNCOVERED_OK_START
    if (!has_entries && readdir_errno != 0) {
        errno = readdir_errno;
        die("failed to read output directory: %errno");
    }
    // IMGNEKO_UNCOVERED_OK_END
    require(closedir(dir) == 0, "failed to close output directory: %errno");

    if (has_entries) {
        fprintf(stderr,
                "error: test output directory is not empty: %s\n"
                "       remove it first with %s\n",
                output_dir, rm_command.cstr);
        str_free(rm_command);
        exit(1);
    }

    str_free(rm_command);
}

// Reject output roots that would let the runner delete the repository root,
// the whole build directory, or the filesystem root.
static void validate_output_dir(const char *output_dir) {
    String build_dir_abs = absolute_build_dir();
    bool unsafe = false;

    require(path_is_absolute(output_dir),
            "output directory must be an absolute path");

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

// Parse a positive job count.
static bool parse_positive_int(const char *text, int *value_out) {
    char *end = NULL;
    long value;

    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value <= 0 ||
        value > INT_MAX) {
        return false;
    }

    *value_out = (int)value;
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

// pselect() only wakes for signals that are actually caught, so install a
// no-op SIGCHLD handler purely to interrupt the sleep when the child changes
// state. The main loop still does the real reap via waitpid().
static void signal_state_sigchld_handler(int signum) { (void)signum; }

// Install the signal-handler and mask state needed for the parent event loop to
// sleep in pselect() without racing against child exits.
static void signal_state_init(TestSignalState *signal_state) {
    struct sigaction sigchld_action = {0};
    sigset_t sigchld_mask;

    // Catch SIGCHLD with a no-op handler so pselect() wakes when a child exits.
    sigemptyset(&sigchld_action.sa_mask);
    sigchld_action.sa_handler = signal_state_sigchld_handler;
    require(sigaction(SIGCHLD, &sigchld_action,
                      &signal_state->old_sigchld_action) == 0,
            "sigaction failed: %errno");

    // Block SIGCHLD in the parent outside pselect() so a child exit cannot
    // slip between waitpid(WNOHANG) and the next sleep. Without this, the
    // runner can observe "no exited children", lose the SIGCHLD in the gap
    // before pselect(), and then sleep until some unrelated fd activity or
    // timeout.
    sigemptyset(&sigchld_mask);
    sigaddset(&sigchld_mask, SIGCHLD);
    require(sigprocmask(SIG_BLOCK, &sigchld_mask,
                        &signal_state->old_sigchld_mask) == 0,
            "sigprocmask failed: %errno");

    // During pselect() we want the caller's original mask, except SIGCHLD must
    // be unblocked so child exits interrupt the sleep.
    signal_state->wait_mask = signal_state->old_sigchld_mask;
    sigdelset(&signal_state->wait_mask, SIGCHLD);
    signal_state->installed = true;
}

// Restore the caller's signal state after the wait loop finishes.
static void signal_state_deinit(TestSignalState *signal_state) {
    // IMGNEKO_UNCOVERED_OK[2 lines]: Defensive. We always install it first.
    if (!signal_state->installed)
        return;

    require(sigprocmask(SIG_SETMASK, &signal_state->old_sigchld_mask, NULL) ==
                0,
            "sigprocmask restore failed: %errno");
    require(sigaction(SIGCHLD, &signal_state->old_sigchld_action, NULL) == 0,
            "sigaction restore failed: %errno");
    signal_state->installed = false;
}

// Write the full byte range to fd, retrying short writes and EINTR. Fatal on
// failure because the runner cannot recover from losing captured output.
static void write_all_or_die(int fd, const char *data, size_t len,
                             const char *message) {
    while (len != 0) {
        ssize_t written = write(fd, data, len);

        // IMGNEKO_UNCOVERED_OK_START
        if (written < 0) {
            if (errno == EINTR)
                continue;
            die_errno(message);
        }
        // IMGNEKO_UNCOVERED_OK_END

        data += written;
        len -= (size_t)written;
    }
}

// Close a running test's output pipe once the runner has reached EOF or has
// given up draining it after a timeout.
static void close_running_test_output_pipe(RunningTest *running_test) {
    // IMGNEKO_UNCOVERED_OK[2 lines]: Defensive. We always close it here once.
    if (running_test->output_pipe_closed || running_test->output_pipe_fd < 0)
        return;

    close(running_test->output_pipe_fd);
    running_test->output_pipe_fd = -1;
    running_test->output_pipe_closed = true;
}

// Release the captured output file descriptor once the test no longer needs
// additional output appended to it.
static void close_running_test_output_file(RunningTest *running_test) {
    // IMGNEKO_UNCOVERED_OK[2 lines]: Defensive. We always close it here once.
    if (running_test->output_fd < 0)
        return;

    close(running_test->output_fd);
    running_test->output_fd = -1;
}

// Read one chunk from the merged child-output pipe, append it to the captured
// output file, and optionally mirror it to the user's terminal. Returns
// whether a non-empty chunk was consumed.
static bool pump_running_test_output(RunningTest *running_test,
                                     bool output_passthrough) {
    char buffer[4096];
    ssize_t count = read(running_test->output_pipe_fd, buffer, sizeof(buffer));

    // IMGNEKO_UNCOVERED_OK_START
    if (count < 0) {
        if (errno == EINTR)
            return false;
        die_errno("failed to read child output");
    }
    // IMGNEKO_UNCOVERED_OK_END

    if (count == 0) {
        close_running_test_output_pipe(running_test);
        return false;
    }

    write_all_or_die(running_test->output_fd, buffer, (size_t)count,
                     "failed to write captured output");
    if (output_passthrough) {
        write_all_or_die(STDOUT_FILENO, buffer, (size_t)count,
                         "failed to pass test output through");
    }
    return true;
}

// Release a running test's dynamically allocated paths and open file
// descriptors. The child process, if any, must already have been reaped.
static void running_test_deinit(RunningTest *running_test) {
    assert(running_test->pid <= 0 || running_test->child_reaped);
    close_running_test_output_pipe(running_test);
    close_running_test_output_file(running_test);
    str_free(running_test->test_output_dir);
    str_free(running_test->output_file_path);
}

// Locate the running-test entry for a pid, or NULL when it is no longer
// tracked.
static RunningTest *find_running_test_by_pid(RunningTestArray *running_tests,
                                             pid_t pid) {
    // IMGNEKO_UNCOVERED_OK: We always find the pid.
    for (size_t i = 0; i < running_tests->size; ++i) {
        if (running_tests->data[i].pid == pid)
            return &running_tests->data[i];
    }

    // This defensive fallback needs a reaped pid that is no longer tracked in
    // running_tests. The current wait loop only reaps tracked children before
    // finalization, so normal tests do not reach it.
    // IMGNEKO_UNCOVERED_OK
    return NULL;
}

// Fork and exec one test case, returning a fully initialized RunningTest that
// the wait loop owns and later releases with running_test_deinit(). The input
// path strings transfer ownership into the returned struct.
static RunningTest start_test_process(const TestCase *test_case,
                                      const TestRunConfig *config,
                                      const TestSignalState *signal_state,
                                      String test_output_dir,
                                      String output_file_path) {
    int output_fd = -1;
    int output_pipe_fds[2] = {-1, -1};
    pid_t pid;
    double start_time;
    char *argv[] = {
        test_case->kind == TEST_KIND_C ? test_case->c_exe_path.cstr
                                       : test_case->file_abs_path.cstr,
        test_case->kind == TEST_KIND_C
            ? (test_case->c_subtest.len != 0 ? test_case->c_subtest.cstr
                                             : "--all")
            : NULL,
        NULL,
    };

    require(mkdir_p(config->test_output_dir),
            "failed to create a directory: %errno");
    output_fd =
        open(config->output_file_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    require(output_fd >= 0, "failed to open a test output file: %errno");

    // Route both stdout and stderr through one pipe so the parent can always
    // capture the full merged output stream into output_file_path, and
    // optionally mirror the same bytes to the user's terminal in passthrough
    // mode.
    require(pipe(output_pipe_fds) == 0, "pipe failed: %errno");

    pid = fork();
    require(pid >= 0, "fork failed: %errno");

    // IMGNEKO_UNCOVERED_OK_START
    if (pid == 0) {
        // The parent blocks SIGCHLD around its wait loop. Undo that in the
        // child before running the test so the test process inherits the
        // caller's original signal mask, not the runner's internal one.
        if (sigprocmask(SIG_SETMASK, &signal_state->old_sigchld_mask, NULL) !=
            0) {
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
        if (setenv(test_output_dir_env, config->test_output_dir, 1) != 0) {
            fprintf(stderr, "error: failed to update test output env: %s\n",
                    strerror(errno));
            _exit(127);
        }
        if (chdir(config->test_output_dir) != 0) {
            fprintf(stderr, "error: failed to chdir to %s: %s\n",
                    config->test_output_dir, strerror(errno));
            _exit(127);
        }
        execvp(argv[0], argv);
        fprintf(stderr, "error: failed to exec %s: %s\n", argv[0],
                strerror(errno));
        _exit(127);
    }
    // IMGNEKO_UNCOVERED_OK_END

    close(output_pipe_fds[1]);

    // An artificial delay injected to trigger the situation where the parent is
    // too late to set the child process group before the child exits or sets it
    // itself.
    time_sleep_seconds(config->debug_parent_setpgid_delay_seconds);

    // Repeat setpgid in the parent to close the small race where timeout
    // cleanup might need to signal the process group before the child runs its
    // own setpgid call.
    if (setpgid(pid, pid) != 0) {
        // EACCES means the child already exec'd after its own setpgid(), so the
        // parent lost the race but the process-group setup step is done.
        // ESRCH means the child exited before the parent got here, so there is
        // no remaining process to move into a group. This is hard to trigger in
        // practice if we don't reap the process first.
        // IMGNEKO_UNCOVERED_OK[2 lines]
        if (errno != EACCES && errno != ESRCH)
            die_errno("setpgid failed: %errno");
    }

    start_time = time_monotonic_seconds();
    return (RunningTest){
        .test_case = test_case,
        .test_output_dir = test_output_dir,
        .output_file_path = output_file_path,
        .pid = pid,
        .output_fd = output_fd,
        .output_pipe_fd = output_pipe_fds[0],
        .start_time_seconds = start_time,
        .deadline_seconds = config->timeout_seconds > 0.0
                                ? start_time + config->timeout_seconds
                                : 0.0,
        .sigkill_deadline_seconds = 0.0,
        .output_drain_deadline_seconds = 0.0,
        .child_reaped = false,
        .output_pipe_closed = false,
        .timed_out = false,
        .sigkill_sent = false,
    };
}

// Reap any exited children without blocking and attach their statuses to the
// corresponding running-test records.
static void reap_running_tests(RunningTestArray *running_tests) {
    for (;;) {
        int status = 0;
        pid_t waited = waitpid(-1, &status, WNOHANG);

        if (waited == 0)
            return;

        if (waited > 0) {
            RunningTest *running_test =
                find_running_test_by_pid(running_tests, waited);
            require(running_test != NULL, "reaped untracked child");
            running_test->child_reaped = true;
            running_test->status = status;
            continue;
        }

        // IMGNEKO_UNCOVERED_OK[2 lines]: EINTR is hard to trigger here.
        if (errno == EINTR)
            continue;

        // ECHILD is possible and happens when there are currently no waitable
        // children left.
        require(errno == ECHILD, "waitpid failed: %errno");
        return;
    }
}

// Start timeout cleanup for a still-running child and bound the amount of time
// the runner will keep waiting on its output pipe afterward.
static void signal_running_test_process_group(const RunningTest *running_test,
                                              int signal_number) {
    // IMGNEKO_UNCOVERED_OK: ESRCH case is hard to trigger
    require(kill(-running_test->pid, signal_number) == 0 || errno == ESRCH,
            "kill failed: %errno");
}

// Escalate a timed-out test from SIGTERM to SIGKILL.
static void sigkill_running_test_process(RunningTest *running_test) {
    signal_running_test_process_group(running_test, SIGKILL);
    running_test->sigkill_sent = true;
}

// Mark a test as timed out, send SIGTERM to its process group, and configure
// the later SIGKILL/output-drain deadlines used by the event loop.
static void sigterm_running_test_process(RunningTest *running_test,
                                         double now_seconds) {
    // IMGNEKO_UNCOVERED_OK[2 lines]: Defensive. Callers check timed_out.
    if (running_test->timed_out)
        return;

    running_test->timed_out = true;
    running_test->sigkill_deadline_seconds =
        now_seconds + timeout_sigkill_grace_seconds;
    running_test->output_drain_deadline_seconds =
        now_seconds + timeout_output_drain_grace_seconds;

    signal_running_test_process_group(running_test, SIGTERM);
}

// Advance timeout cleanup for every running child: start timeout handling,
// escalate timed-out children from SIGTERM to SIGKILL, and eventually stop
// waiting for lingering output pipes.
static void update_running_test_timeouts(RunningTestArray *running_tests,
                                         double now_seconds) {
    for (size_t i = 0; i < running_tests->size; ++i) {
        RunningTest *running_test = &running_tests->data[i];

        // Start timeout cleanup once the test's main deadline has passed.
        if (!running_test->timed_out && running_test->deadline_seconds > 0.0 &&
            !running_test->child_reaped &&
            now_seconds >= running_test->deadline_seconds) {
            sigterm_running_test_process(running_test, now_seconds);
        }

        // If SIGTERM did not stop the test subtree quickly enough, force it
        // down with SIGKILL after the grace period expires.
        if (running_test->timed_out && !running_test->child_reaped &&
            !running_test->sigkill_sent &&
            now_seconds >= running_test->sigkill_deadline_seconds) {
            sigkill_running_test_process(running_test);
        }

        // Detached descendants can keep the inherited pipe open after the main
        // child is gone, so stop draining after a short post-timeout window.
        if (running_test->timed_out && !running_test->output_pipe_closed &&
            now_seconds >= running_test->output_drain_deadline_seconds) {
            close_running_test_output_pipe(running_test);
        }
    }
}

// Return whether the child has been fully accounted for and no longer needs to
// stay in the running set.
static bool running_test_is_complete(const RunningTest *running_test) {
    return running_test->child_reaped && running_test->output_pipe_closed;
}

// Compute the next deadline that should bound pselect(). Returns false when no
// timeout-related deadline is currently pending.
static bool next_running_test_deadline(const RunningTestArray *running_tests,
                                       double *deadline_out) {
    double deadline = DBL_MAX;

    for (size_t i = 0; i < running_tests->size; ++i) {
        const RunningTest *running_test = &running_tests->data[i];

        // The deadline from the main timeout.
        if (!running_test->timed_out && running_test->deadline_seconds > 0.0 &&
            !running_test->child_reaped &&
            running_test->deadline_seconds < deadline)
            deadline = running_test->deadline_seconds;

        // TODO: Cover the false branch here with a deterministic scenario
        // that leaves multiple timed-out children racing with staggered
        // SIGKILL deadlines. Current tests only hit the minimum case.
        // The deadline from the SIGKILL escalation after a timeout.
        if (running_test->timed_out && !running_test->child_reaped &&
            !running_test->sigkill_sent &&
            running_test->sigkill_deadline_seconds < deadline)
            deadline = running_test->sigkill_deadline_seconds;

        // TODO: Cover the ordering comparison here with multiple timed-out
        // children that keep their output pipes open long enough to create
        // different output-drain deadlines. Existing tests only hit the
        // first-deadline case.
        // The deadline for giving up on draining the output pipe.
        if (running_test->timed_out && !running_test->output_pipe_closed &&
            running_test->output_drain_deadline_seconds < deadline)
            deadline = running_test->output_drain_deadline_seconds;
    }

    if (deadline == DBL_MAX)
        return false;

    *deadline_out = deadline;
    return true;
}

// Sleep until one child emits output, exits, or reaches the next timeout
// transition, then process the newly available events.
static void wait_for_running_test_events(RunningTestArray *running_tests,
                                         bool output_passthrough,
                                         double output_chunk_delay_seconds,
                                         const TestSignalState *signal_state) {
    fd_set read_fds;
    struct timespec wait_timeout = {0};
    struct timespec *wait_timeout_ptr = NULL;
    double next_deadline = 0.0;
    double now_seconds = time_monotonic_seconds();
    int max_fd = -1;
    int rc;

    // Bring timeout and reap state up to date before deciding whether we need
    // to sleep at all.
    update_running_test_timeouts(running_tests, now_seconds);
    reap_running_tests(running_tests);

    // Build the fd set for output pipes that still need draining.
    FD_ZERO(&read_fds);
    for (size_t i = 0; i < running_tests->size; ++i) {
        RunningTest *running_test = &running_tests->data[i];

        if (running_test->output_pipe_closed)
            continue;

        require(running_test->output_pipe_fd < FD_SETSIZE,
                "too many open test output pipes; reduce -j");
        FD_SET(running_test->output_pipe_fd, &read_fds);
        // TODO: Cover the false branch by constructing a later running test
        // with a smaller fd than an earlier one. The runner currently opens
        // pipes monotonically, so max_fd only grows in practice.
        if (running_test->output_pipe_fd > max_fd)
            max_fd = running_test->output_pipe_fd;
    }

    // Bound the sleep by the earliest pending timeout transition, if any.
    if (next_running_test_deadline(running_tests, &next_deadline)) {
        wait_timeout = time_timeout_until_deadline(next_deadline, now_seconds);
        wait_timeout_ptr = &wait_timeout;
    }

    // Sleep until output arrives, a child state change interrupts pselect(),
    // or the next timeout deadline expires.
    rc = pselect(max_fd + 1, max_fd >= 0 ? &read_fds : NULL, NULL, NULL,
                 wait_timeout_ptr, &signal_state->wait_mask);

    if (rc < 0) {
        require(errno == EINTR, "pselect failed: %errno");
    }

    if (rc > 0) {
        // Drain whichever output pipes became readable during this wakeup.
        for (size_t i = 0; i < running_tests->size; ++i) {
            RunningTest *running_test = &running_tests->data[i];

            if (running_test->output_pipe_closed)
                continue;
            if (!FD_ISSET(running_test->output_pipe_fd, &read_fds))
                continue;

            if (pump_running_test_output(running_test, output_passthrough) &&
                output_chunk_delay_seconds > 0.0) {
                // Debug-only hook to slow output draining enough to exercise
                // the state where the child is already reaped but unread bytes
                // still remain buffered in the merged output pipe.
                time_sleep_seconds(output_chunk_delay_seconds);
            }
        }
    }

    // A timeout wakeup, a signal wakeup, or readable output can all change the
    // observed child/timeout state, so refresh once at the end regardless of
    // how pselect() returned.
    // NOTE: This is not strictly necessary because we do it at the beginning of
    // the function, but it helps finalizing children a bit earlier.
    reap_running_tests(running_tests);
    update_running_test_timeouts(running_tests, time_monotonic_seconds());
}

// Run argv, capture the child's stdout lines, and return an exit-like status.
// On success, this frees the previous contents of `*stdout_lines`.
static int run_argv_capture_stdout_lines(char *const *argv,
                                         StringArray *stdout_lines) {
    int pipe_fds[2];
    FILE *stream = NULL;
    pid_t pid;
    int status;

    require(pipe(pipe_fds) == 0, "pipe failed: %errno");

    pid = fork();
    require(pid >= 0, "fork failed: %errno");

    // IMGNEKO_UNCOVERED_OK_START
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
    // IMGNEKO_UNCOVERED_OK_END

    close(pipe_fds[1]);
    stream = fdopen(pipe_fds[0], "r");
    require(stream != NULL, "fdopen failed: %errno");

    // IMGNEKO_UNCOVERED_OK_START: Hard to make it fail
    if (!file_read_stream_lines(stdout_lines, stream, -1)) {
        int read_errno = errno;

        fclose(stream);
        require(waitpid(pid, &status, 0) >= 0, "waitpid failed: %errno");
        errno = read_errno;
        die_errno("failed to read child stdout");
    }
    // IMGNEKO_UNCOVERED_OK_END

    fclose(stream);
    require(waitpid(pid, &status, 0) >= 0, "waitpid failed: %errno");

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }

    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }

    // IMGNEKO_UNCOVERED_OK
    return 1;
}

// Print the failing output file path and the last max_lines lines so users do
// not need to open the full file just to see the failure context.
static void print_output_tail(const char *output_file_path, size_t max_lines) {
    StringArray lines = arr_empty;

    if (!file_read_lines(&lines, output_file_path, (ptrdiff_t)max_lines)) {
        fprintf(stderr, "error: failed to read captured output %s: %s\n\n",
                output_file_path, strerror(errno));
        return;
    }

    if (lines.size == 0) {
        fprintf(stderr, "output is empty: %s\n", output_file_path);
        str_array_free(&lines);
        return;
    }

    fprintf(stderr, "===== LAST %zu LINES OF TEST OUTPUT %s {{{ =====\n",
            max_lines, output_file_path);
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

// Check whether a test matches any already-flattened filter pattern. Empty
// filter arrays match everything.
static bool test_matches_filters(const TestCase *test_case,
                                 const StringArray *filters) {
    if (filters->size == 0)
        return true;

    for (size_t i = 0; i < filters->size; ++i)
        if (test_matches_pattern(test_case, filters->data[i].cstr))
            return true;

    return false;
}

// Expand `|` alternation in filter patterns into individual match patterns.
// Return false after reporting a CLI error if any alternation part is empty.
static bool flatten_filters(const StringArray *patterns, StringArray *out) {
    for (size_t i = 0; i < patterns->size; ++i) {
        const char *pattern = patterns->data[i].cstr;
        const char *cursor = pattern;

        do {
            const char *bar = strchr(cursor, '|');
            size_t len = bar != NULL ? (size_t)(bar - cursor) : strlen(cursor);

            if (len == 0) {
                fprintf(stderr, "error: invalid filter pattern: %s\n", pattern);
                return false;
            }

            arr_push(*out, str_from_data(cursor, len));
            if (bar == NULL)
                break;

            cursor = bar + 1;
        } while (true);
    }

    return true;
}

// Build the expected on-disk path for a compiled C test binary.
// The caller owns the returned string and must free it with str_free.
static String c_test_output_path(const char *test_bin_dir,
                                 const char *rel_path) {
    String final_path = path_join(test_bin_dir, rel_path);
    str_append_cstr(final_path, ".bin");
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
                            const char *test_bin_dir, String *exe_path_out) {
    StringArray stdout_lines = arr_empty;
    String exe_path = c_test_output_path(test_bin_dir, file->rel_path.cstr);
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

            // IMGNEKO_UNCOVERED_OK_START: This looks impossible with the
            // current implementation of strip_trailing_test_marker.
            if (line[0] == '\0') {
                fprintf(stderr, "error: invalid empty subtest name in %s\n",
                        file->rel_path.cstr);
                str_array_free(&stdout_lines);
                str_free(exe_path);
                c_subtest_array_free(subtests);
                exit(1);
            }
            // IMGNEKO_UNCOVERED_OK_END

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
                                const char *test_bin_dir,
                                TestCaseArray *cases) {
    size_t i;

    for (i = 0; i < files->size; ++i) {
        const TestFile *file = &files->data[i];

        if (file->kind == TEST_KIND_C) {
            CSubtestArray subtests = arr_empty;
            String exe_path = str_empty;
            size_t j;

            // A C test binary can expose multiple subtests via --list.
            c_test_subtests(file, &subtests, test_bin_dir, &exe_path);
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

    // IMGNEKO_UNCOVERED_OK[2 lines]
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

// Translate a completed running-test record into the final exit status and
// elapsed time reported to the summary logic.
static TestRunResult
test_run_result_from_running_test(const RunningTest *running_test) {
    TestRunResult result = {
        .exit_code = 1,
        .timed_out = running_test->timed_out,
        .elapsed_seconds =
            time_monotonic_seconds() - running_test->start_time_seconds,
    };

    if (running_test->timed_out) {
        // Use 124 as the synthetic timeout status. This matches the common
        // shell convention used by tools like `timeout`.
        result.exit_code = 124;
        return result;
    }

    if (WIFEXITED(running_test->status)) {
        result.exit_code = WEXITSTATUS(running_test->status);
        return result;
    }

    if (WIFSIGNALED(running_test->status)) {
        result.exit_code = 128 + WTERMSIG(running_test->status);
        return result;
    }

    // IMGNEKO_UNCOVERED_OK
    return result;
}

// Classify the finished test into the user-visible outcome categories.
static ClassifiedTestResult classify_test_result(const TestCase *test_case,
                                                 TestRunResult run_result,
                                                 double flip_exit_probability) {
    ClassifiedTestResult classified = {.run_result = run_result};

    if (!run_result.timed_out) {
        run_result.exit_code = maybe_flip_exit_code(
            test_case, run_result.exit_code, flip_exit_probability);
    }
    classified.run_result = run_result;

    if (run_result.timed_out) {
        classified.outcome = TEST_OUTCOME_TIMEOUT;
    } else if (run_result.exit_code == 0 &&
               test_case->marker == TEST_MARKER_XFAIL) {
        classified.outcome = TEST_OUTCOME_XPASS;
    } else if (run_result.exit_code == 0) {
        classified.outcome = TEST_OUTCOME_PASS;
    } else if (test_case->marker == TEST_MARKER_XFAIL) {
        classified.outcome = TEST_OUTCOME_XFAIL;
    } else {
        classified.outcome = TEST_OUTCOME_FAIL;
    }

    return classified;
}

// Update the aggregate summary counters and per-outcome test lists using a
// pre-classified result.
static void
update_summary_for_classified_result(RunSummary *summary,
                                     const TestCase *test_case,
                                     const ClassifiedTestResult *classified) {
    // IMGNEKO_UNCOVERED_OK
    switch (classified->outcome) {
    case TEST_OUTCOME_PASS:
        summary->passed++;
        return;
    case TEST_OUTCOME_XFAIL:
        summary->xfailed++;
        return;
    case TEST_OUTCOME_DISABLED:
        summary->disabled++;
        return;
    case TEST_OUTCOME_XPASS:
        summary->xpassed++;
        string_array_push_copy(&summary->xpassed_tests, test_case->id.cstr);
        return;
    case TEST_OUTCOME_TIMEOUT:
        summary->timed_out++;
        string_array_push_copy(&summary->timed_out_tests, test_case->id.cstr);
        return;
    case TEST_OUTCOME_FAIL:
        summary->failed++;
        string_array_push_copy(&summary->failed_tests, test_case->id.cstr);
        return;
    }
}

// Print the final status line for one classified test result and, for failures
// and timeouts, include the captured output tail unless passthrough already
// showed the full stream live.
static void print_classified_test_result(const TestCase *test_case,
                                         const ClassifiedTestResult *classified,
                                         const char *output_file_path,
                                         bool output_passthrough) {
    // IMGNEKO_UNCOVERED_OK
    switch (classified->outcome) {
    case TEST_OUTCOME_PASS:
        printf("PASS: %s\n", test_case->id.cstr);
        break;
    case TEST_OUTCOME_XFAIL:
        printf("XFAIL: %s\n", test_case->id.cstr);
        break;
    case TEST_OUTCOME_DISABLED:
        printf("DISABLED: %s\n", test_case->id.cstr);
        break;
    case TEST_OUTCOME_XPASS:
        printf("XPASS: %s\n", test_case->id.cstr);
        break;
    case TEST_OUTCOME_TIMEOUT:
        printf("\nTIMEOUT: %s\n", test_case->id.cstr);
        if (!output_passthrough)
            print_output_tail(output_file_path, 20);
        break;
    case TEST_OUTCOME_FAIL:
        printf("\nFAIL: %s\n", test_case->id.cstr);
        if (!output_passthrough)
            print_output_tail(output_file_path, 20);
        break;
    }

    fflush(stdout);
}

// Update the run summary and user-facing status output for one completed test.
static void record_test_result(RunSummary *summary, const TestCase *test_case,
                               TestRunResult run_result,
                               const char *output_file_path,
                               bool output_passthrough,
                               double flip_exit_probability) {
    ClassifiedTestResult classified =
        classify_test_result(test_case, run_result, flip_exit_probability);

    update_summary_for_classified_result(summary, test_case, &classified);
    print_classified_test_result(test_case, &classified, output_file_path,
                                 output_passthrough);
}

// Release one completed running test and remove it from the dense running-test
// array while preserving the order of the remaining entries.
static void remove_running_test_at(RunningTestArray *running_tests,
                                   size_t index) {
    running_test_deinit(&running_tests->data[index]);
    arr_remove_at(*running_tests, index);
}

// Flush all running tests that have reached a terminal state into the summary
// and remove them from the running set.
static void finalize_completed_running_tests(RunningTestArray *running_tests,
                                             RunSummary *summary,
                                             bool output_passthrough,
                                             double flip_exit_probability) {
    for (size_t i = 0; i < running_tests->size;) {
        RunningTest *running_test = &running_tests->data[i];

        if (!running_test_is_complete(running_test)) {
            i++;
            continue;
        }

        record_test_result(summary, running_test->test_case,
                           test_run_result_from_running_test(running_test),
                           running_test->output_file_path.cstr,
                           output_passthrough, flip_exit_probability);
        remove_running_test_at(running_tests, i);
    }
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

    // IMGNEKO_UNCOVERED_OK_START: setenv/unsetenv failures are hard to inject
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
    // IMGNEKO_UNCOVERED_OK_END

    str_free(build_dir_abs);
    str_free(bin_dir);
    str_free(new_path);
}

// Print CLI usage help.
static void usage(FILE *stream) {
    String default_output_dir = default_test_output_dir();
    String default_test_bin_dir_path = default_test_bin_dir();
    String tests_dir = default_tests_dir();

    fprintf(stream,
            "Usage: %s [--list] [--all] [-j JOBS] [--output-dir DIR]\n"
            "       [--filter PATTERN] [--tests-dir DIR] [--test-bin-dir DIR]\n"
            "       [--timeout SECONDS] [-p|--output-passthrough]\n"
            "       [--debug-flip-exit-probability P]\n"
            "       [--debug-parent-setpgid-delay SECONDS]\n"
            "       [--debug-parent-output-chunk-delay SECONDS]\n"
            "       [PATTERN ...]\n"
            "\n"
            "Use -p/--output-passthrough to mirror test stdout/stderr live.\n"
            "Use -j/--jobs to run multiple tests concurrently.\n"
            "Patterns use shell-style wildcards and may be joined with '|'.\n"
            "Default tests dir: %s\n"
            "Default C test bin dir: %s\n"
            "Default output dir: %s\n"
            "Default jobs: %d\n"
            "Default timeout: %.0f seconds\n",
            "test-runner", tests_dir.cstr, default_test_bin_dir_path.cstr,
            default_output_dir.cstr, TEST_RUNNER_DEFAULT_JOBS,
            default_test_timeout_seconds);

    str_free(tests_dir);
    str_free(default_test_bin_dir_path);
    str_free(default_output_dir);
}

static void cli_options_init(CliOptions *options) {
    *options = (CliOptions){
        .raw_filters = arr_empty,
        .filters = arr_empty,
        .jobs = TEST_RUNNER_DEFAULT_JOBS,
        .tests_dir = str_empty,
        .output_dir = str_empty,
        .test_bin_dir = str_empty,
        .timeout_seconds = default_test_timeout_seconds,
    };
}

static void cli_options_deinit(CliOptions *options) {
    str_free(options->test_bin_dir);
    str_free(options->output_dir);
    str_free(options->tests_dir);
    str_array_free(&options->filters);
    str_array_free(&options->raw_filters);
}

static void test_runner_state_init(TestRunnerState *state) {
    *state = (TestRunnerState){
        .files = arr_empty,
        .cases = arr_empty,
        .selected_cases = arr_empty,
        .running_tests = arr_empty,
        .summary =
            {
                .failed_tests = arr_empty,
                .xpassed_tests = arr_empty,
                .timed_out_tests = arr_empty,
            },
        .run_start_seconds = time_monotonic_seconds(),
    };
}

static void test_runner_state_deinit(TestRunnerState *state) {
    // By the time teardown runs, every started child must already have been
    // finalized and removed from running_tests.
    assert(state->running_tests.size == 0);
    arr_free(state->running_tests);
    arr_free(state->selected_cases);
    str_array_free(&state->summary.timed_out_tests);
    str_array_free(&state->summary.xpassed_tests);
    str_array_free(&state->summary.failed_tests);
    test_case_array_free(&state->cases);
    test_file_array_free(&state->files);
}

// Parse argv into CliOptions. Returns false when the caller should exit
// immediately, with exit_code_out already set and any diagnostics printed.
static bool parse_cli_args(int argc, char **argv, CliOptions *options,
                           int *exit_code_out) {
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--list") == 0) {
            options->list_only = true;
            continue;
        }
        if (strcmp(argv[i], "--all") == 0) {
            options->run_all = true;
            continue;
        }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(stdout);
            *exit_code_out = 0;
            return false;
        }
        if (strcmp(argv[i], "-p") == 0 ||
            strcmp(argv[i], "--output-passthrough") == 0) {
            options->output_passthrough = true;
            continue;
        }
        if (strcmp(argv[i], "-j") == 0 || strcmp(argv[i], "--jobs") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --jobs requires a value\n");
                usage(stderr);
                *exit_code_out = 2;
                return false;
            }
            if (!parse_positive_int(argv[++i], &options->jobs)) {
                fprintf(stderr, "error: invalid jobs value: %s\n", argv[i]);
                usage(stderr);
                *exit_code_out = 2;
                return false;
            }
            continue;
        }
        if (strncmp(argv[i], "--jobs=", 7) == 0) {
            if (!parse_positive_int(argv[i] + 7, &options->jobs)) {
                fprintf(stderr, "error: invalid jobs value: %s\n", argv[i] + 7);
                usage(stderr);
                *exit_code_out = 2;
                return false;
            }
            continue;
        }
        // IMGNEKO_UNCOVERED_OK: The argv[i][2] == '\0' is handled earlier.
        if (strncmp(argv[i], "-j", 2) == 0 && argv[i][2] != '\0') {
            if (!parse_positive_int(argv[i] + 2, &options->jobs)) {
                fprintf(stderr, "error: invalid jobs value: %s\n", argv[i] + 2);
                usage(stderr);
                *exit_code_out = 2;
                return false;
            }
            continue;
        }
        if (strcmp(argv[i], "--output-dir") == 0) {
            if (i + 1 >= argc) {
                usage(stderr);
                *exit_code_out = 2;
                return false;
            }
            require(path_resolve_absolute(&options->output_dir, argv[++i]),
                    "failed to resolve output directory: %errno");
            continue;
        }
        if (strncmp(argv[i], "--output-dir=", 13) == 0) {
            require(path_resolve_absolute(&options->output_dir, argv[i] + 13),
                    "failed to resolve output directory: %errno");
            continue;
        }
        if (strcmp(argv[i], "--filter") == 0) {
            if (i + 1 >= argc) {
                usage(stderr);
                *exit_code_out = 2;
                return false;
            }
            string_array_push_copy(&options->raw_filters, argv[++i]);
            continue;
        }
        if (strncmp(argv[i], "--filter=", 9) == 0) {
            string_array_push_copy(&options->raw_filters, argv[i] + 9);
            continue;
        }
        if (strcmp(argv[i], "--tests-dir") == 0) {
            if (i + 1 >= argc) {
                usage(stderr);
                *exit_code_out = 2;
                return false;
            }
            require(path_resolve_absolute(&options->tests_dir, argv[++i]),
                    "failed to resolve tests directory: %errno");
            continue;
        }
        if (strncmp(argv[i], "--tests-dir=", 12) == 0) {
            require(path_resolve_absolute(&options->tests_dir, argv[i] + 12),
                    "failed to resolve tests directory: %errno");
            continue;
        }
        if (strcmp(argv[i], "--test-bin-dir") == 0) {
            if (i + 1 >= argc) {
                usage(stderr);
                *exit_code_out = 2;
                return false;
            }
            require(path_resolve_absolute(&options->test_bin_dir, argv[++i]),
                    "failed to resolve test-bin directory: %errno");
            continue;
        }
        if (strncmp(argv[i], "--test-bin-dir=", 15) == 0) {
            require(path_resolve_absolute(&options->test_bin_dir, argv[i] + 15),
                    "failed to resolve test-bin directory: %errno");
            continue;
        }
        if (strcmp(argv[i], "--timeout") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --timeout requires a value\n");
                usage(stderr);
                *exit_code_out = 2;
                return false;
            }
            if (!parse_timeout_seconds(argv[++i], &options->timeout_seconds)) {
                fprintf(stderr, "error: invalid --timeout value: %s\n",
                        argv[i]);
                usage(stderr);
                *exit_code_out = 2;
                return false;
            }
            continue;
        }
        if (strncmp(argv[i], "--timeout=", 10) == 0) {
            if (!parse_timeout_seconds(argv[i] + 10,
                                       &options->timeout_seconds)) {
                fprintf(stderr, "error: invalid --timeout value: %s\n",
                        argv[i] + 10);
                usage(stderr);
                *exit_code_out = 2;
                return false;
            }
            continue;
        }
        if (strcmp(argv[i], "--debug-flip-exit-probability") == 0) {
            if (i + 1 >= argc) {
                fprintf(
                    stderr,
                    "error: --debug-flip-exit-probability requires a value\n");
                *exit_code_out = 2;
                return false;
            }
            if (!parse_probability(argv[++i],
                                   &options->debug_flip_exit_probability)) {
                fprintf(stderr,
                        "error: invalid --debug-flip-exit-probability value: "
                        "%s\n",
                        argv[i]);
                usage(stderr);
                *exit_code_out = 2;
                return false;
            }
            continue;
        }
        if (strncmp(argv[i], "--debug-flip-exit-probability=", 30) == 0) {
            if (!parse_probability(argv[i] + 30,
                                   &options->debug_flip_exit_probability)) {
                fprintf(stderr,
                        "error: invalid --debug-flip-exit-probability value: "
                        "%s\n",
                        argv[i] + 30);
                usage(stderr);
                *exit_code_out = 2;
                return false;
            }
            continue;
        }
        if (strcmp(argv[i], "--debug-parent-setpgid-delay") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr,
                        "error: --debug-parent-setpgid-delay requires a "
                        "value\n");
                *exit_code_out = 2;
                return false;
            }
            if (!parse_timeout_seconds(
                    argv[++i], &options->debug_parent_setpgid_delay_seconds)) {
                fprintf(stderr,
                        "error: invalid --debug-parent-setpgid-delay value: "
                        "%s\n",
                        argv[i]);
                usage(stderr);
                *exit_code_out = 2;
                return false;
            }
            continue;
        }
        if (strncmp(argv[i], "--debug-parent-setpgid-delay=", 29) == 0) {
            // IMGNEKO_UNCOVERED_OK_START
            if (!parse_timeout_seconds(
                    argv[i] + 29,
                    &options->debug_parent_setpgid_delay_seconds)) {
                fprintf(stderr,
                        "error: invalid --debug-parent-setpgid-delay value: "
                        "%s\n",
                        argv[i] + 29);
                usage(stderr);
                *exit_code_out = 2;
                return false;
            }
            continue;
            // IMGNEKO_UNCOVERED_OK_END
        }
        if (strcmp(argv[i], "--debug-parent-output-chunk-delay") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr,
                        "error: --debug-parent-output-chunk-delay requires "
                        "a value\n");
                *exit_code_out = 2;
                return false;
            }
            if (!parse_timeout_seconds(
                    argv[++i],
                    &options->debug_parent_output_chunk_delay_seconds)) {
                fprintf(stderr,
                        "error: invalid --debug-parent-output-chunk-delay "
                        "value: %s\n",
                        argv[i]);
                usage(stderr);
                *exit_code_out = 2;
                return false;
            }
            continue;
        }
        if (strncmp(argv[i], "--debug-parent-output-chunk-delay=", 34) == 0) {
            if (!parse_timeout_seconds(
                    argv[i] + 34,
                    &options->debug_parent_output_chunk_delay_seconds)) {
                fprintf(stderr,
                        "error: invalid --debug-parent-output-chunk-delay "
                        "value: %s\n",
                        argv[i] + 34);
                usage(stderr);
                *exit_code_out = 2;
                return false;
            }
            continue;
        }
        if (argv[i][0] == '-') {
            fprintf(stderr, "error: unknown option: %s\n", argv[i]);
            usage(stderr);
            *exit_code_out = 2;
            return false;
        }
        string_array_push_copy(&options->raw_filters, argv[i]);
    }

    if (options->run_all && options->raw_filters.size != 0) {
        fprintf(stderr, "error: --all cannot be combined with --filter or "
                        "positional patterns\n");
        usage(stderr);
        *exit_code_out = 2;
        return false;
    }

    return true;
}

// Fill in defaults and validate the parsed command-line options.
static bool finalize_cli_options(CliOptions *options, int *exit_code_out) {
    if (!flatten_filters(&options->raw_filters, &options->filters)) {
        *exit_code_out = 2;
        return false;
    }

    if (options->output_dir.len == 0)
        options->output_dir = default_test_output_dir();
    if (options->tests_dir.len == 0)
        options->tests_dir = default_tests_dir();
    if (options->test_bin_dir.len == 0)
        options->test_bin_dir = default_test_bin_dir();
    validate_output_dir(options->output_dir.cstr);
    if (!options->list_only)
        require_empty_output_dir(options->output_dir.cstr);
    if (options->debug_flip_exit_probability > 0.0)
        seed_debug_random();
    return true;
}

// Export the nested-test environment and switch to the repository root so test
// discovery and spawned commands see consistent relative paths.
static void prepare_test_run_environment(void) {
    prepare_env_vars();
    require(chdir(root_dir) == 0, "failed to chdir to repository root: %errno");
}

// Discover the full runnable test set after option parsing has established the
// tests directory and C binary directory roots.
static bool discover_tests_for_run(const CliOptions *options,
                                   TestRunnerState *state, int *exit_code_out) {
    discover_test_files(options->tests_dir.cstr, &state->files);
    if (state->files.size == 0) {
        fprintf(stderr, "error: no tests found under %s\n",
                options->tests_dir.cstr);
        *exit_code_out = 2;
        return false;
    }

    discover_test_cases(&state->files, options->test_bin_dir.cstr,
                        &state->cases);
    // IMGNEKO_UNCOVERED_OK_START
    if (state->cases.size == 0) {
        fprintf(stderr, "error: no runnable tests found under %s\n",
                options->tests_dir.cstr);
        *exit_code_out = 2;
        return false;
    }
    // IMGNEKO_UNCOVERED_OK_END
    return true;
}

// Append every discovered test case that matches the active filters to
// selected_cases, preserving discovery order.
static void collect_selected_test_cases(const TestCaseArray *cases,
                                        const StringArray *filters,
                                        TestCasePtrArray *selected_cases) {
    for (size_t i = 0; i < cases->size; ++i) {
        const TestCase *test_case = &cases->data[i];

        if (!test_matches_filters(test_case, filters))
            continue;

        arr_push(*selected_cases, test_case);
    }
}

// Print the already-selected tests in discovery order for `--list`.
static void list_selected_tests(const TestRunnerState *state) {
    for (size_t i = 0; i < state->selected_cases.size; ++i)
        print_listed_test(state->selected_cases.data[i]);
}

// Record a disabled test without spawning a child process.
static void record_disabled_test(RunSummary *summary,
                                 const TestCase *test_case) {
    ClassifiedTestResult classified = {.outcome = TEST_OUTCOME_DISABLED};

    update_summary_for_classified_result(summary, test_case, &classified);
    print_classified_test_result(test_case, &classified, NULL, false);
}

// Start one selected test or, for disabled entries, record the synthetic result
// immediately without consuming a job slot.
static void start_selected_test(const CliOptions *options,
                                TestRunnerState *state,
                                const TestCase *test_case) {
    String test_output_dir;
    String output_file_path;
    TestRunConfig config;
    RunningTest running_test;

    if (test_case->marker == TEST_MARKER_DISABLED) {
        record_disabled_test(&state->summary, test_case);
        return;
    }

    printf("RUN: %s\n", test_case->id.cstr);
    fflush(stdout);

    test_output_dir = test_output_dir_path(test_case, options->output_dir.cstr);
    output_file_path = test_output_file_path(test_output_dir.cstr);
    config = (TestRunConfig){
        .test_output_dir = test_output_dir.cstr,
        .output_file_path = output_file_path.cstr,
        .timeout_seconds = options->timeout_seconds,
        .debug_parent_setpgid_delay_seconds =
            options->debug_parent_setpgid_delay_seconds,
    };
    running_test = start_test_process(test_case, &config, &state->signal_state,
                                      test_output_dir, output_file_path);
    arr_push(state->running_tests, running_test);
}

// Start more selected tests until all job slots are full or
// *next_selected_index reaches the end of state->selected_cases. The function
// advances *next_selected_index for every test it consumes from the selected
// list.
static void start_ready_tests(const CliOptions *options, TestRunnerState *state,
                              size_t *next_selected_index) {
    while (state->running_tests.size < (size_t)options->jobs &&
           *next_selected_index < state->selected_cases.size) {
        const TestCase *test_case =
            state->selected_cases.data[(*next_selected_index)++];

        start_selected_test(options, state, test_case);
    }
}

// Run the selected tests under the single-threaded event loop until every test
// has either been recorded immediately or started, reaped, and finalized.
static void run_selected_tests(const CliOptions *options,
                               TestRunnerState *state) {
    size_t next_selected_index = 0;

    signal_state_init(&state->signal_state);
    while (next_selected_index < state->selected_cases.size ||
           state->running_tests.size != 0) {
        // First launch as many new tests as we can. Disabled tests are
        // accounted for synchronously here and do not enter running_tests.
        start_ready_tests(options, state, &next_selected_index);

        // Then wait until some running test produces output, exits, or reaches
        // its next timeout transition. The wait helper may return immediately
        // if a child already completed before we went to sleep.
        wait_for_running_test_events(
            &state->running_tests, options->output_passthrough,
            options->debug_parent_output_chunk_delay_seconds,
            &state->signal_state);

        // Finally, flush every completed child into the summary and free its
        // slot so the next loop iteration can start more work.
        finalize_completed_running_tests(&state->running_tests, &state->summary,
                                         options->output_passthrough,
                                         options->debug_flip_exit_probability);
    }
    signal_state_deinit(&state->signal_state);
}

// Finish the run by checking whether any test matched and printing the final
// summary for execution mode.
static void finalize_run_result(const CliOptions *options,
                                TestRunnerState *state, int *exit_code_out) {
    if (state->discovered == 0) {
        if (!options->list_only) {
            fprintf(stderr, "error: no tests matched the requested filters\n");
            *exit_code_out = 2;
        }
        return;
    }

    if (options->list_only)
        return;

    print_named_test_list("timed out tests", &state->summary.timed_out_tests);
    print_named_test_list("failed tests", &state->summary.failed_tests);
    print_named_test_list("xpassed tests", &state->summary.xpassed_tests);
    printf("\nSummary:\n");
    print_summary_count("discovered", state->discovered);
    print_summary_count("passed", state->summary.passed);
    print_summary_count("xfailed", state->summary.xfailed);
    print_summary_count("disabled", state->summary.disabled);
    print_summary_count("xpassed", state->summary.xpassed);
    print_summary_count("timeout", state->summary.timed_out);
    print_summary_count("failed", state->summary.failed);
    if (state->summary.failed != 0 || state->summary.xpassed != 0 ||
        state->summary.timed_out != 0) {
        *exit_code_out = 1;
    }
    printf("\n");
    printf("Time: %.3f s\n",
           time_monotonic_seconds() - state->run_start_seconds);
    printf("Result: %s\n", *exit_code_out == 0 ? "SUCCESS" : "FAILURE");
}

int main(int argc, char **argv) {
    CliOptions options;
    TestRunnerState state;
    int exit_code = 0;

    cli_options_init(&options);
    test_runner_state_init(&state);

    if (!parse_cli_args(argc, argv, &options, &exit_code))
        goto cleanup;
    if (!finalize_cli_options(&options, &exit_code))
        goto cleanup;

    prepare_test_run_environment();
    if (!discover_tests_for_run(&options, &state, &exit_code))
        goto cleanup;
    collect_selected_test_cases(&state.cases, &options.filters,
                                &state.selected_cases);
    state.discovered = state.selected_cases.size;

    if (options.list_only)
        list_selected_tests(&state);
    else
        run_selected_tests(&options, &state);

    finalize_run_result(&options, &state, &exit_code);

cleanup:
    test_runner_state_deinit(&state);
    cli_options_deinit(&options);
    return exit_code;
}
