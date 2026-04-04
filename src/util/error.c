#include "util/error.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Write message to stderr, expanding `%errno` to strerror(errno) inline.
static void print_error_message(const char *message) {
    int saved_errno = errno;
    const char *cursor = message;

    while (*cursor != '\0') {
        const char *placeholder = strstr(cursor, "%errno");

        if (placeholder == NULL) {
            fputs(cursor, stderr);
            return;
        }

        fwrite(cursor, 1, (size_t)(placeholder - cursor), stderr);
        fputs(strerror(saved_errno), stderr);
        cursor = placeholder + strlen("%errno");
    }
}

UTIL_NORETURN void die(const char *message) {
    fputs("error: ", stderr);
    print_error_message(message);
    fputc('\n', stderr);
    exit(1);
}

UTIL_NORETURN void die_errno(const char *message) {
    fputs("error: ", stderr);
    fputs(message, stderr);
    fputs(": ", stderr);
    fputs(strerror(errno), stderr);
    fputc('\n', stderr);
    exit(1);
}

void require(bool condition, const char *message) {
    if (!condition)
        die(message);
}
