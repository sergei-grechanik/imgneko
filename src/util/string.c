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
