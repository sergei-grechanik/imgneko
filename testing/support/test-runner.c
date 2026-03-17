// Enable POSIX APIs used in this file (getline, strdup/strndup, setenv, etc.).
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fnmatch.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "util/array.h"
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
    String rel_path;
    String abs_path;
} TestFile;

// One runnable test case expanded from a discovered file.
//
// Example for a C test with subtests:
// id:            `integration/unit/util/string.c/empty_and_reserve`
// file_id:       `integration/unit/util/string.c`
// file_abs_path: `/repo/testing/tests/integration/unit/util/string.c`
// c_exe_path:    `/repo/build/asan/test-bin/integration/unit/util/string.c.bin`
// c_subtest:     `empty_and_reserve`
//
// For an executable test file, id and file_id both equal the relative path, and
// c_exe_path/c_subtest are empty.
typedef struct TestCase {
    TestKind kind;
    String id;
    String file_id;
    String file_abs_path;
    String c_exe_path;
    String c_subtest;
} TestCase;

DEFINE_ARRAY_TYPE(StringArray, String)
DEFINE_ARRAY_TYPE(TestFileArray, TestFile)
DEFINE_ARRAY_TYPE(TestCaseArray, TestCase)

// Absolute path to the repository root (/path/to/imgneko).
static const char *const root_dir = TEST_RUNNER_ROOT_DIR;
// Absolute path to the selected build directory (for example:
// /path/to/imgneko/build/default).
static const char *const build_dir = TEST_RUNNER_BUILD_DIR;
// Repository-relative directory containing runnable tests.
static const char *const tests_root_rel = "testing/tests";

static void die_errno(const char *message) {
    fprintf(stderr, "error: %s: %s\n", message, strerror(errno));
    exit(1);
}

static void string_array_push_copy(StringArray *array, const char *item) {
    arr_push(*array, str_from_cstr(item));
}

static void string_array_free(StringArray *array) {
    for (size_t i = 0; i < array->size; ++i)
        str_free(array->data[i]);
    arr_free(*array);
}

static void test_file_array_free(TestFileArray *array) {
    for (size_t i = 0; i < array->size; ++i) {
        str_free(array->data[i].rel_path);
        str_free(array->data[i].abs_path);
    }
    arr_free(*array);
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

// Join two path segments and return a newly allocated absolute/relative path.
// The caller owns the returned string and must free it with str_free.
static String join_two_paths(const char *left, const char *right) {
    String result = str_from_cstr(left);

    if (result.len > 0 && result.cstr[result.len - 1] != '/')
        str_push(result, '/');
    str_append_cstr(result, right);
    return result;
}

// Trim leading and trailing ASCII whitespace from a string in place.
static void trim_in_place(char *line) {
    size_t len;
    size_t start = 0;
    size_t end;

    while (isspace((unsigned char)line[start])) {
        start++;
    }

    len = strlen(line);
    end = len;
    while (end > start && isspace((unsigned char)line[end - 1])) {
        end--;
    }

    if (start > 0) {
        memmove(line, line + start, end - start);
    }
    line[end - start] = '\0';
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

// Recursively discover supported test files tests_root_abs/rel_dir/** and
// append entries to `files`. `rel_dir` should be "" when called from the top
// level.
static void discover_test_files_rec(const char *tests_root_abs,
                                    const char *rel_dir, TestFileArray *files) {
    // Resolve the directory represented by rel_dir relative to tests_root_abs.
    String dir_path = rel_dir[0] == '\0'
                          ? str_from_cstr(tests_root_abs)
                          : join_two_paths(tests_root_abs, rel_dir);
    DIR *dir = opendir(dir_path.cstr);
    struct dirent *entry;

    if (dir == NULL) {
        str_free(dir_path);
        die_errno("failed to open tests directory");
    }

    while ((entry = readdir(dir)) != NULL) {
        struct stat st;
        String rel_path;
        String abs_path;
        TestKind kind;

        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        rel_path = rel_dir[0] == '\0' ? str_from_cstr(entry->d_name)
                                      : join_two_paths(rel_dir, entry->d_name);
        abs_path = join_two_paths(tests_root_abs, rel_path.cstr);

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
        } else {
            str_free(rel_path);
            str_free(abs_path);
            continue;
        }

        arr_push(*files, ((TestFile){
                             .kind = kind,
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

// Run argv in a child process and return an exit-like status code.
static int run_argv(char *const *argv) {
    pid_t pid = fork();
    int status;

    if (pid < 0) {
        die_errno("fork failed");
    }

    if (pid == 0) {
        execvp(argv[0], argv);
        fprintf(stderr, "error: failed to exec %s: %s\n", argv[0],
                strerror(errno));
        _exit(127);
    }

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

// Read the entire stream from `fd` into a NUL-terminated buffer.
// The caller owns the returned string and must free it with str_free.
static String read_all_from_fd(int fd) {
    String buffer = str_empty;

    for (;;) {
        char chunk[4096];
        ssize_t nread;

        nread = read(fd, chunk, sizeof(chunk));
        if (nread < 0) {
            str_free(buffer);
            die_errno("read failed");
        }
        if (nread == 0)
            break;

        str_append_data(buffer, chunk, (size_t)nread);
    }

    return buffer;
}

// Run argv, capture the child's stdout text, and return an exit-like status.
// The caller owns `*stdout_text` and must free it with str_free.
static int run_argv_capture_stdout(char *const *argv, String *stdout_text) {
    int pipe_fds[2];
    pid_t pid;
    int status;

    if (pipe(pipe_fds) != 0) {
        die_errno("pipe failed");
    }

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
    str_free(*stdout_text);
    *stdout_text = read_all_from_fd(pipe_fds[0]);
    close(pipe_fds[0]);

    if (waitpid(pid, &status, 0) < 0) {
        str_free(*stdout_text);
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
    String base = join_two_paths(build_dir, "test-bin");
    String final_path = join_two_paths(base.cstr, rel_path);

    str_append_cstr(final_path, ".bin");
    str_free(base);
    return final_path;
}

// Build a user-facing command hint for building C test binaries.
// The caller owns the returned string and must free it with str_free.
static String c_test_build_hint(void) {
    String hint = str_from_cstr("make -C ");

    str_append_cstr(hint, build_dir);
    str_append_cstr(hint, " test-programs");
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
static void c_test_subtests(const TestFile *file, StringArray *subtests,
                            String *exe_path_out) {
    String stdout_text = str_empty;
    String exe_path = c_test_output_path(file->rel_path.cstr);
    int status;
    char *argv[] = {exe_path.cstr, "--list", NULL};

    require_c_test_binary(file, exe_path.cstr);

    status = run_argv_capture_stdout(argv, &stdout_text);
    if (status != 0) {
        fprintf(stderr, "error: %s --list failed with status %d\n",
                file->rel_path.cstr, status);
        str_free(stdout_text);
        str_free(exe_path);
        exit(status);
    }

    char *cursor = stdout_text.cstr;
    while (cursor != NULL && *cursor != '\0') {
        char *next = strchr(cursor, '\n');
        char *line;

        if (next != NULL) {
            *next = '\0';
            line = cursor;
            cursor = next + 1;
        } else {
            line = cursor;
            cursor = NULL;
        }

        trim_in_place(line);
        if (line[0] != '\0') {
            string_array_push_copy(subtests, line);
        }
    }

    str_free(stdout_text);
    *exe_path_out = exe_path;
}

// Expand discovered files into runnable test cases.
static void discover_test_cases(const TestFileArray *files,
                                TestCaseArray *cases) {
    size_t i;

    for (i = 0; i < files->size; ++i) {
        const TestFile *file = &files->data[i];

        if (file->kind == TEST_KIND_C) {
            StringArray subtests = arr_empty;
            String exe_path = str_empty;
            size_t j;

            // A C test binary can expose multiple subtests via --list.
            c_test_subtests(file, &subtests, &exe_path);
            if (subtests.size == 0) {
                arr_push(*cases, ((TestCase){
                                     .kind = TEST_KIND_C,
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
                    str_append_str(id, subtests.data[j]);
                    arr_push(*cases,
                             ((TestCase){
                                 .kind = TEST_KIND_C,
                                 .id = id,
                                 .file_id = copy_str(file->rel_path),
                                 .file_abs_path = copy_str(file->abs_path),
                                 .c_exe_path = copy_str(exe_path),
                                 .c_subtest = copy_str(subtests.data[j]),
                             }));
                }
            }

            str_free(exe_path);
            string_array_free(&subtests);
            continue;
        }

        arr_push(*cases, ((TestCase){
                             .kind = file->kind,
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
static int run_executable_test(const TestCase *test_case) {
    char *argv[] = {test_case->file_abs_path.cstr, NULL};

    if (access(test_case->file_abs_path.cstr, X_OK) != 0) {
        fprintf(stderr, "error: test file is not executable: %s\n",
                test_case->file_abs_path.cstr);
        return 1;
    }

    return run_argv(argv);
}

// Run one compiled C test, either a selected subtest or all subtests.
static int run_c_test(const TestCase *test_case) {
    char *argv[] = {
        test_case->c_exe_path.cstr,
        test_case->c_subtest.len != 0 ? test_case->c_subtest.cstr : "--all",
        NULL,
    };

    return run_argv(argv);
}

// Prepend build/bin to PATH and export stable test-runner environment
// variables.
static void prepare_env_vars(void) {
    const char *old_path = getenv("PATH");
    String bin_dir = join_two_paths(build_dir, "bin");
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
        unsetenv("MAKELEVEL") != 0) {
        str_free(bin_dir);
        str_free(new_path);
        die_errno("failed to clear inherited make state");
    }
    if (setenv("IMGNEKO_ROOT_DIR", root_dir, 1) != 0 ||
        setenv("IMGNEKO_BUILD_DIR", build_dir, 1) != 0) {
        str_free(bin_dir);
        str_free(new_path);
        die_errno("failed to update test environment");
    }

    str_free(bin_dir);
    str_free(new_path);
}

// Print CLI usage help.
static void usage(FILE *stream) {
    fprintf(stream,
            "Usage: %s [--list] [--all] [--filter PATTERN] [PATTERN ...]\n"
            "\n"
            "Discover tests under %s/ relative to %s.\n"
            "\n"
            "Patterns use shell-style wildcards and may be joined with '|'.\n",
            "test-runner", tests_root_rel, root_dir);
}

int main(int argc, char **argv) {
    // User-specified filters and options.
    StringArray filters = arr_empty;
    bool list_only = false;
    bool run_all = false;

    // Collected files and test cases.
    TestFileArray files = arr_empty;
    TestCaseArray cases = arr_empty;

    // The directory where we look for test files.
    String tests_dir = str_empty;

    // Results
    int exit_code = 0;
    size_t selected = 0;
    size_t passed = 0;

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

    // Discover tests.

    prepare_env_vars();
    tests_dir = join_two_paths(root_dir, tests_root_rel);

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

        selected++;
        if (list_only) {
            puts(test_case->id.cstr);
            continue;
        }

        printf("RUN: %s\n", test_case->id.cstr);
        fflush(stdout);

        int test_exit_code = -1;
        if (test_case->kind == TEST_KIND_C) {
            test_exit_code = run_c_test(test_case);
        } else if (test_case->kind == TEST_KIND_EXECUTABLE) {
            test_exit_code = run_executable_test(test_case);
        } else {
            test_exit_code = 1;
        }

        if (test_exit_code == 0) {
            passed++;
            printf("PASS: %s\n", test_case->id.cstr);
        } else {
            printf("FAIL: %s\n", test_case->id.cstr);
            break;
        }
        fflush(stdout);
    }

    if (selected == 0) {
        if (!list_only) {
            fprintf(stderr, "error: no tests matched the requested filters\n");
            exit_code = 1;
        }
        goto cleanup;
    }

    if (!list_only) {
        printf("%zu/%zu tests passed\n", passed, selected);
        if (passed != selected)
            exit_code = 1;
    }

cleanup:
    str_free(tests_dir);
    string_array_free(&filters);
    test_case_array_free(&cases);
    test_file_array_free(&files);
    return exit_code;
}
