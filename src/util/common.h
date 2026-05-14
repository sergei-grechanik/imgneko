// SPDX-License-Identifier: MIT-0

#ifndef UTIL_COMMON_H
#define UTIL_COMMON_H

// Mark header-only helpers as inline and suppress unused warnings when a
// translation unit includes a helper but does not use every function.
#if defined(__GNUC__) || defined(__clang__)
#define UTIL_INLINE static inline __attribute__((unused))
#else
#define UTIL_INLINE static inline
#endif

// Return the number of elements in a fixed-size C array.
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

// Return the smaller of two values. Arguments must not have side effects.
#define MIN(a, b) ((a) < (b) ? (a) : (b))

// Return the larger of two values. Arguments must not have side effects.
#define MAX(a, b) ((a) > (b) ? (a) : (b))

// Expand one macro argument and stringify the final token sequence.
#define UTIL_STRINGIFY_IMPL(value) #value
#define UTIL_STRINGIFY(value) UTIL_STRINGIFY_IMPL(value)

#endif
