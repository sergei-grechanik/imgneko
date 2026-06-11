// SPDX-License-Identifier: MIT-0

#ifndef UTIL_STRING_H
#define UTIL_STRING_H

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "util/array.h"

// Owning null-terminated byte string.
//
// `cstr` always points to a null-terminated buffer. When `capacity` is zero,
// the string is the special empty value `{ "", 0, 0 }`.
//
// Typical usage:
//     String path = str_from_cstr("foo");
//     str_append_cstr(path, "/bar");
//     str_free(path);
//
// It is not recommended to pass a String to a function as an input parameter,
// good old `const char *` is usually more appropriate for that. String is
// primarily intended for building up strings piece by piece, storing
// dynamically allocated strings in data structures, and returning owned strings
// from functions.
typedef struct String {
    char *cstr;
    size_t len;
    size_t capacity;
} String;

// Initialize an owning string to the empty-string special case.
#define str_empty                                                              \
    { "", 0, 0 }

// ASCII-only character predicates used in parser code that must not depend on
// locale-sensitive ctype behavior or signed-char promotion rules.
ARRLIB_INLINE bool str_char_is_ascii_lower(char ch) {
    return 'a' <= ch && ch <= 'z';
}

ARRLIB_INLINE bool str_char_is_ascii_upper(char ch) {
    return 'A' <= ch && ch <= 'Z';
}

ARRLIB_INLINE bool str_char_is_ascii_alpha(char ch) {
    return str_char_is_ascii_lower(ch) || str_char_is_ascii_upper(ch);
}

ARRLIB_INLINE bool str_char_is_ascii_digit(char ch) {
    return '0' <= ch && ch <= '9';
}

ARRLIB_INLINE int str_ascii_hex_digit_value(char ch) {
    if (str_char_is_ascii_digit(ch))
        return ch - '0';
    if ('a' <= ch && ch <= 'f')
        return ch - 'a' + 10;
    if ('A' <= ch && ch <= 'F')
        return ch - 'A' + 10;
    return -1;
}

ARRLIB_INLINE bool str_char_is_ascii_alnum(char ch) {
    return str_char_is_ascii_alpha(ch) || str_char_is_ascii_digit(ch);
}

ARRLIB_INLINE bool str_char_is_ascii_space(char ch) {
    return ch == ' ' || ('\t' <= ch && ch <= '\r');
}

// Get a pointer to the string's dynamically allocated null-terminated data, or
// NULL when the string is empty and no data is dynamically allocated.
ARRLIB_INLINE char *str__get_cstr_or_null(char *str, size_t len,
                                          size_t capacity) {
    (void)len;
    if (capacity == 0) {
        assert(len == 0);
        assert(str[0] == '\0');
        return NULL;
    }
    return str;
}

// Reserve at least new_capacity bytes, including the null terminator, and
// return the resulting string buffer.
ARRLIB_INLINE char *str__reserve_impl(char *cstr, size_t len, size_t *capacity,
                                      size_t new_capacity) {
    if (new_capacity <= *capacity)
        return cstr;
    cstr = str__get_cstr_or_null(cstr, len, *capacity);
    cstr = arr__reserve_impl(cstr, capacity, new_capacity, sizeof(char));
    if (len == 0)
        cstr[0] = '\0';
    return cstr;
}

// Resize a string, preserving null termination, zero-filling growth, and
// return the resulting string buffer.
ARRLIB_INLINE char *str__resize_impl(char *cstr, size_t *len, size_t *capacity,
                                     size_t new_len) {
    if (new_len <= *len) {
        // Shrinking never needs reallocation or zero-filling. Handling it
        // directly avoids size underflow in fortified libc warning analysis.
        if (*capacity != 0)
            cstr[new_len] = '\0';
        *len = new_len;
        return cstr;
    }

    size_t size_with_nul = *len + 1;

    cstr = str__get_cstr_or_null(cstr, *len, *capacity);
    cstr = arr__resize_impl(cstr, &size_with_nul, capacity, new_len + 1,
                            sizeof(char));
    cstr[new_len] = '\0';
    *len = size_with_nul - 1;
    return cstr;
}

// Insert a non-overlapping byte range and keep the resulting string
// null-terminated. When other_data is NULL, the inserted gap is left
// uninitialized for the caller to fill.
ARRLIB_INLINE char *str__insert_str_impl(char *cstr, size_t *len,
                                         size_t *capacity, size_t index,
                                         char const *other_data,
                                         size_t other_len) {
    assert(index <= *len);
    if (other_len == 0)
        return cstr;

    if (*capacity == 0) {
        cstr = str__reserve_impl(cstr, *len, capacity, other_len + 1);
        cstr[0] = '\0';
    }

    size_t size_with_nul = *len + 1;
    cstr = arr__insert_arr_impl(cstr, &size_with_nul, capacity, index,
                                other_data, other_len, sizeof(char));
    *len = size_with_nul - 1;
    return cstr;
}

// Copy raw string data into a new owning String. The caller frees the result
// with str_free.
ARRLIB_INLINE String str_from_data(char const *data, size_t len) {
    String str = str_empty;
    str.cstr =
        str__insert_str_impl(str.cstr, &str.len, &str.capacity, 0, data, len);
    return str;
}

// Copy a null-terminated C string into a new owning String. The caller frees
// the result with str_free.
ARRLIB_INLINE String str_from_cstr(char const *cstr) {
    return str_from_data(cstr, strlen(cstr));
}

// Copy raw bytes into a new owning String while escaping non-printable bytes
// for diagnostics. Printable ASCII bytes are copied as-is, backslash and
// common control bytes use short C-style escapes, and other bytes use `\xHH`.
// The caller frees the result with str_free.
String str_from_escaped_bytes(char const *data, size_t len);

// Copy an owning String into a new owning String. The caller frees the result
// with str_free.
ARRLIB_INLINE String copy_str(String str) {
    return str_from_data(str.cstr, str.len);
}

// Free a String's owned storage and reset it to str_empty.
#define str_free(str)                                                          \
    do {                                                                       \
        if ((str).capacity != 0)                                               \
            free((str).cstr);                                                  \
        (str).cstr = "";                                                       \
        (str).len = 0;                                                         \
        (str).capacity = 0;                                                    \
    } while (0)

// Clear a String and keep its current allocation.
#define str_clear(str)                                                         \
    do {                                                                       \
        if ((str).capacity != 0)                                               \
            (str).cstr[0] = '\0';                                              \
        (str).len = 0;                                                         \
    } while (0)

// Reserve at least new_capacity bytes, including the null terminator.
#define str_reserve(str, new_capacity)                                         \
    do {                                                                       \
        (str).cstr = str__reserve_impl((str).cstr, (str).len, &(str).capacity, \
                                       (new_capacity));                        \
    } while (0)

// Truncate a String to new_size bytes.
#define str_truncate(str, new_size)                                            \
    do {                                                                       \
        assert((new_size) <= (str).len);                                       \
        (str).cstr = str__resize_impl((str).cstr, &(str).len, &(str).capacity, \
                                      (new_size));                             \
    } while (0)

// Append one character to the back of a String.
#define str_push(str, c)                                                       \
    do {                                                                       \
        size_t str__old_len = (str).len;                                       \
        (str).cstr = str__resize_impl((str).cstr, &(str).len, &(str).capacity, \
                                      str__old_len + 1);                       \
        (str).cstr[str__old_len] = (c);                                        \
    } while (0)

// Drop n bytes from the back of a String.
#define str_drop_back(str, n)                                                  \
    do {                                                                       \
        size_t str__n = (n);                                                   \
        assert(str__n <= (str).len);                                           \
        (str).len -= str__n;                                                   \
        if ((str).capacity != 0)                                               \
            (str).cstr[(str).len] = '\0';                                      \
    } while (0)

// Drop n bytes from the front of a String.
#define str_drop_front(str, n)                                                 \
    do {                                                                       \
        size_t str__n = (n);                                                   \
        assert(str__n <= (str).len);                                           \
        if ((str).capacity != 0 && str__n != 0)                                \
            memmove((str).cstr, (str).cstr + str__n, (str).len - str__n + 1);  \
        (str).len -= str__n;                                                   \
    } while (0)

// Keep only the last n bytes of a String.
#define str_take_back(str, n)                                                  \
    do {                                                                       \
        size_t str__n = (n);                                                   \
        assert(str__n <= (str).len);                                           \
        if ((str).capacity != 0 && str__n != 0)                                \
            memmove((str).cstr, (str).cstr + (str).len - str__n, str__n);      \
        (str).len = str__n;                                                    \
        if ((str).capacity != 0)                                               \
            (str).cstr[(str).len] = '\0';                                      \
    } while (0)

// Keep only the first n bytes of a String.
#define str_take_front(str, n)                                                 \
    do {                                                                       \
        size_t str__n = (n);                                                   \
        assert(str__n <= (str).len);                                           \
        (str).len = str__n;                                                    \
        if ((str).capacity != 0)                                               \
            (str).cstr[(str).len] = '\0';                                      \
    } while (0)

// Replace a String with the half-open slice [start_index, end_index).
#define str_slice(str, start_index, end_index)                                 \
    do {                                                                       \
        ArrSlice str__slice =                                                  \
            arr__slice_bounds((str).len, (start_index), (end_index));          \
        if ((str).capacity != 0 && str__slice.start != 0)                      \
            memmove((str).cstr, (str).cstr + str__slice.start,                 \
                    str__slice.end - str__slice.start);                        \
        (str).len = str__slice.end - str__slice.start;                         \
        if ((str).capacity != 0)                                               \
            (str).cstr[(str).len] = '\0';                                      \
    } while (0)

// Append another owning String to the back of a String.
#define str_append_str(str, other)                                             \
    do {                                                                       \
        (str).cstr =                                                           \
            str__insert_str_impl((str).cstr, &(str).len, &(str).capacity,      \
                                 (str).len, (other).cstr, (other).len);        \
    } while (0)

// Append raw string data to the back of a String.
#define str_append_data(str, other_data, other_len)                            \
    do {                                                                       \
        (str).cstr =                                                           \
            str__insert_str_impl((str).cstr, &(str).len, &(str).capacity,      \
                                 (str).len, (other_data), (other_len));        \
    } while (0)

// Append a null-terminated C string to the back of a String.
#define str_append_cstr(str, other_cstr)                                       \
    do {                                                                       \
        char const *str__other_cstr = (other_cstr);                            \
        (str).cstr = str__insert_str_impl(                                     \
            (str).cstr, &(str).len, &(str).capacity, (str).len,                \
            str__other_cstr, strlen(str__other_cstr));                         \
    } while (0)

// Insert one character into a String at index.
#define str_insert(str, index, c)                                              \
    do {                                                                       \
        size_t str__index = (index);                                           \
        (str).cstr = str__insert_str_impl(                                     \
            (str).cstr, &(str).len, &(str).capacity, str__index, NULL, 1);     \
        (str).cstr[str__index] = (c);                                          \
    } while (0)

// Insert another owning String into a String at index.
#define str_insert_str(str, index, other)                                      \
    do {                                                                       \
        (str).cstr =                                                           \
            str__insert_str_impl((str).cstr, &(str).len, &(str).capacity,      \
                                 (index), (other).cstr, (other).len);          \
    } while (0)

// Insert raw string data into a String at index.
#define str_insert_data(str, index, other_data, other_len)                     \
    do {                                                                       \
        (str).cstr =                                                           \
            str__insert_str_impl((str).cstr, &(str).len, &(str).capacity,      \
                                 (index), (other_data), (other_len));          \
    } while (0)

// Insert a null-terminated C string into a String at index.
#define str_insert_cstr(str, index, other_cstr)                                \
    do {                                                                       \
        char const *str__other_cstr = (other_cstr);                            \
        (str).cstr = str__insert_str_impl(                                     \
            (str).cstr, &(str).len, &(str).capacity, (index), str__other_cstr, \
            strlen(str__other_cstr));                                          \
    } while (0)

// Return true when cstr begins with prefix.
ARRLIB_INLINE bool starts_with_cstr(char const *cstr, char const *prefix) {
    while (*prefix != '\0') {
        if (*cstr != *prefix)
            return false;
        ++cstr;
        ++prefix;
    }

    return true;
}

// Return true when cstr ends with suffix.
ARRLIB_INLINE bool ends_with_cstr(char const *cstr, char const *suffix) {
    size_t cstr_len = strlen(cstr);
    size_t suffix_len = strlen(suffix);

    if (suffix_len > cstr_len)
        return false;

    return strcmp(cstr + cstr_len - suffix_len, suffix) == 0;
}

// Trim trailing characters from a mutable C string while any suffix byte is
// present in `trim_chars`.
void str_trim_trailing_chars_cstr(char *text, const char *trim_chars);

// Trim trailing characters from an owned String while any suffix byte is
// present in `trim_chars`.
void str_trim_trailing_chars(String *text, const char *trim_chars);

// Append one single-quoted shell word so a POSIX shell parses the original
// bytes literally. The existing contents of `out` are preserved.
void str_append_shell_quoted_word(String *out, const char *text);

// Append one double-quoted C-style string literal fragment that preserves the
// original bytes using common escapes where possible.
void str_append_c_quoted_data(String *out, const char *data, size_t len);

// Append one null-terminated C string as a double-quoted C-style literal
// fragment.
ARRLIB_INLINE void str_append_c_quoted_cstr(String *out, const char *text) {
    str_append_c_quoted_data(out, text, strlen(text));
}

DEFINE_ARRAY_TYPE(StringArray, String)

// Free a StringArray and each owned string it contains.
void str_array_free(StringArray *strings);

#endif
