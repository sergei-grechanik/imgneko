// SPDX-License-Identifier: MIT-0

#include "util/string.h"

StrSpan str_span_trim(StrSpan span) {
    while (span.len != 0 && str_char_is_ascii_space(span.data[0])) {
        ++span.data;
        --span.len;
    }

    while (span.len != 0 && str_char_is_ascii_space(span.data[span.len - 1])) {
        --span.len;
    }

    return span;
}

void str_trim_trailing_chars_cstr(char *text, const char *trim_chars) {
    size_t len = strlen(text);

    while (len > 0 && strchr(trim_chars, text[len - 1]) != NULL) {
        text[len - 1] = '\0';
        len--;
    }
}

void str_trim_trailing_chars(String *text, const char *trim_chars) {
    while (text->len > 0 &&
           strchr(trim_chars, text->cstr[text->len - 1]) != NULL) {
        str_truncate(*text, text->len - 1);
    }
}

void str_append_shell_quoted_word(String *out, const char *text) {
    str_push(*out, '\'');
    for (size_t i = 0; text[i] != '\0'; ++i) {
        if (text[i] == '\'')
            str_append_cstr(*out, "'\\''");
        else
            str_push(*out, text[i]);
    }
    str_push(*out, '\'');
}

void str_append_c_quoted_data(String *out, const char *data, size_t len) {
    static char const hex_digits[] = "0123456789abcdef";

    str_push(*out, '"');
    for (size_t i = 0; i < len; ++i) {
        unsigned char byte = (unsigned char)data[i];
        char escaped[4];
        size_t escaped_len = 0;

        switch (byte) {
        case '"':
            escaped[0] = '\\';
            escaped[1] = '"';
            escaped_len = 2;
            break;
        case '\\':
            escaped[0] = '\\';
            escaped[1] = '\\';
            escaped_len = 2;
            break;
        case '\a':
            escaped[0] = '\\';
            escaped[1] = 'a';
            escaped_len = 2;
            break;
        case '\b':
            escaped[0] = '\\';
            escaped[1] = 'b';
            escaped_len = 2;
            break;
        case '\f':
            escaped[0] = '\\';
            escaped[1] = 'f';
            escaped_len = 2;
            break;
        case '\n':
            escaped[0] = '\\';
            escaped[1] = 'n';
            escaped_len = 2;
            break;
        case '\r':
            escaped[0] = '\\';
            escaped[1] = 'r';
            escaped_len = 2;
            break;
        case '\t':
            escaped[0] = '\\';
            escaped[1] = 't';
            escaped_len = 2;
            break;
        case '\v':
            escaped[0] = '\\';
            escaped[1] = 'v';
            escaped_len = 2;
            break;
        default:
            if (0x20 <= byte && byte <= 0x7e) {
                escaped[0] = (char)byte;
                escaped_len = 1;
                break;
            }

            escaped[0] = '\\';
            escaped[1] = 'x';
            escaped[2] = hex_digits[byte >> 4];
            escaped[3] = hex_digits[byte & 0x0f];
            escaped_len = 4;
            break;
        }

        str_append_data(*out, escaped, escaped_len);
    }
    str_push(*out, '"');
}

String str_from_escaped_bytes(char const *data, size_t len) {
    static char const hex_digits[] = "0123456789abcdef";
    String str = str_empty;

    for (size_t i = 0; i < len; ++i) {
        unsigned char byte = (unsigned char)data[i];
        char escaped[4];
        size_t escaped_len = 0;

        switch (byte) {
        case '\\':
            escaped[0] = '\\';
            escaped[1] = '\\';
            escaped_len = 2;
            break;
        case '\a':
            escaped[0] = '\\';
            escaped[1] = 'a';
            escaped_len = 2;
            break;
        case '\b':
            escaped[0] = '\\';
            escaped[1] = 'b';
            escaped_len = 2;
            break;
        case '\f':
            escaped[0] = '\\';
            escaped[1] = 'f';
            escaped_len = 2;
            break;
        case '\n':
            escaped[0] = '\\';
            escaped[1] = 'n';
            escaped_len = 2;
            break;
        case '\r':
            escaped[0] = '\\';
            escaped[1] = 'r';
            escaped_len = 2;
            break;
        case '\t':
            escaped[0] = '\\';
            escaped[1] = 't';
            escaped_len = 2;
            break;
        case '\v':
            escaped[0] = '\\';
            escaped[1] = 'v';
            escaped_len = 2;
            break;
        default:
            if (0x20 <= byte && byte <= 0x7e) {
                escaped[0] = (char)byte;
                escaped_len = 1;
                break;
            }

            escaped[0] = '\\';
            escaped[1] = 'x';
            escaped[2] = hex_digits[byte >> 4];
            escaped[3] = hex_digits[byte & 0x0f];
            escaped_len = 4;
            break;
        }

        str_append_data(str, escaped, escaped_len);
    }

    return str;
}

void str_sanitize_for_diagnostic_impl(char *out, size_t out_size,
                                      const char *data, size_t len) {
    static const char hex_digits[] = "0123456789ABCDEF";
    size_t capacity;
    size_t out_len = 0;
    size_t i = 0;

    if (out_size == 0)
        return;

    capacity = out_size - 1;
    if (capacity == 0) {
        out[0] = '\0';
        return;
    }

    for (; i < len; ++i) {
        unsigned char byte = (unsigned char)data[i];
        char sanitized[5];
        size_t sanitized_len = 0;

        switch (byte) {
        case '\n':
            memcpy(sanitized, "<LF>", 4);
            sanitized_len = 4;
            break;
        case '\r':
            memcpy(sanitized, "<CR>", 4);
            sanitized_len = 4;
            break;
        case '\t':
            memcpy(sanitized, "<TAB>", 5);
            sanitized_len = 5;
            break;
        case 0x1b:
            memcpy(sanitized, "<ESC>", 5);
            sanitized_len = 5;
            break;
        default:
            if (byte >= 0x20 && byte != 0x7f) {
                sanitized[0] = (char)byte;
                sanitized_len = 1;
                break;
            }
            sanitized[0] = '<';
            sanitized[1] = hex_digits[byte >> 4];
            sanitized[2] = hex_digits[byte & 0x0f];
            sanitized[3] = '>';
            sanitized_len = 4;
            break;
        }

        if (out_len + sanitized_len > capacity)
            break;

        memcpy(out + out_len, sanitized, sanitized_len);
        out_len += sanitized_len;
    }

    if (i < len && capacity >= 3) {
        // Keep enough room for the ellipsis marker.
        while (out_len > 0 && out_len + 3 > capacity)
            --out_len;
        memcpy(out + out_len, "...", 3);
        out_len += 3;
    }

    out[out_len] = '\0';
}

void str_array_free(StringArray *strings) {
    for (size_t i = 0; i < strings->size; ++i)
        str_free(strings->data[i]);
    arr_free(*strings);
}
