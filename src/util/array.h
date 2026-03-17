#ifndef UTIL_ARRAY_H
#define UTIL_ARRAY_H

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

// Mark header-only helpers as inline and suppress unused warnings when a
// translation unit instantiates an array type but only uses part of its API.
#if defined(__GNUC__) || defined(__clang__)
#define ARRLIB_INLINE static inline __attribute__((unused))
#else
#define ARRLIB_INLINE static inline
#endif

typedef struct ArrSlice {
    size_t start;
    size_t end;
} ArrSlice;

// Abort immediately on allocation failure.
ARRLIB_INLINE void arr__abort_oom(void) { abort(); }

// Allocate or grow an array buffer to at least new_capacity elements and
// return the resulting data pointer.
ARRLIB_INLINE void *arr__reserve_impl(void *data, size_t *capacity,
                                      size_t new_capacity, size_t elem_size) {
    if (new_capacity <= *capacity)
        return data;

    if (new_capacity > (size_t)-1 / elem_size)
        arr__abort_oom();

    void *new_data = realloc(data, new_capacity * elem_size);
    if (new_data == NULL)
        arr__abort_oom();

    *capacity = new_capacity;
    return new_data;
}

// Grow capacity geometrically until it can hold min_capacity elements.
ARRLIB_INLINE size_t arr__grown_capacity(size_t capacity, size_t min_capacity) {
    size_t new_capacity = capacity == 0 ? 1 : capacity;

    while (new_capacity < min_capacity) {
        if (new_capacity > (size_t)-1 / 2)
            return min_capacity;
        new_capacity *= 2;
    }

    return new_capacity;
}

// Resize an owning array, zero-initializing any newly exposed elements, and
// return the resulting data pointer.
ARRLIB_INLINE void *arr__resize_impl(void *data, size_t *size, size_t *capacity,
                                     size_t new_size, size_t elem_size) {
    size_t old_size = *size;

    if (new_size > *capacity) {
        data = arr__reserve_impl(data, capacity,
                                 arr__grown_capacity(*capacity, new_size),
                                 elem_size);
    }

    if (new_size > old_size) {
        memset((char *)data + old_size * elem_size, 0,
               (new_size - old_size) * elem_size);
    }

    *size = new_size;
    return data;
}

// Convert a possibly-negative slice bound into a checked absolute index.
ARRLIB_INLINE size_t arr__normalize_bound(size_t size, ptrdiff_t index) {
    ptrdiff_t resolved = index;

    if (resolved < 0)
        resolved += (ptrdiff_t)size;

    assert(resolved >= 0);
    assert((size_t)resolved <= size);
    return (size_t)resolved;
}

// Resolve slice bounds, including negative indexes. Assert if the resulting
// slice is invalid.
ARRLIB_INLINE ArrSlice arr__slice_bounds(size_t size, ptrdiff_t start,
                                         ptrdiff_t end) {
    ArrSlice slice;

    slice.start = arr__normalize_bound(size, start);
    slice.end = arr__normalize_bound(size, end);
    assert(slice.start <= slice.end);
    return slice;
}

// Insert a non-overlapping range into an array and return the resulting data
// pointer. When other_data is NULL, the inserted range is left uninitialized
// for the caller to fill.
ARRLIB_INLINE void *arr__insert_arr_impl(void *data, size_t *size,
                                         size_t *capacity, size_t index,
                                         void const *other_data,
                                         size_t other_size, size_t elem_size) {
    char *base = data;
    size_t old_size = *size;

    assert(index <= old_size);
    if (other_size == 0)
        return data;

    // Assert that the new range doesn't overlap with the existing array.
    if (base != NULL && other_data != NULL) {
        assert((char const *)other_data + other_size * elem_size <= base ||
               (char const *)other_data >= base + old_size * elem_size &&
                   "inserting data from the same array is not supported");
    }

    // Resize the array.
    data = arr__resize_impl(data, size, capacity, old_size + other_size,
                            elem_size);
    base = data;

    // Move the tail of the existing array to make room for the new data.
    if (index < old_size) {
        memmove(base + (index + other_size) * elem_size,
                base + index * elem_size, (old_size - index) * elem_size);
    }

    // Copy the new data into the gap when the caller supplied a source buffer.
    if (other_data != NULL)
        memcpy(base + index * elem_size, other_data, other_size * elem_size);

    return data;
}

// Define an owning array type plus typed construction and copy helpers for
// ElemType.
#define DEFINE_ARRAY_TYPE(ArrayName, ElemType)                                 \
    typedef struct ArrayName {                                                 \
        ElemType *data;                                                        \
        size_t size;                                                           \
        size_t capacity;                                                       \
    } ArrayName;                                                               \
    /* Allocates an array copied from existing data, owned by caller. */       \
    ARRLIB_INLINE ArrayName make_##ArrayName(ElemType const *data,             \
                                             size_t size) {                    \
        ArrayName copy = arr_empty;                                            \
        copy.data =                                                            \
            arr__insert_arr_impl(copy.data, &copy.size, &copy.capacity, 0,     \
                                 data, size, sizeof(copy.data[0]));            \
        return copy;                                                           \
    }                                                                          \
    /* Copies an array, the result is owned by caller. */                      \
    ARRLIB_INLINE ArrayName copy_##ArrayName(ArrayName arr) {                  \
        return make_##ArrayName(arr.data, arr.size);                           \
    }

// Initialize an owning array to the empty state.
#define arr_empty                                                              \
    { NULL, 0, 0 }

// Materialize a fixed-size C array expression into a new owning array. The
// caller owns the result and frees it with arr_free.
#define arr_from_c(ArrayName, carr)                                            \
    make_##ArrayName((carr), sizeof(carr) / sizeof((carr)[0]))

// Free an owning array's storage and reset it to arr_empty.
#define arr_free(arr)                                                          \
    do {                                                                       \
        free((arr).data);                                                      \
        (arr).data = NULL;                                                     \
        (arr).size = 0;                                                        \
        (arr).capacity = 0;                                                    \
    } while (0)

// Reset an owning array's size to zero without releasing capacity.
#define arr_clear(arr)                                                         \
    do {                                                                       \
        (arr).size = 0;                                                        \
    } while (0)

// Ensure an owning array has capacity for at least new_capacity elements.
#define arr_reserve(arr, new_capacity)                                         \
    do {                                                                       \
        (arr).data = arr__reserve_impl((arr).data, &(arr).capacity,            \
                                       (new_capacity), sizeof((arr).data[0])); \
    } while (0)

// Append one element to the back of an owning array.
#define arr_push(arr, value)                                                   \
    do {                                                                       \
        (arr).data =                                                           \
            arr__resize_impl((arr).data, &(arr).size, &(arr).capacity,         \
                             (arr).size + 1, sizeof((arr).data[0]));           \
        (arr).data[(arr).size - 1] = (value);                                  \
    } while (0)

// Resize an owning array, zero-initializing any newly added elements.
#define arr_resize(arr, new_size)                                              \
    do {                                                                       \
        (arr).data =                                                           \
            arr__resize_impl((arr).data, &(arr).size, &(arr).capacity,         \
                             (new_size), sizeof((arr).data[0]));               \
    } while (0)

// Remove n elements from the back of an owning array in place.
#define arr_drop_back(arr, n)                                                  \
    do {                                                                       \
        assert((n) <= (arr).size);                                             \
        (arr).size -= (n);                                                     \
    } while (0)

// Remove n elements from the front of an owning array in place.
#define arr_drop_front(arr, n)                                                 \
    do {                                                                       \
        size_t arr__n = (n);                                                   \
        assert(arr__n <= (arr).size);                                          \
        memmove((arr).data, (arr).data + arr__n,                               \
                ((arr).size - arr__n) * sizeof((arr).data[0]));                \
        (arr).size -= arr__n;                                                  \
    } while (0)

// Keep only the last n elements of an owning array.
#define arr_take_back(arr, n)                                                  \
    do {                                                                       \
        size_t arr__n = (n);                                                   \
        assert(arr__n <= (arr).size);                                          \
        memmove((arr).data, (arr).data + (arr).size - arr__n,                  \
                arr__n * sizeof((arr).data[0]));                               \
        (arr).size = arr__n;                                                   \
    } while (0)

// Keep only the first n elements of an owning array.
#define arr_take_front(arr, n)                                                 \
    do {                                                                       \
        size_t arr__n = (n);                                                   \
        assert(arr__n <= (arr).size);                                          \
        (arr).size = arr__n;                                                   \
    } while (0)

// Replace an owning array with the half-open slice [start_index, end_index).
// Negative indexes are interpreted as indexes from the end (like in Python).
#define arr_slice(arr, start_index, end_index)                                 \
    do {                                                                       \
        ArrSlice arr__slice =                                                  \
            arr__slice_bounds((arr).size, (start_index), (end_index));         \
        memmove((arr).data, (arr).data + arr__slice.start,                     \
                (arr__slice.end - arr__slice.start) * sizeof((arr).data[0]));  \
        (arr).size = arr__slice.end - arr__slice.start;                        \
    } while (0)

// Append another owning array to the back of an owning array.
#define arr_append_arr(arr, other)                                             \
    do {                                                                       \
        (arr).data = arr__insert_arr_impl(                                     \
            (arr).data, &(arr).size, &(arr).capacity, (arr).size,              \
            (other).data, (other).size, sizeof((arr).data[0]));                \
    } while (0)

// Append raw data to the back of an owning array.
#define arr_append_data(arr, other_data, other_size)                           \
    do {                                                                       \
        (arr).data = arr__insert_arr_impl(                                     \
            (arr).data, &(arr).size, &(arr).capacity, (arr).size,              \
            (other_data), (other_size), sizeof((arr).data[0]));                \
    } while (0)

// Insert one element into an owning array at index, shifting later elements.
#define arr_insert(arr, index, value)                                          \
    do {                                                                       \
        size_t arr__index = (index);                                           \
        (arr).data =                                                           \
            arr__insert_arr_impl((arr).data, &(arr).size, &(arr).capacity,     \
                                 arr__index, NULL, 1, sizeof((arr).data[0]));  \
        (arr).data[arr__index] = (value);                                      \
    } while (0)

// Insert raw data at index, shifting later elements.
#define arr_insert_data(arr, index, other_data, other_size)                    \
    do {                                                                       \
        (arr).data = arr__insert_arr_impl(                                     \
            (arr).data, &(arr).size, &(arr).capacity, (index), (other_data),   \
            (other_size), sizeof((arr).data[0]));                              \
    } while (0)

// Insert another owning array at index, shifting later elements.
#define arr_insert_arr(arr, index, other)                                      \
    do {                                                                       \
        (arr).data = arr__insert_arr_impl(                                     \
            (arr).data, &(arr).size, &(arr).capacity, (index), (other).data,   \
            (other).size, sizeof((arr).data[0]));                              \
    } while (0)

#endif
