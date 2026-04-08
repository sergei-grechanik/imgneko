#include <stdio.h>
#include <string.h>

#include "test_main.h"
#include "util/array.h"
#include "util/common.h"

DEFINE_ARRAY_TYPE(IntArray, int)

#define INTS(...)                                                              \
    ((int const[]){__VA_ARGS__}),                                              \
        (sizeof((int const[]){__VA_ARGS__}) / sizeof(int))

// Print one failure message for a subtest and return a failing status code.
static int fail_message(const char *subtest, const char *message) {
    fprintf(stderr, "%s: %s\n", subtest, message);
    return 1;
}

// Compare two integer arrays for exact equality.
static int expect_array_eq(const char *subtest, const int *actual_data,
                           size_t actual_size, const int *expected_data,
                           size_t expected_size) {
    if (actual_size != expected_size) {
        fprintf(stderr, "%s: expected size %zu, got %zu\n", subtest,
                expected_size, actual_size);
        return 1;
    }

    for (size_t i = 0; i < expected_size; ++i) {
        if (actual_data[i] != expected_data[i]) {
            fprintf(stderr, "%s: element %zu expected %d, got %d\n", subtest, i,
                    expected_data[i], actual_data[i]);
            return 1;
        }
    }

    return 0;
}

static int test_growth_and_resize(TestContext *ctx) {
    const char *name = ctx->test_name;
    IntArray array = arr_empty;
    int *reserved_data = NULL;
    size_t reserved_capacity = 0;
    int status = 0;

    arr_push(array, 10);
    arr_push(array, 20);
    arr_reserve(array, 8);
    arr_resize(array, 5);

    if (array.capacity < 8) {
        status = fail_message(name, "reserve did not grow capacity");
        goto cleanup;
    }

    // Reserving the current capacity should be a no-op and keep the buffer.
    reserved_data = array.data;
    reserved_capacity = array.capacity;
    arr_reserve(array, reserved_capacity);
    if (array.data != reserved_data || array.capacity != reserved_capacity) {
        status = fail_message(name, "reserve changed an already-large buffer");
        goto cleanup;
    }

    if (array.data[2] != 0 || array.data[3] != 0 || array.data[4] != 0) {
        status = fail_message(name, "resize did not zero new elements");
        goto cleanup;
    }

    array.data[2] = 30;
    array.data[3] = 40;
    array.data[4] = 50;

    status =
        expect_array_eq(name, array.data, array.size, INTS(10, 20, 30, 40, 50));
    if (status != 0)
        goto cleanup;

    arr_clear(array);
    if (array.size != 0 || array.capacity < 8) {
        status = fail_message(name, "clear changed more than size");
        goto cleanup;
    }

cleanup:
    arr_free(array);
    return status;
}

static int test_copy_from_data(TestContext *ctx) {
    const char *name = ctx->test_name;
    IntArray array = arr_from_c(IntArray, ((int const[]){1, 2, 3, 4, 5}));
    const int *middle = array.data + 1;
    size_t middle_size = 3;
    const int *tail = array.data + 3;
    size_t tail_size = 2;
    IntArray materialized = arr_empty;
    IntArray copied = arr_empty;
    int status = 0;

    status = expect_array_eq(name, middle, middle_size, INTS(2, 3, 4));
    if (status != 0)
        goto cleanup;

    status = expect_array_eq(name, tail, tail_size, INTS(4, 5));
    if (status != 0)
        goto cleanup;

    materialized = make_IntArray(middle, middle_size);
    copied = copy_IntArray(array);

    array.data[1] = 99;
    array.data[4] = 77;

    status = expect_array_eq(name, materialized.data, materialized.size,
                             INTS(2, 3, 4));
    if (status != 0)
        goto cleanup;

    status =
        expect_array_eq(name, copied.data, copied.size, INTS(1, 2, 3, 4, 5));

cleanup:
    arr_free(copied);
    arr_free(materialized);
    arr_free(array);
    return status;
}

static int test_in_place_slice_ops(TestContext *ctx) {
    const char *name = ctx->test_name;
    IntArray front = arr_from_c(IntArray, ((int const[]){1, 2, 3, 4, 5, 6}));
    IntArray back = arr_from_c(IntArray, ((int const[]){1, 2, 3, 4, 5, 6}));
    IntArray middle = arr_from_c(IntArray, ((int const[]){1, 2, 3, 4, 5, 6}));
    int status = 0;

    arr_drop_front(front, 1);
    arr_drop_back(front, 1);
    arr_take_front(front, 3);
    status = expect_array_eq(name, front.data, front.size, INTS(2, 3, 4));
    if (status != 0)
        goto cleanup;

    arr_take_back(back, 2);
    status = expect_array_eq(name, back.data, back.size, INTS(5, 6));
    if (status != 0)
        goto cleanup;

    arr_slice(middle, 1, -1);
    status = expect_array_eq(name, middle.data, middle.size, INTS(2, 3, 4, 5));

cleanup:
    arr_free(middle);
    arr_free(back);
    arr_free(front);
    return status;
}

static int test_append_and_insert(TestContext *ctx) {
    const char *name = ctx->test_name;
    IntArray array = arr_from_c(IntArray, ((int const[]){1, 4}));
    IntArray middle = arr_from_c(IntArray, ((int const[]){2, 3}));
    int status = 0;

    arr_insert(array, 1, 2);
    arr_insert(array, 2, 3);
    status = expect_array_eq(name, array.data, array.size, INTS(1, 2, 3, 4));
    if (status != 0)
        goto cleanup;

    arr_insert_arr(array, 4, middle);
    status =
        expect_array_eq(name, array.data, array.size, INTS(1, 2, 3, 4, 2, 3));
    if (status != 0)
        goto cleanup;

    arr_insert_data(array, 6, middle.data, 1);
    status = expect_array_eq(name, array.data, array.size,
                             INTS(1, 2, 3, 4, 2, 3, 2));
    if (status != 0)
        goto cleanup;

    arr_append_arr(array, middle);
    status = expect_array_eq(name, array.data, array.size,
                             INTS(1, 2, 3, 4, 2, 3, 2, 2, 3));
    if (status != 0)
        goto cleanup;

    arr_append_data(array, middle.data, middle.size);
    status = expect_array_eq(name, array.data, array.size,
                             INTS(1, 2, 3, 4, 2, 3, 2, 2, 3, 2, 3));

cleanup:
    arr_free(middle);
    arr_free(array);
    return status;
}

static int test_remove_at(TestContext *ctx) {
    const char *name = ctx->test_name;
    IntArray front = arr_from_c(IntArray, ((int const[]){1, 2, 3, 4}));
    IntArray middle = arr_from_c(IntArray, ((int const[]){1, 2, 3, 4}));
    IntArray back = arr_from_c(IntArray, ((int const[]){1, 2, 3, 4}));
    int status = 0;

    // Removing from any position should keep the remaining elements packed and
    // preserve their relative order.
    arr_remove_at(front, 0);
    status = expect_array_eq(name, front.data, front.size, INTS(2, 3, 4));
    if (status != 0)
        goto cleanup;

    arr_remove_at(middle, 1);
    status = expect_array_eq(name, middle.data, middle.size, INTS(1, 3, 4));
    if (status != 0)
        goto cleanup;

    arr_remove_at(back, back.size - 1);
    status = expect_array_eq(name, back.data, back.size, INTS(1, 2, 3));

cleanup:
    arr_free(back);
    arr_free(middle);
    arr_free(front);
    return status;
}

int main(int argc, char **argv) {
    const Subtest subtests[] = {
        PREFIXED_TEST(test_growth_and_resize),
        PREFIXED_TEST(test_copy_from_data),
        PREFIXED_TEST(test_in_place_slice_ops),
        PREFIXED_TEST(test_append_and_insert),
        PREFIXED_TEST(test_remove_at),
    };

    return run_subtests(argc, argv, subtests, ARRAY_SIZE(subtests));
}
