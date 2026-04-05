#include "util/string.h"

// Copy raw bytes into a new owning String while escaping non-printable bytes
// for diagnostics. Printable ASCII bytes are copied as-is, backslash and
// common control bytes use short C-style escapes, and other bytes use `\xHH`.
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

        str.cstr = str__insert_str_impl(str.cstr, &str.len, &str.capacity,
                                        str.len, escaped, escaped_len);
    }

    return str;
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

void str_array_free(StringArray *strings) {
    for (size_t i = 0; i < strings->size; ++i)
        str_free(strings->data[i]);
    arr_free(*strings);
}
