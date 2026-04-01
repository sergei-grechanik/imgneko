#include <stdio.h>
#include <string.h>

#include "test_main.h"
#include "util/string.h"

#define STR(text) (text), (sizeof(text) - 1)

// Print one failure message for a subtest and return a failing status code.
static int fail_message(const char *subtest, const char *message) {
    fprintf(stderr, "%s: %s\n", subtest, message);
    return 1;
}

// Compare two strings for exact equality, including the NUL terminator.
static int expect_string_eq(const char *subtest, const char *actual_data,
                            size_t actual_len, const char *expected_data,
                            size_t expected_len) {
    if (actual_len != expected_len) {
        fprintf(stderr, "%s: expected length %zu, got %zu\n", subtest,
                expected_len, actual_len);
        return 1;
    }

    if (memcmp(actual_data, expected_data, actual_len + 1) != 0) {
        fprintf(stderr, "%s: string contents differ\n", subtest);
        return 1;
    }

    return 0;
}

// Check the observable empty-string invariants for both allocated and special
// empty states.
static int expect_empty_string(const char *subtest, String string,
                               bool expect_allocated) {
    if (string.len != 0)
        return fail_message(subtest, "expected an empty string");

    if (string.cstr[0] != '\0')
        return fail_message(subtest, "empty string is not null-terminated");

    if (expect_allocated) {
        if (string.capacity == 0)
            return fail_message(subtest, "expected an allocated empty string");
    } else {
        if (string.capacity != 0)
            return fail_message(
                subtest, "expected the special non-allocated empty string");
    }

    return 0;
}

static int test_from_cstr_and_copy(TestContext *ctx) {
    const char *name = ctx->test_name;
    String string = str_from_cstr("hello");
    const char *middle = string.cstr + 1;
    size_t middle_len = 3;
    String materialized = str_empty;
    String copied = str_empty;
    int status = 0;

    materialized = str_from_data(middle, middle_len);
    copied = copy_str(string);

    string.cstr[1] = 'A';
    string.cstr[4] = 'O';

    status =
        expect_string_eq(name, materialized.cstr, materialized.len, STR("ell"));
    if (status != 0)
        goto cleanup;

    status = expect_string_eq(name, copied.cstr, copied.len, STR("hello"));

cleanup:
    str_free(copied);
    str_free(materialized);
    str_free(string);
    return status;
}

static int test_empty_and_reserve(TestContext *ctx) {
    const char *name = ctx->test_name;
    String string = str_empty;
    int status = 0;

    if (string.len != 0 || string.capacity != 0 || string.cstr[0] != '\0') {
        status = fail_message(name, "str_empty is not initialized correctly");
        goto cleanup;
    }

    str_append_cstr(string, "hi");
    status = expect_string_eq(name, string.cstr, string.len, STR("hi"));
    if (status != 0)
        goto cleanup;

    str_clear(string);
    if (string.len != 0 || string.cstr[0] != '\0') {
        status = fail_message(name, "str_clear did not reset the string");
        goto cleanup;
    }

    str_reserve(string, 8);
    if (string.capacity < 8) {
        status = fail_message(name, "str_reserve did not grow capacity");
        goto cleanup;
    }

    str_insert(string, 0, 'x');
    status = expect_string_eq(name, string.cstr, string.len, STR("x"));

cleanup:
    str_free(string);
    return status;
}

static int test_empty_states(TestContext *ctx) {
    const char *name = ctx->test_name;
    String special_empty = str_from_data("", 0);
    String empty_cstr = str_from_cstr("");
    String copied_empty = copy_str((String)str_empty);
    String allocated_empty = str_from_cstr("abc");
    String reserved_empty = str_empty;
    String append_empty = str_empty;
    String insert_empty = str_empty;
    int status = 0;

    status = expect_empty_string(name, special_empty, false);
    if (status != 0)
        goto cleanup;

    status = expect_empty_string(name, empty_cstr, false);
    if (status != 0)
        goto cleanup;

    status = expect_empty_string(name, copied_empty, false);
    if (status != 0)
        goto cleanup;

    str_clear(allocated_empty);
    status = expect_empty_string(name, allocated_empty, true);
    if (status != 0)
        goto cleanup;

    str_reserve(reserved_empty, 4);
    status = expect_empty_string(name, reserved_empty, true);
    if (status != 0)
        goto cleanup;

    if (reserved_empty.capacity < 4) {
        status =
            fail_message(name, "reserve did not allocate requested capacity");
        goto cleanup;
    }

    str_append_str(append_empty, allocated_empty);
    status = expect_empty_string(name, append_empty, false);
    if (status != 0)
        goto cleanup;

    str_append_data(append_empty, "", 0);
    status = expect_empty_string(name, append_empty, false);
    if (status != 0)
        goto cleanup;

    str_append_cstr(append_empty, "");
    status = expect_empty_string(name, append_empty, false);
    if (status != 0)
        goto cleanup;

    str_insert_str(insert_empty, 0, allocated_empty);
    status = expect_empty_string(name, insert_empty, false);
    if (status != 0)
        goto cleanup;

    str_insert_data(insert_empty, 0, "", 0);
    status = expect_empty_string(name, insert_empty, false);
    if (status != 0)
        goto cleanup;

    str_insert_cstr(insert_empty, 0, "");
    status = expect_empty_string(name, insert_empty, false);

cleanup:
    str_free(insert_empty);
    str_free(append_empty);
    str_free(reserved_empty);
    str_free(allocated_empty);
    str_free(copied_empty);
    str_free(empty_cstr);
    str_free(special_empty);
    return status;
}

static int test_in_place_slice_ops(TestContext *ctx) {
    const char *name = ctx->test_name;
    String front = str_from_cstr("abcdef");
    String back = str_from_cstr("abcdef");
    String middle = str_from_cstr("abcdef");
    String drop_to_empty = str_from_cstr("abc");
    String drop_back_empty = str_from_cstr("abc");
    String take_back_empty = str_from_cstr("abc");
    String take_front_empty = str_from_cstr("abc");
    String slice_to_empty = str_from_cstr("abc");
    int status = 0;

    str_drop_front(front, 1);
    str_drop_back(front, 1);
    str_take_front(front, 3);
    status = expect_string_eq(name, front.cstr, front.len, STR("bcd"));
    if (status != 0)
        goto cleanup;

    str_take_back(back, 2);
    status = expect_string_eq(name, back.cstr, back.len, STR("ef"));
    if (status != 0)
        goto cleanup;

    str_slice(middle, 1, -1);
    status = expect_string_eq(name, middle.cstr, middle.len, STR("bcde"));
    if (status != 0)
        goto cleanup;

    str_drop_front(drop_to_empty, 3);
    status = expect_empty_string(name, drop_to_empty, true);
    if (status != 0)
        goto cleanup;

    str_drop_back(drop_back_empty, 3);
    status = expect_empty_string(name, drop_back_empty, true);
    if (status != 0)
        goto cleanup;

    str_take_back(take_back_empty, 0);
    status = expect_empty_string(name, take_back_empty, true);
    if (status != 0)
        goto cleanup;

    str_take_front(take_front_empty, 0);
    status = expect_empty_string(name, take_front_empty, true);
    if (status != 0)
        goto cleanup;

    str_slice(slice_to_empty, 1, 1);
    status = expect_empty_string(name, slice_to_empty, true);

cleanup:
    str_free(slice_to_empty);
    str_free(take_front_empty);
    str_free(take_back_empty);
    str_free(drop_back_empty);
    str_free(drop_to_empty);
    str_free(middle);
    str_free(back);
    str_free(front);
    return status;
}

static int test_truncate_and_push(TestContext *ctx) {
    const char *name = ctx->test_name;
    String string = str_from_cstr("abcde");
    String cleared = str_from_cstr("xy");
    int status = 0;

    str_truncate(string, 2);
    status = expect_string_eq(name, string.cstr, string.len, STR("ab"));
    if (status != 0)
        goto cleanup;

    str_push(string, '!');
    status = expect_string_eq(name, string.cstr, string.len, STR("ab!"));
    if (status != 0)
        goto cleanup;

    str_truncate(cleared, 0);
    status = expect_empty_string(name, cleared, true);
    if (status != 0)
        goto cleanup;

    str_push(cleared, 'z');
    if (cleared.capacity == 0) {
        status = fail_message(name,
                              "push lost allocation after truncating to empty");
        goto cleanup;
    }

    status = expect_string_eq(name, cleared.cstr, cleared.len, STR("z"));

cleanup:
    str_free(cleared);
    str_free(string);
    return status;
}

static int test_append_and_insert(TestContext *ctx) {
    const char *name = ctx->test_name;
    String string = str_from_cstr("ad");
    String tail = str_from_cstr("!");
    String middle = str_from_cstr("XY");
    int status = 0;

    str_insert(string, 1, 'b');
    str_insert(string, 2, 'c');
    status = expect_string_eq(name, string.cstr, string.len, STR("abcd"));
    if (status != 0)
        goto cleanup;

    str_insert_str(string, 2, middle);
    status = expect_string_eq(name, string.cstr, string.len, STR("abXYcd"));
    if (status != 0)
        goto cleanup;

    str_insert_data(string, 6, "?", 1);
    status = expect_string_eq(name, string.cstr, string.len, STR("abXYcd?"));
    if (status != 0)
        goto cleanup;

    str_insert_cstr(string, 7, "!");
    status = expect_string_eq(name, string.cstr, string.len, STR("abXYcd?!"));
    if (status != 0)
        goto cleanup;

    str_append_str(string, tail);
    status = expect_string_eq(name, string.cstr, string.len, STR("abXYcd?!!"));
    if (status != 0)
        goto cleanup;

    str_append_data(string, "-ref", 4);
    status =
        expect_string_eq(name, string.cstr, string.len, STR("abXYcd?!!-ref"));
    if (status != 0)
        goto cleanup;

    str_append_cstr(string, "-cstr");
    status = expect_string_eq(name, string.cstr, string.len,
                              STR("abXYcd?!!-ref-cstr"));

cleanup:
    str_free(middle);
    str_free(tail);
    str_free(string);
    return status;
}

static int test_predicates(TestContext *ctx) {
    const char *name = ctx->test_name;

    if (!starts_with_cstr("abcdef", "abc"))
        return fail_message(name, "starts_with_cstr failed");
    if (!starts_with_cstr("abcdef", ""))
        return fail_message(name, "starts_with_cstr rejected empty prefix");
    if (starts_with_cstr("abcdef", "bcd"))
        return fail_message(name, "starts_with_cstr matched the middle");
    if (!ends_with_cstr("abcdef", "def"))
        return fail_message(name, "ends_with_cstr failed");
    if (!ends_with_cstr("abcdef", ""))
        return fail_message(name, "ends_with_cstr rejected empty suffix");
    if (ends_with_cstr("abcdef", "cde"))
        return fail_message(name, "ends_with_cstr matched the middle");
    if (ends_with_cstr("abc", "abcdef"))
        return fail_message(name, "ends_with_cstr matched a longer suffix");

    return 0;
}

static int test_escape_bytes(TestContext *ctx) {
    const char *name = ctx->test_name;
    const char escaped_input[] = {'A',  '\n',       '\t',
                                  '\\', (char)0x01, (char)0xff};
    String escaped = str_empty;
    String empty = str_empty;
    int status = 0;

    escaped = str_from_escaped_bytes(escaped_input, sizeof(escaped_input));
    status = expect_string_eq(name, escaped.cstr, escaped.len,
                              STR("A\\n\\t\\\\\\x01\\xff"));
    if (status != 0)
        goto cleanup;

    empty = str_from_escaped_bytes("", 0);
    status = expect_empty_string(name, empty, false);

cleanup:
    str_free(empty);
    str_free(escaped);
    return status;
}

static int test_trim_trailing_chars(TestContext *ctx) {
    const char *name = ctx->test_name;
    char cstr_line[] = "alpha\r\n";
    char cstr_custom[] = "beta!!!";
    String string_line = str_from_cstr("beta\n");
    String string_custom = str_from_cstr("gammaxyz");
    String untouched = str_from_cstr("delta");
    int status = 0;

    str_trim_trailing_chars_cstr(cstr_line, "\r\n");
    status = expect_string_eq(name, cstr_line, strlen(cstr_line), STR("alpha"));
    if (status != 0)
        goto cleanup;

    str_trim_trailing_chars_cstr(cstr_custom, "!");
    status =
        expect_string_eq(name, cstr_custom, strlen(cstr_custom), STR("beta"));
    if (status != 0)
        goto cleanup;

    str_trim_trailing_chars(&string_line, "\r\n");
    status =
        expect_string_eq(name, string_line.cstr, string_line.len, STR("beta"));
    if (status != 0)
        goto cleanup;

    str_trim_trailing_chars(&string_custom, "zyx");
    status = expect_string_eq(name, string_custom.cstr, string_custom.len,
                              STR("gamma"));
    if (status != 0)
        goto cleanup;

    str_trim_trailing_chars(&untouched, "\r\n");
    status =
        expect_string_eq(name, untouched.cstr, untouched.len, STR("delta"));

cleanup:
    str_free(untouched);
    str_free(string_custom);
    str_free(string_line);
    return status;
}

int main(int argc, char **argv) {
    const Subtest subtests[] = {
        PREFIXED_TEST(test_from_cstr_and_copy),
        PREFIXED_TEST(test_empty_and_reserve),
        PREFIXED_TEST(test_empty_states),
        PREFIXED_TEST(test_in_place_slice_ops),
        PREFIXED_TEST(test_truncate_and_push),
        PREFIXED_TEST(test_append_and_insert),
        PREFIXED_TEST(test_predicates),
        PREFIXED_TEST(test_escape_bytes),
        PREFIXED_TEST(test_trim_trailing_chars),
    };

    return run_subtests(argc, argv, subtests, ARRAY_SIZE(subtests));
}
