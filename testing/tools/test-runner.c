// SPDX-License-Identifier: MIT-0

// Enable POSIX APIs used in this file (getline, strdup/strndup, setenv, etc.).
#define _POSIX_C_SOURCE 200809L
// macOS hides mkdtemp() when strict POSIX feature macros are active unless the
// Darwin extensions are requested explicitly.
#define _DARWIN_C_SOURCE

#include <assert.h>
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
#include "util/io.h"
#include "util/options.h"
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

// A discovered runnable file under `testing/tests/`.
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

// A subtest reported by a compiled C test binary.
typedef struct CSubtest {
    String name;
    TestMarker marker;
} CSubtest;

// A runnable test case expanded from a discovered file.
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

// A flattened command-line test selection pattern.
//
// `text` is the shell-style pattern supplied by the user, after splitting `|`
// alternation into separate atoms. `abs_path_text` is the same pattern resolved
// as an absolute path pattern against the invocation cwd, for the fallback path
// match after name matching misses.
typedef struct TestPattern {
    String text;
    String abs_path_text;
    bool matched;
} TestPattern;

DEFINE_ARRAY_TYPE(TestPatternArray, TestPattern)

// Result of running a test case in a child process.
typedef struct TestRunResult {
    int exit_code;
    bool interrupted;
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
    TEST_OUTCOME_INTERRUPTED,
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

// Parsed command-line options plus their derived selection patterns.
typedef struct CliOptions {
    TestPatternArray patterns;
    bool list_only;
    bool run_all;
    bool output_passthrough;
    bool out_tmp;
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
// The parent blocks SIGCHLD, SIGINT, and SIGTERM between wait-loop iterations
// and then passes wait_mask to pselect() so those signals are temporarily
// unblocked during the sleep. That avoids missing child exits or shutdown
// requests in the small window between updating state and going to sleep, while
// still restoring the caller's original signal state at exit.
typedef struct TestSignalState {
    // SIGCHLD action before the runner installed its wakeup handler.
    struct sigaction old_sigchld_action;
    // SIGINT action before the runner installed its shutdown handler.
    struct sigaction old_sigint_action;
    // SIGTERM action before the runner installed its shutdown handler.
    struct sigaction old_sigterm_action;
    // Signal mask before the runner blocked its internal wakeup/shutdown
    // signals in the parent.
    sigset_t old_signal_mask;
    // Mask used during pselect(): same as old_signal_mask, but with SIGCHLD,
    // SIGINT, and SIGTERM unblocked so those events interrupt the sleep
    // immediately.
    sigset_t wait_mask;
    // Whether signal_state_init() ran successfully and cleanup must restore
    // signal state.
    bool installed;
} TestSignalState;

// A currently running/supervised test child plus its captured-output state.
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
    // Deadline for giving up on additional pipe output after the child exits
    // or after a timeout starts cleanup.
    double output_drain_deadline_seconds;

    bool child_reaped;
    bool output_pipe_closed;
    bool timed_out;
    bool sigkill_sent;

    // The runner interrupted this test after receiving SIGINT/SIGTERM, so the
    // final status should be reported as interrupted rather than timeout/fail.
    bool interrupted;
} RunningTest;

// Summary counters accumulated across a whole test run.
typedef struct RunSummary {
    size_t passed;
    size_t xfailed;
    size_t disabled;
    size_t xpassed;
    size_t timed_out;
    size_t failed;
    size_t interrupted;
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

// Mutable process-wide state for a test-runner invocation.
typedef struct TestRunnerState {
    TestFileArray files;
    TestCaseArray cases;
    TestCasePtrArray selected_cases;
    RunningTestArray running_tests;
    RunSummary summary;
    String timing_file_path;
    FILE *timing_file;
    size_t discovered;
    size_t num_recorded_results;
    size_t next_selected_index;
    double run_start_seconds;
    TestSignalState signal_state;
    int shutdown_signal_number;
    bool shutdown_requested;
} TestRunnerState;

// Absolute path to the repository root (/path/to/imgneko).
static const char *const root_dir = TEST_RUNNER_ROOT_DIR;
// Absolute path to the selected build directory (for example:
// /path/to/imgneko/build/default).
static const char *const build_dir = TEST_RUNNER_BUILD_DIR;

// Root-level file inside the output directory that records per-test timings.
static const char *const timing_file_name = "test-times.txt";
// After a timeout sends SIGTERM, wait briefly before escalating to SIGKILL.
static const double timeout_sigkill_grace_seconds = 0.5;
// Keep draining buffered output only briefly after the supervised child is gone
// so a detached descendant that inherited the pipe cannot hang the runner
// forever.
static const double output_drain_grace_seconds = 0.25;
// Bound otherwise-indefinite sleeps so platforms that miss a SIGCHLD wakeup
// still make progress by polling child state periodically. This prevents macOS
// runs from hanging when the child state changes without another readable pipe
// or pending timeout transition to wake pselect().
static const double event_loop_idle_poll_seconds = 0.1;

// Per-test environment variable that points tests at their own output
// directory.
static const char *const test_output_dir_env = "IMGNEKO_TEST_OUTPUT_DIR";

// The first SIGINT/SIGTERM received while test children are running.
static volatile sig_atomic_t pending_shutdown_signal = 0;

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

static void test_pattern_array_free(TestPatternArray *array) {
    for (size_t i = 0; i < array->size; ++i) {
        str_free(array->data[i].text);
        str_free(array->data[i].abs_path_text);
    }
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

// Return the lowercase outcome name written to the machine-readable timing
// file.
static const char *test_outcome_record_name(TestOutcomeKind outcome) {
    // IMGNEKO_UNCOVERED_OK
    switch (outcome) {
    case TEST_OUTCOME_PASS:
        return "pass";
    case TEST_OUTCOME_XFAIL:
        return "xfail";
    case TEST_OUTCOME_DISABLED:
        return "disabled";
    case TEST_OUTCOME_XPASS:
        return "xpass";
    case TEST_OUTCOME_TIMEOUT:
        return "timeout";
    case TEST_OUTCOME_FAIL:
        return "fail";
    case TEST_OUTCOME_INTERRUPTED:
        return "interrupted";
    }

    // IMGNEKO_UNCOVERED_OK
    return "unknown";
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
        cstr_sanitize_for_diagnostic(line_text, 255, line);
        fprintf(stderr, "error: ambiguous test markers in line: %s\n",
                line_text);
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
            cstr_sanitize_for_diagnostic(path_text, 255, path);
            fprintf(stderr, "error: multiple test markers found in %s\n",
                    path_text);
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

    // IMGNEKO_UNCOVERED_OK: path[prefix_len] != '/' hard to trigger cleanly.
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
        if (cstr_ends_with_cstr(rel_path.cstr, ".c")) {
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

// Require the selected output root to be absent or empty so a new run never
// mixes fresh results with leftover files from an earlier invocation.
static void require_empty_output_dir(const char *output_dir) {
    struct stat st;
    DIR *dir;
    bool has_entries = false;
    int readdir_errno = 0;
    String rm_command = str_from_cstr("rm -rf ");

    if (stat(output_dir, &st) != 0) {
        require(errno == ENOENT, "failed to stat output directory: %errno");
        str_free(rm_command);
        return;
    }

    str_append_shell_quoted_word(&rm_command, output_dir);

    if (!S_ISDIR(st.st_mode)) {
        cstr_sanitize_for_diagnostic(output_dir_text, 255, output_dir);
        str_sanitize_for_diagnostic(rm_command_text, 255, rm_command);
        fprintf(stderr,
                "error: test output path exists and is not a directory: %s\n"
                "       remove it first with %s\n",
                output_dir_text, rm_command_text);
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
        cstr_sanitize_for_diagnostic(output_dir_text, 255, output_dir);
        str_sanitize_for_diagnostic(rm_command_text, 255, rm_command);
        fprintf(stderr,
                "error: test output directory is not empty: %s\n"
                "       remove it first with %s\n",
                output_dir_text, rm_command_text);
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
        cstr_sanitize_for_diagnostic(output_dir_text, 255, output_dir);
        fprintf(stderr, "error: unsafe output directory: %s\n",
                output_dir_text);
        exit(1);
    }

    str_free(build_dir_abs);
}

// CLI schema for test-runner. Positional patterns first match test names, then
// fall back to absolute test paths resolved against the invocation cwd.
#define TEST_RUNNER_CLI_OPTIONS(X, S)                                          \
    X(S, list_only, OptBool,                                                   \
      OPT_BOOL_FLAG(.cli = "--list",                                           \
                    .descr = "List matching tests without running them."))     \
    X(S, run_all, OptBool,                                                     \
      OPT_BOOL_FLAG(.cli = "--all",                                            \
                    .descr = "Run the entire discovered test set."))           \
    X(S, jobs, OptInt,                                                         \
      OPT_INT(.cli = "-j --jobs JOBS", .validate = opt_validate_positive_int,  \
              .descr = "Run up to JOBS tests concurrently.",                   \
              .dflt = UTIL_STRINGIFY(TEST_RUNNER_DEFAULT_JOBS)))               \
    X(S, output_dir, OptString,                                                \
      OPT_STRING(.cli = "--output-dir --out-dir DIR",                          \
                 .descr = "Write captured test output under DIR.",             \
                 .dflt = TEST_RUNNER_BUILD_DIR "/test-outputs"))               \
    X(S, out_tmp, OptBool,                                                     \
      OPT_BOOL_FLAG(.cli = "--output-tmp --out-tmp",                           \
                    .descr = "Create a temporary output directory instead of " \
                             "using --out-dir."))                              \
    X(S, tests_dir, OptString,                                                 \
      OPT_STRING(.cli = "--tests-dir DIR",                                     \
                 .descr = "Discover runnable tests under DIR.",                \
                 .dflt = TEST_RUNNER_ROOT_DIR "/testing/tests"))               \
    X(S, test_bin_dir, OptString,                                              \
      OPT_STRING(.cli = "--test-bin-dir DIR",                                  \
                 .descr = "Read compiled C test binaries from DIR.",           \
                 .dflt = TEST_RUNNER_BUILD_DIR "/obj/test-bin"))               \
    X(S, timeout_seconds, OptDouble,                                           \
      OPT_DOUBLE(.cli = "--timeout SECONDS",                                   \
                 .validate = opt_validate_non_negative_double,                 \
                 .descr = "Set the per-test timeout in seconds. Use 0 to "     \
                          "disable timeouts.",                                 \
                 .dflt = "180.0"))                                             \
    X(S, output_passthrough, OptBool,                                          \
      OPT_BOOL_FLAG(.cli = "-p --output-passthrough",                          \
                    .descr = "Mirror test stdout/stderr live while still "     \
                             "capturing it."))                                 \
    X(S, debug_flip_exit_probability, OptDouble,                               \
      OPT_DOUBLE(.cli = "--debug-flip-exit-probability P",                     \
                 .validate = opt_validate_probability,                         \
                 .descr = "Flip child exit codes with probability P for "      \
                          "debugging."))                                       \
    X(S, debug_parent_setpgid_delay_seconds, OptDouble,                        \
      OPT_DOUBLE(.cli = "--debug-parent-setpgid-delay SECONDS",                \
                 .validate = opt_validate_non_negative_double,                 \
                 .descr = "Delay the parent-side setpgid() call to exercise "  \
                          "timeout races."))                                   \
    X(S, debug_parent_output_chunk_delay_seconds, OptDouble,                   \
      OPT_DOUBLE(.cli = "--debug-parent-output-chunk-delay SECONDS",           \
                 .validate = opt_validate_non_negative_double,                 \
                 .descr = "Sleep between captured-output chunks to exercise "  \
                          "drain timing."))                                    \
    X(S, patterns, OptStringList,                                              \
      OPT_STRING_LIST(.cli = "PATTERN",                                        \
                      .descr =                                                 \
                          "Select tests by shell-style wildcard PATTERN. "     \
                          "Patterns first match test names, then absolute "    \
                          "test paths. Relative path patterns are resolved "   \
                          "against the current directory. Use '|' for "        \
                          "alternation.",                                      \
                      .positional = true))

OPT_DEFINE_STRUCT(TestRunnerCliArgs, TEST_RUNNER_CLI_OPTIONS)

OPT_DEFINE_PROGRAM_PARSER_NO_COMMANDS(
    TestRunnerCli, OPT_PROGRAM(.program_name = "test-runner"),
    TestRunnerCliArgs);

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

// Build the root-level timing file path (`<output-root>/test-times.txt`).
// The caller owns the returned string and must free it with str_free.
static String timing_file_path(const char *output_root) {
    return path_join(output_root, timing_file_name);
}

// pselect() only wakes for signals that are actually caught, so install a
// no-op SIGCHLD handler purely to interrupt the sleep when the child changes
// state. The main loop still does the real reap via waitpid().
static void signal_state_sigchld_handler(int signum) { (void)signum; }

// Record the first SIGINT/SIGTERM so the main loop can stop scheduling work
// and begin shutting down running tests.
static void signal_state_shutdown_handler(int signum) {
    // IMGNEKO_UNCOVERED_OK: It's hard to call the handler twice
    if (pending_shutdown_signal == 0)
        pending_shutdown_signal = signum;
}

// Install the signal-handler and mask state needed for the parent event loop to
// sleep in pselect() without racing against child exits.
static void signal_state_init(TestSignalState *signal_state) {
    struct sigaction sigchld_action = {0};
    struct sigaction shutdown_action = {0};
    sigset_t blocked_signals;

    pending_shutdown_signal = 0;

    // Catch SIGCHLD with a no-op handler so pselect() wakes when a child exits.
    sigemptyset(&sigchld_action.sa_mask);
    sigchld_action.sa_handler = signal_state_sigchld_handler;
    require(sigaction(SIGCHLD, &sigchld_action,
                      &signal_state->old_sigchld_action) == 0,
            "sigaction failed: %errno");

    // Catch SIGINT/SIGTERM so the runner can stop early, terminate its
    // children, and still print the partial summary before exiting.
    sigemptyset(&shutdown_action.sa_mask);
    shutdown_action.sa_handler = signal_state_shutdown_handler;
    require(sigaction(SIGINT, &shutdown_action,
                      &signal_state->old_sigint_action) == 0,
            "sigaction failed: %errno");
    require(sigaction(SIGTERM, &shutdown_action,
                      &signal_state->old_sigterm_action) == 0,
            "sigaction failed: %errno");

    // Block SIGCHLD, SIGINT, and SIGTERM in the parent outside pselect() so
    // those events cannot slip between the current poll/update step and the
    // next sleep.
    sigemptyset(&blocked_signals);
    sigaddset(&blocked_signals, SIGCHLD);
    sigaddset(&blocked_signals, SIGINT);
    sigaddset(&blocked_signals, SIGTERM);
    require(sigprocmask(SIG_BLOCK, &blocked_signals,
                        &signal_state->old_signal_mask) == 0,
            "sigprocmask failed: %errno");

    // During pselect() we want the caller's original mask, except SIGCHLD,
    // SIGINT, and SIGTERM must be unblocked so those events interrupt the
    // sleep.
    signal_state->wait_mask = signal_state->old_signal_mask;
    sigdelset(&signal_state->wait_mask, SIGCHLD);
    sigdelset(&signal_state->wait_mask, SIGINT);
    sigdelset(&signal_state->wait_mask, SIGTERM);
    signal_state->installed = true;
}

// Restore the caller's signal state after the wait loop finishes.
static void signal_state_deinit(TestSignalState *signal_state) {
    // IMGNEKO_UNCOVERED_OK[2 lines]: Defensive. We always install it first.
    if (!signal_state->installed)
        return;

    require(sigprocmask(SIG_SETMASK, &signal_state->old_signal_mask, NULL) == 0,
            "sigprocmask restore failed: %errno");
    require(sigaction(SIGCHLD, &signal_state->old_sigchld_action, NULL) == 0,
            "sigaction restore failed: %errno");
    require(sigaction(SIGINT, &signal_state->old_sigint_action, NULL) == 0,
            "sigaction restore failed: %errno");
    require(sigaction(SIGTERM, &signal_state->old_sigterm_action, NULL) == 0,
            "sigaction restore failed: %errno");
    pending_shutdown_signal = 0;
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

// Close a running test's output pipe once the runner has reached EOF or once
// the short output-drain deadline expires.
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

// Read a chunk from the merged child-output pipe, append it to the captured
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

// Fork and exec a test case, returning a fully initialized RunningTest that
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
        if (sigprocmask(SIG_SETMASK, &signal_state->old_signal_mask, NULL) !=
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
            cstr_sanitize_for_diagnostic(output_dir_text, 255,
                                         config->test_output_dir);
            fprintf(stderr, "error: failed to chdir to %s: %s\n",
                    output_dir_text, strerror(errno));
            _exit(127);
        }
        execvp(argv[0], argv);
        cstr_sanitize_for_diagnostic(argv0_text, 255, argv[0]);
        fprintf(stderr, "error: failed to exec %s: %s\n", argv0_text,
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
        .interrupted = false,
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
            // Reaping the direct child does not guarantee the merged output
            // pipe is finished. The kernel may still have buffered bytes, or a
            // detached descendant may still hold the write end open, so set up
            // drain deadline.
            if (!running_test->output_pipe_closed &&
                running_test->output_drain_deadline_seconds == 0.0) {
                running_test->output_drain_deadline_seconds =
                    time_monotonic_seconds() + output_drain_grace_seconds;
            }
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
        now_seconds + output_drain_grace_seconds;

    signal_running_test_process_group(running_test, SIGTERM);
}

// Stop scheduling new tests after SIGINT/SIGTERM and gracefully terminate the
// currently running children through the same timeout/escalation machinery used
// for ordinary per-test timeouts.
static void maybe_begin_test_run_shutdown(TestRunnerState *state) {
    double now_seconds;
    sigset_t pending_signals;
    int shutdown_signal_number = (int)pending_shutdown_signal;

    // If shutdown has already begun, nothing to do.
    if (state->shutdown_requested)
        return;

    // Check for a pending shutdown signal.
    if (shutdown_signal_number == 0) {
        require(sigpending(&pending_signals) == 0, "sigpending failed: %errno");
        if (sigismember(&pending_signals, SIGINT) == 1)
            shutdown_signal_number = SIGINT;
        else if (sigismember(&pending_signals, SIGTERM) == 1)
            shutdown_signal_number = SIGTERM;
    }

    if (shutdown_signal_number == 0)
        return;

    state->shutdown_requested = true;
    state->shutdown_signal_number = shutdown_signal_number;
    state->summary.interrupted +=
        state->selected_cases.size - state->next_selected_index;
    now_seconds = time_monotonic_seconds();

    // Mark running tests as interrupted.
    for (size_t i = 0; i < state->running_tests.size; ++i) {
        RunningTest *running_test = &state->running_tests.data[i];

        // IMGNEKO_UNCOVERED_OK[2 lines]
        if (running_test->child_reaped && running_test->output_pipe_closed)
            continue;

        running_test->interrupted = true;

        if (!running_test->child_reaped && !running_test->timed_out)
            sigterm_running_test_process(running_test, now_seconds);
    }
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
        // child is gone, so stop draining after a short period.
        if (running_test->output_drain_deadline_seconds > 0.0 &&
            !running_test->output_pipe_closed &&
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
        if (running_test->output_drain_deadline_seconds > 0.0 &&
            !running_test->output_pipe_closed &&
            running_test->output_drain_deadline_seconds < deadline)
            deadline = running_test->output_drain_deadline_seconds;
    }

    if (deadline == DBL_MAX)
        return false;

    *deadline_out = deadline;
    return true;
}

// Sleep until a child emits output, exits, or reaches the next timeout
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

    // If there are no running tests, pselect() will wait forever (since there
    // are no events to wake it up), so skip it and return immediately to let
    // the caller handle this.
    if (running_tests->size == 0)
        return;

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

    // Bound the sleep by the earliest pending transition. If there is no
    // specific deadline, still wake periodically so a missed signal cannot
    // leave the runner asleep forever.
    if (next_running_test_deadline(running_tests, &next_deadline)) {
        wait_timeout = time_timeout_until_deadline(next_deadline, now_seconds);
        wait_timeout_ptr = &wait_timeout;
    } else {
        wait_timeout = time_timeout_until_deadline(
            now_seconds + event_loop_idle_poll_seconds, now_seconds);
        wait_timeout_ptr = &wait_timeout;
    }

    // Sleep until output arrives, a child state change interrupts pselect(),
    // or the next deadline expires.
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

            if (pump_running_test_output(running_test, output_passthrough)) {
                if (output_chunk_delay_seconds > 0.0) {
                    // Debug-only hook to slow output draining enough to
                    // exercise the state where the child is already reaped but
                    // unread bytes still remain buffered in the merged output
                    // pipe.
                    time_sleep_seconds(output_chunk_delay_seconds);
                }
                // IMGNEKO_UNCOVERED_OK[2 lines]: Hard to trigger closed pipe.
                if (running_test->child_reaped &&
                    !running_test->output_pipe_closed) {
                    // A successful read after waitpid() means the child is
                    // gone but the pipe still had buffered output. Restart the
                    // drain window after the read so we do not close before
                    // consuming nearby buffered bytes, while still bounding a
                    // pipe kept open by a descendant.
                    running_test->output_drain_deadline_seconds =
                        time_monotonic_seconds() + output_drain_grace_seconds;
                }
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
        cstr_sanitize_for_diagnostic(argv0_text, 255, argv[0]);
        fprintf(stderr, "error: failed to exec %s: %s\n", argv0_text,
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
        cstr_sanitize_for_diagnostic(output_path_text, 255, output_file_path);
        fprintf(stderr, "error: failed to read captured output %s: %s\n\n",
                output_path_text, strerror(errno));
        return;
    }

    if (lines.size == 0) {
        cstr_sanitize_for_diagnostic(output_path_text, 255, output_file_path);
        fprintf(stderr, "output is empty: %s\n", output_path_text);
        str_array_free(&lines);
        return;
    }

    fflush(stdout);
    fflush(stderr);

    BufferedWriter writer = buffered_writer_for_fd(STDERR_FILENO);
    cstr_sanitize_for_diagnostic(output_path_text, 255, output_file_path);
    buffered_writer_printf(&writer,
                           "===== LAST %zu LINES OF TEST OUTPUT %s {{{ =====\n",
                           max_lines, output_path_text);
    for (size_t i = 0; i < lines.size; ++i) {
        str_trim_trailing_chars(&lines.data[i], "\r\n");
        String escaped =
            str_from_escaped_bytes(lines.data[i].cstr, lines.data[i].len);

        buffered_writer_make_room(&writer, escaped.len + 1);
        buffered_writer_write(&writer, escaped.cstr, escaped.len);
        buffered_writer_write(&writer, "\n", 1);
        str_free(escaped);
    }
    buffered_writer_printf(&writer, "===== }}} END TEST OUTPUT =====\n\n");
    buffered_writer_flush(&writer);
    buffered_writer_free(&writer);

    str_array_free(&lines);
}

// Check whether a test case's name matches a pattern.
//
// C tests with discovered subtests have two stable identifiers:
// - id:      `foo.c/subtest_name`
// - file_id: `foo.c`
//
// Name matching succeeds if at least one of these two matches the pattern.
static bool test_name_matches_pattern(const TestCase *test_case,
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

// Check whether a test case's absolute path matches a pattern. For C subtests,
// the absolute id is the source path plus the subtest name:
// `/tests/root/foo.c/subtest_name`.
static bool test_abs_path_matches_pattern(const TestCase *test_case,
                                          const char *pattern) {
    if (fnmatch(pattern, test_case->file_abs_path.cstr, 0) == 0)
        return true;

    if (test_case->c_subtest.len == 0)
        return false;

    String abs_id = str_copy(test_case->file_abs_path);
    str_push(abs_id, '/');
    str_append_str(abs_id, test_case->c_subtest);
    bool matched = fnmatch(pattern, abs_id.cstr, 0) == 0;

    str_free(abs_id);
    return matched;
}

// Check whether a test case matches a flattened command-line pattern.
static bool test_matches_pattern(const TestCase *test_case,
                                 const TestPattern *pattern) {
    if (test_name_matches_pattern(test_case, pattern->text.cstr))
        return true;

    return test_abs_path_matches_pattern(test_case,
                                         pattern->abs_path_text.cstr);
}

// Add one flattened pattern atom to `out`. The absolute-path fallback is
// computed now so relative patterns use the invocation cwd before the runner
// changes directories for discovery and execution.
static void push_test_pattern(const char *text, size_t len,
                              TestPatternArray *out) {
    TestPattern pattern = {
        .text = str_from_data(text, len),
        .abs_path_text = str_empty,
        .matched = false,
    };

    if (!path_resolve_absolute(&pattern.abs_path_text, pattern.text.cstr))
        die_errno("failed to resolve test pattern");

    arr_push(*out, pattern);
}

// Expand `|` alternation in test patterns into individual match patterns.
// Return false after reporting a CLI error if any alternation part is empty.
static bool flatten_test_patterns(const StringArray *patterns,
                                  TestPatternArray *out) {
    for (size_t i = 0; i < patterns->size; ++i) {
        const char *pattern = patterns->data[i].cstr;
        const char *cursor = pattern;

        do {
            const char *bar = strchr(cursor, '|');
            size_t len = bar != NULL ? (size_t)(bar - cursor) : strlen(cursor);

            if (len == 0) {
                cstr_sanitize_for_diagnostic(pattern_text, 255, pattern);
                fprintf(stderr, "error: invalid test pattern: '%s'\n",
                        pattern_text);
                return false;
            }

            push_test_pattern(cursor, len, out);
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

        str_sanitize_for_diagnostic(rel_path_text, 255, file->rel_path);
        cstr_sanitize_for_diagnostic(exe_path_text, 255, exe_path);
        fprintf(stderr,
                "error: missing built C test binary for %s at %s\n"
                "       build C tests first with `%s` or `make -C %s test`\n",
                rel_path_text, exe_path_text, hint.cstr, build_dir);
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
        str_sanitize_for_diagnostic(rel_path_text, 255, file->rel_path);
        fprintf(stderr, "error: %s --list failed with status %d\n",
                rel_path_text, status);
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
                str_sanitize_for_diagnostic(rel_path_text, 255, file->rel_path);
                fprintf(stderr, "error: invalid empty subtest name in %s\n",
                        rel_path_text);
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
                                     .id = str_copy(file->rel_path),
                                     .file_id = str_copy(file->rel_path),
                                     .file_abs_path = str_copy(file->abs_path),
                                     .c_exe_path = str_copy(exe_path),
                                     .c_subtest = str_empty,
                                 }));
            } else {
                for (j = 0; j < subtests.size; ++j) {
                    String id = str_copy(file->rel_path);

                    str_push(id, '/');
                    str_append_str(id, subtests.data[j].name);
                    arr_push(*cases,
                             ((TestCase){
                                 .kind = TEST_KIND_C,
                                 .marker = subtests.data[j].marker,
                                 .id = id,
                                 .file_id = str_copy(file->rel_path),
                                 .file_abs_path = str_copy(file->abs_path),
                                 .c_exe_path = str_copy(exe_path),
                                 .c_subtest = str_copy(subtests.data[j].name),
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
                             .id = str_copy(file->rel_path),
                             .file_id = str_copy(file->rel_path),
                             .file_abs_path = str_copy(file->abs_path),
                             .c_exe_path = str_empty,
                             .c_subtest = str_empty,
                         }));
    }

    qsort(cases->data, cases->size, sizeof(cases->data[0]), compare_test_cases);
}

// Print a discovered test id, appending its marker when present.
static void print_listed_test(const TestCase *test_case) {
    const char *marker_name = test_marker_name(test_case->marker);
    str_sanitize_for_diagnostic(test_id_text, 255, test_case->id);

    printf("%s", test_id_text);
    if (marker_name != NULL)
        printf(" %s", marker_name);
    fputc('\n', stdout);
}

// Print a named list of tests with a heading, skipping the list if empty.
static void print_named_test_list(const char *heading,
                                  const StringArray *tests) {
    if (tests->size == 0)
        return;

    printf("\n%s:\n", heading);
    for (size_t i = 0; i < tests->size; ++i) {
        str_sanitize_for_diagnostic(test_id_text, 255, tests->data[i]);
        printf("  %s\n", test_id_text);
    }
}

// Print a non-zero summary counter.
static void print_summary_count(const char *label, size_t count) {
    if (count != 0)
        printf("  %s: %zu\n", label, count);
}

// Print the [current/total] prefix for a recorded test result line.
static void print_test_result_prefix(const TestRunnerState *state) {
    printf("[%zu/%zu] ", state->num_recorded_results + 1, state->discovered);
}

// Print the selected test count and parallelism once discovery is complete and
// before any test-specific status lines begin.
static void print_run_start_message(const CliOptions *options,
                                    const TestRunnerState *state) {
    printf("Starting test run: %d job%s, %zu discovered test%s\n",
           options->jobs, options->jobs == 1 ? "" : "s", state->discovered,
           state->discovered == 1 ? "" : "s");
    fflush(stdout);
}

// Print the user-facing shutdown message for a runner-shutdown signal.
static void print_shutdown_signal_message(int signal_number) {
    switch (signal_number) {
    case SIGINT:
        puts("Interrupted by SIGINT.");
        return;
    case SIGTERM:
        puts("Terminated by SIGTERM.");
        return;
    // IMGNEKO_UNCOVERED_OK[3 lines]
    default:
        printf("Interrupted by signal %d.\n", signal_number);
        return;
    }
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
    str_sanitize_for_diagnostic(test_id_text, 255, test_case->id);
    printf("DEBUG: flipped exit code for %s (%d -> %d)\n", test_id_text,
           test_exit_code, flipped_exit_code);
    return flipped_exit_code;
}

// Translate a completed running-test record into the final exit status and
// elapsed time reported to the summary logic.
static TestRunResult
test_run_result_from_running_test(const RunningTest *running_test) {
    TestRunResult result = {
        .exit_code = 1,
        .interrupted = running_test->interrupted,
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

    if (!run_result.interrupted && !run_result.timed_out) {
        run_result.exit_code = maybe_flip_exit_code(
            test_case, run_result.exit_code, flip_exit_probability);
    }
    classified.run_result = run_result;

    if (run_result.interrupted) {
        classified.outcome = TEST_OUTCOME_INTERRUPTED;
    } else if (run_result.timed_out) {
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
    case TEST_OUTCOME_INTERRUPTED:
        summary->interrupted++;
        return;
    }
}

// Print the final status line for a classified test result and, for failures
// and timeouts, include the captured output tail unless passthrough already
// showed the full stream live.
static void print_classified_test_result(const TestRunnerState *state,
                                         const TestCase *test_case,
                                         const ClassifiedTestResult *classified,
                                         const char *output_file_path,
                                         bool output_passthrough) {
    str_sanitize_for_diagnostic(test_id_text, 255, test_case->id);

    // IMGNEKO_UNCOVERED_OK
    switch (classified->outcome) {
    case TEST_OUTCOME_PASS:
        print_test_result_prefix(state);
        printf("PASS: %s\n", test_id_text);
        break;
    case TEST_OUTCOME_XFAIL:
        print_test_result_prefix(state);
        printf("XFAIL: %s\n", test_id_text);
        break;
    case TEST_OUTCOME_DISABLED:
        print_test_result_prefix(state);
        printf("DISABLED: %s\n", test_id_text);
        break;
    case TEST_OUTCOME_XPASS:
        print_test_result_prefix(state);
        printf("XPASS: %s\n", test_id_text);
        break;
    case TEST_OUTCOME_TIMEOUT:
        printf("\n");
        print_test_result_prefix(state);
        printf("TIMEOUT: %s\n", test_id_text);
        fflush(stdout);
        if (!output_passthrough)
            print_output_tail(output_file_path, 20);
        break;
    case TEST_OUTCOME_FAIL:
        printf("\n");
        print_test_result_prefix(state);
        printf("FAIL: %s\n", test_id_text);
        fflush(stdout);
        if (!output_passthrough)
            print_output_tail(output_file_path, 20);
        break;
    case TEST_OUTCOME_INTERRUPTED:
        break;
    }

    fflush(stdout);
}

// Open the root-level timing file before the run starts so every recorded test
// can append a machine-readable line immediately.
static void open_timing_file(TestRunnerState *state, const char *output_dir) {
    require(mkdir_p(output_dir), "failed to create a directory: %errno");
    state->timing_file_path = timing_file_path(output_dir);
    state->timing_file = fopen(state->timing_file_path.cstr, "w");
    require(state->timing_file != NULL,
            "failed to open the timing file: %errno");
}

// Append a finalized test result to the timing file in
// `time outcome test_name` order.
static void append_timing_file_record(const TestRunnerState *state,
                                      const TestCase *test_case,
                                      const ClassifiedTestResult *classified) {
    require(state->timing_file != NULL, "timing file must be open");
    require(fprintf(state->timing_file, "%.2f %s %s\n",
                    classified->run_result.elapsed_seconds,
                    test_outcome_record_name(classified->outcome),
                    test_case->id.cstr) >= 0,
            "failed to write the timing file: %errno");
    require(fflush(state->timing_file) == 0,
            "failed to flush the timing file: %errno");
}

// Update the summary, print the terminal status line, and record the
// timing entry for an already-classified test result.
static void
record_classified_test_result(TestRunnerState *state, const TestCase *test_case,
                              const ClassifiedTestResult *classified,
                              const char *output_file_path,
                              bool output_passthrough) {
    update_summary_for_classified_result(&state->summary, test_case,
                                         classified);
    print_classified_test_result(state, test_case, classified, output_file_path,
                                 output_passthrough);
    append_timing_file_record(state, test_case, classified);
    state->num_recorded_results++;
}

// Update state for a completed test: summary counters, status output, and the
// next result index.
static void record_test_result(TestRunnerState *state,
                               const RunningTest *running_test,
                               bool output_passthrough,
                               double flip_exit_probability) {
    const TestCase *test_case = running_test->test_case;
    ClassifiedTestResult classified = classify_test_result(
        test_case, test_run_result_from_running_test(running_test),
        flip_exit_probability);

    record_classified_test_result(state, test_case, &classified,
                                  running_test->output_file_path.cstr,
                                  output_passthrough);
}

// Release a completed running test and remove it from the dense running-test
// array while preserving the order of the remaining entries.
static void remove_running_test_at(RunningTestArray *running_tests,
                                   size_t index) {
    running_test_deinit(&running_tests->data[index]);
    arr_remove_at(*running_tests, index);
}

// Flush all running tests that have reached a terminal state into the summary
// and remove them from the running set.
static void finalize_completed_running_tests(TestRunnerState *state,
                                             bool output_passthrough,
                                             double flip_exit_probability) {
    for (size_t i = 0; i < state->running_tests.size;) {
        RunningTest *running_test = &state->running_tests.data[i];

        if (!running_test_is_complete(running_test)) {
            i++;
            continue;
        }

        record_test_result(state, running_test, output_passthrough,
                           flip_exit_probability);
        remove_running_test_at(&state->running_tests, i);
    }
}

// Prepend build/bin to PATH and export stable test-runner environment
// variables.
static void prepare_env_vars(void) {
    const char *old_path = getenv("PATH");
    String build_dir_abs = absolute_build_dir();
    String bin_dir = path_join(build_dir_abs.cstr, "bin");
    String new_path = str_copy(bin_dir);

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

static void cli_options_init(CliOptions *options) {
    *options = (CliOptions){
        .patterns = arr_empty,
        .tests_dir = str_empty,
        .output_dir = str_empty,
        .test_bin_dir = str_empty,
    };
}

static void cli_options_deinit(CliOptions *options) {
    str_free(options->test_bin_dir);
    str_free(options->output_dir);
    str_free(options->tests_dir);
    test_pattern_array_free(&options->patterns);
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
        .timing_file_path = str_empty,
        .timing_file = NULL,
        .next_selected_index = 0,
        .run_start_seconds = time_monotonic_seconds(),
        .shutdown_signal_number = 0,
    };
}

static void test_runner_state_deinit(TestRunnerState *state) {
    // By the time teardown runs, every started child must already have been
    // finalized and removed from running_tests.
    assert(state->running_tests.size == 0);
    if (state->timing_file != NULL) {
        require(fclose(state->timing_file) == 0,
                "failed to close the timing file: %errno");
        state->timing_file = NULL;
    }
    str_free(state->timing_file_path);
    arr_free(state->running_tests);
    arr_free(state->selected_cases);
    str_array_free(&state->summary.timed_out_tests);
    str_array_free(&state->summary.xpassed_tests);
    str_array_free(&state->summary.failed_tests);
    test_case_array_free(&state->cases);
    test_file_array_free(&state->files);
}

// Resolve a parsed path option against the caller's current working
// directory.
static void resolve_cli_path_option(String *out, const char *text,
                                    const char *error_message) {
    if (path_resolve_absolute(out, text))
        return;

    die_errno(error_message);
}

// Parse argv into CliOptions. Returns false when the caller should exit
// immediately, with exit_code_out already set and any diagnostics printed.
static bool parse_cli_args(int argc, char **argv, CliOptions *options,
                           int *exit_code_out) {
    ParsedTestRunnerCli parsed = {0};
    const TestRunnerCliArgs *parsed_options = &parsed.top_level;
    int rc = opt_run_program_parser(&TestRunnerCli_parser, argc, argv, &parsed);

    if (rc != 0 || parsed.must_exit) {
        *exit_code_out = rc;
        return false;
    }

    options->list_only = parsed_options->list_only.value;
    options->run_all = parsed_options->run_all.value;
    options->output_passthrough = parsed_options->output_passthrough.value;
    options->jobs = parsed_options->jobs.value;
    options->timeout_seconds = parsed_options->timeout_seconds.value;
    options->debug_flip_exit_probability =
        parsed_options->debug_flip_exit_probability.value;
    options->debug_parent_setpgid_delay_seconds =
        parsed_options->debug_parent_setpgid_delay_seconds.value;
    options->debug_parent_output_chunk_delay_seconds =
        parsed_options->debug_parent_output_chunk_delay_seconds.value;

    options->out_tmp = parsed_options->out_tmp.value;
    if (!options->out_tmp) {
        resolve_cli_path_option(&options->output_dir,
                                parsed_options->output_dir.value.cstr,
                                "failed to resolve output directory");
    }
    resolve_cli_path_option(&options->tests_dir,
                            parsed_options->tests_dir.value.cstr,
                            "failed to resolve tests directory");
    resolve_cli_path_option(&options->test_bin_dir,
                            parsed_options->test_bin_dir.value.cstr,
                            "failed to resolve test-bin directory");

    if (!flatten_test_patterns(&parsed_options->patterns.value,
                               &options->patterns)) {
        *exit_code_out = 2;
        TestRunnerCliArgs_deinit(&parsed.top_level);
        return false;
    }

    if (options->run_all && options->patterns.size != 0) {
        fprintf(stderr, "error: --all cannot be combined with patterns\n");
        *exit_code_out = 2;
        TestRunnerCliArgs_deinit(&parsed.top_level);
        return false;
    }

    TestRunnerCliArgs_deinit(&parsed.top_level);
    return true;
}

// Validate the parsed command-line options and apply any runtime setup they
// require.
static void finalize_cli_options(CliOptions *options) {
    if (!options->list_only) {
        if (options->out_tmp) {
            // Use options->output_dir directly as the mkdtemp buffer.
            options->output_dir = str_from_cstr("/tmp/imgneko-test-XXXXXX");
            require(mkdtemp(options->output_dir.cstr) != NULL,
                    "failed to mkdtemp");
        }
        validate_output_dir(options->output_dir.cstr);
        require_empty_output_dir(options->output_dir.cstr);
    }
    if (options->debug_flip_exit_probability > 0.0)
        seed_debug_random();
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
        str_sanitize_for_diagnostic(tests_dir_text, 255, options->tests_dir);
        fprintf(stderr, "error: no tests found under %s\n", tests_dir_text);
        *exit_code_out = 2;
        return false;
    }

    discover_test_cases(&state->files, options->test_bin_dir.cstr,
                        &state->cases);
    // IMGNEKO_UNCOVERED_OK_START
    if (state->cases.size == 0) {
        str_sanitize_for_diagnostic(tests_dir_text, 255, options->tests_dir);
        fprintf(stderr, "error: no runnable tests found under %s\n",
                tests_dir_text);
        *exit_code_out = 2;
        return false;
    }
    // IMGNEKO_UNCOVERED_OK_END
    return true;
}

// Append every discovered test case that matches the active patterns to
// selected_cases, preserving discovery order. An empty pattern list selects the
// whole discovered test set.
static void collect_selected_test_cases(const TestCaseArray *cases,
                                        TestPatternArray *patterns,
                                        TestCasePtrArray *selected_cases) {
    for (size_t i = 0; i < cases->size; ++i) {
        const TestCase *test_case = &cases->data[i];
        bool selected = patterns->size == 0;

        for (size_t j = 0; j < patterns->size; ++j) {
            TestPattern *pattern = &patterns->data[j];

            if (!test_matches_pattern(test_case, pattern))
                continue;

            pattern->matched = true;
            selected = true;
        }

        if (selected)
            arr_push(*selected_cases, test_case);
    }
}

// Report pattern atoms that did not select any discovered test case.
static bool validate_test_patterns_matched(const TestPatternArray *patterns) {
    bool ok = true;

    for (size_t i = 0; i < patterns->size; ++i) {
        const TestPattern *pattern = &patterns->data[i];

        if (!pattern->matched) {
            str_sanitize_for_diagnostic(pattern_text, 255, pattern->text);
            fprintf(stderr, "error: no tests matched pattern: '%s'\n",
                    pattern_text);
            ok = false;
        }
    }

    return ok;
}

// Print the already-selected tests in discovery order for `--list`.
static void list_selected_tests(const TestRunnerState *state) {
    for (size_t i = 0; i < state->selected_cases.size; ++i)
        print_listed_test(state->selected_cases.data[i]);
}

// Record a disabled test without spawning a child process.
static void record_disabled_test(TestRunnerState *state,
                                 const TestCase *test_case) {
    ClassifiedTestResult classified = {
        .run_result =
            {
                .exit_code = 0,
                .elapsed_seconds = 0.0,
            },
        .outcome = TEST_OUTCOME_DISABLED,
    };

    record_classified_test_result(state, test_case, &classified, NULL, false);
}

// Start a selected test or, for disabled entries, record the synthetic result
// immediately without consuming a job slot.
static void start_selected_test(const CliOptions *options,
                                TestRunnerState *state,
                                const TestCase *test_case) {
    String test_output_dir;
    String output_file_path;
    TestRunConfig config;
    RunningTest running_test;

    if (test_case->marker == TEST_MARKER_DISABLED) {
        record_disabled_test(state, test_case);
        return;
    }

    str_sanitize_for_diagnostic(test_id_text, 255, test_case->id);
    printf("RUN: %s\n", test_id_text);
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
// state->next_selected_index reaches the end of state->selected_cases. The
// function advances state->next_selected_index for every test it consumes from
// the selected list.
static void start_ready_tests(const CliOptions *options,
                              TestRunnerState *state) {
    while (state->running_tests.size < (size_t)options->jobs &&
           state->next_selected_index < state->selected_cases.size) {
        const TestCase *test_case =
            state->selected_cases.data[state->next_selected_index++];
        start_selected_test(options, state, test_case);
    }
}

// Run the selected tests under the single-threaded event loop until every test
// has either been recorded immediately or started, reaped, and finalized.
static void run_selected_tests(const CliOptions *options,
                               TestRunnerState *state) {
    require(state->signal_state.installed,
            "signal state must be initialized before running tests");

    while ((!state->shutdown_requested &&
            state->next_selected_index < state->selected_cases.size) ||
           state->running_tests.size != 0) {
        // First launch as many new tests as we can. Disabled tests are
        // accounted for synchronously here and do not enter running_tests.
        if (!state->shutdown_requested)
            start_ready_tests(options, state);

        // Then wait until some running test produces output, exits, or reaches
        // its next timeout transition. The wait helper may return immediately
        // if a child already completed before we went to sleep.
        wait_for_running_test_events(
            &state->running_tests, options->output_passthrough,
            options->debug_parent_output_chunk_delay_seconds,
            &state->signal_state);

        // Check if we received a shutdown signal and switch to shutdown mode if
        // so (signal running tests to terminate, stop running new tests).
        maybe_begin_test_run_shutdown(state);

        // Finally, flush every completed child into the summary and free its
        // slot so the next loop iteration can start more work.
        finalize_completed_running_tests(state, options->output_passthrough,
                                         options->debug_flip_exit_probability);
    }
}

// Finish the run by checking whether any test matched and printing the final
// summary for execution mode.
static void finalize_run_result(const CliOptions *options,
                                TestRunnerState *state, int *exit_code_out) {
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
    print_summary_count("interrupted", state->summary.interrupted);
    printf("\n");

    if (state->shutdown_requested) {
        *exit_code_out = 128 + state->shutdown_signal_number;
        print_shutdown_signal_message(state->shutdown_signal_number);
    } else if (state->summary.failed != 0 || state->summary.xpassed != 0 ||
               state->summary.timed_out != 0) {
        *exit_code_out = 1;
    }

    printf("Time: %.3f s\n",
           time_monotonic_seconds() - state->run_start_seconds);
    str_sanitize_for_diagnostic(output_dir_text, 255, options->output_dir);
    str_sanitize_for_diagnostic(timing_file_text, 255, state->timing_file_path);
    printf("Output dir: %s\n", output_dir_text);
    printf("Timing file: %s\n", timing_file_text);
    printf("Result: %s\n", state->shutdown_requested
                               ? "INTERRUPTED"
                               : (*exit_code_out == 0 ? "SUCCESS" : "FAILURE"));
}

int main(int argc, char **argv) {
    CliOptions options;
    TestRunnerState state;
    int exit_code = 0;

    cli_options_init(&options);
    test_runner_state_init(&state);

    if (!parse_cli_args(argc, argv, &options, &exit_code))
        goto cleanup;
    finalize_cli_options(&options);

    prepare_test_run_environment();
    if (!discover_tests_for_run(&options, &state, &exit_code))
        goto cleanup;
    collect_selected_test_cases(&state.cases, &options.patterns,
                                &state.selected_cases);
    state.discovered = state.selected_cases.size;

    if (!validate_test_patterns_matched(&options.patterns)) {
        exit_code = 2;
        goto cleanup;
    }

    if (options.list_only) {
        list_selected_tests(&state);
        goto cleanup;
    }

    open_timing_file(&state, options.output_dir.cstr);
    print_run_start_message(&options, &state);
    signal_state_init(&state.signal_state);
    run_selected_tests(&options, &state);
    finalize_run_result(&options, &state, &exit_code);
    signal_state_deinit(&state.signal_state);

cleanup:
    test_runner_state_deinit(&state);
    cli_options_deinit(&options);
    return exit_code;
}
