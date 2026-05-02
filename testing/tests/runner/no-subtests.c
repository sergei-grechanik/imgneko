// SPDX-License-Identifier: MIT-0

#include <stdio.h>
#include <string.h>

// Exercise the C-test path where `--list` reports no named subtests, so the
// runner falls back to invoking the binary with `--all`.
static int list_subtests(void) { return 0; }

// Run the implicit test body used for the `--all` fallback.
static int run_all(void) {
    puts("no-subtests ran");
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--list") == 0)
        return list_subtests();

    if (argc == 2 && strcmp(argv[1], "--all") == 0)
        return run_all();

    fprintf(stderr, "unexpected arguments\n");
    return 1;
}
