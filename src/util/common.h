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

#endif
