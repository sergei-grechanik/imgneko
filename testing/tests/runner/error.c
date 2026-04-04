#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "test_main.h"
#include "util/common.h"
#include "util/error.h"

static int test_die(TestContext *ctx) {
    (void)ctx;
    die("plain failure");
    return 0;
}

static int test_die_placeholder(TestContext *ctx) {
    (void)ctx;
    errno = ENOENT;
    die("placeholder failure: %errno");
    return 0;
}

static int test_die_errno(TestContext *ctx) {
    (void)ctx;
    errno = ENOENT;
    die_errno("errno failure");
    return 0;
}

static int test_require_fail(TestContext *ctx) {
    (void)ctx;
    errno = EACCES;
    require(false, "require failure: %errno");
    return 0;
}

static int test_require_pass(TestContext *ctx) {
    (void)ctx;
    require(true, "unexpected");
    puts("require passed");
    return 0;
}

int main(int argc, char **argv) {
    const Subtest subtests[] = {
        PREFIXED_XFAIL_TEST(test_die),
        PREFIXED_XFAIL_TEST(test_die_placeholder),
        PREFIXED_XFAIL_TEST(test_die_errno),
        PREFIXED_XFAIL_TEST(test_require_fail),
        PREFIXED_TEST(test_require_pass),
    };

    return run_subtests(argc, argv, subtests, ARRAY_SIZE(subtests));
}
