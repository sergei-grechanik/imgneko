// SPDX-License-Identifier: MIT-0

#include <stdio.h>
#include <string.h>

// Emit intentionally padded `--list` lines so the runner's in-place trimming
// and trailing-marker parsing paths are exercised by normal discovery.
static int list_subtests(void) {
    puts("  plain_spaced  ");
    puts("");
    puts("  marked_disabled DISABLED  ");
    puts("  marked_xfail XFAIL  ");
    return 0;
}

// Run one named subtest listed above.
static int run_named_subtest(const char *name) {
    if (strcmp(name, "plain_spaced") == 0) {
        puts("plain spaced subtest ran");
        return 0;
    }

    if (strcmp(name, "marked_xfail") == 0) {
        fputs("intentional expected failure\n", stderr);
        return 1;
    }

    if (strcmp(name, "marked_disabled") == 0) {
        fputs("disabled subtest ran unexpectedly\n", stderr);
        return 1;
    }

    fprintf(stderr, "unknown subtest: %s\n", name);
    return 1;
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--list") == 0)
        return list_subtests();

    if (argc == 2)
        return run_named_subtest(argv[1]);

    fprintf(stderr, "unexpected arguments\n");
    return 1;
}
