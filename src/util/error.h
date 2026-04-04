#ifndef UTIL_ERROR_H
#define UTIL_ERROR_H

#include <stdbool.h>

#if defined(__GNUC__) || defined(__clang__)
#define UTIL_NORETURN __attribute__((noreturn))
#else
#define UTIL_NORETURN
#endif

// Print a fatal error message and terminate the process with exit status 1.
// A literal `%errno` substring in `message` is replaced with strerror(errno).
UTIL_NORETURN void die(const char *message);

// Print a fatal error message that also includes strerror(errno), then
// terminate the process with exit status 1.
UTIL_NORETURN void die_errno(const char *message);

// Require condition to be true. On failure, terminate via die(message).
// A literal `%errno` substring in `message` is replaced with strerror(errno).
void require(bool condition, const char *message);

#endif
